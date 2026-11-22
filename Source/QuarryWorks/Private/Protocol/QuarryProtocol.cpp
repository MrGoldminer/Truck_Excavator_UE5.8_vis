#include "Protocol/QuarryProtocol.h"

#include "Cargo/QuarryCargoComponent.h"
#include "Components/BoxComponent.h"
#include "Protocol/QuarryProtobuf.h"
#include "Sensors/QuarryImuSensorComponent.h"
#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"
#include "Vehicles/QuarryVehiclePawn.h"
#include "Worksite/QuarryWorksiteSubsystem.h"
#include "google/protobuf/text_format.h"

namespace
{
	quarryworks::v1::MaterialType ToProtocolMaterial(EQuarryMaterialType Material)
	{
		switch (Material)
		{
		case EQuarryMaterialType::Sand: return quarryworks::v1::MATERIAL_TYPE_SAND;
		case EQuarryMaterialType::Gravel: return quarryworks::v1::MATERIAL_TYPE_GRAVEL;
		case EQuarryMaterialType::Ore: return quarryworks::v1::MATERIAL_TYPE_ORE;
		default: return quarryworks::v1::MATERIAL_TYPE_UNSPECIFIED;
		}
	}

	void FillVector3(quarryworks::v1::Vector3* Output, const FVector& Value)
	{
		Output->set_x(Value.X);
		Output->set_y(Value.Y);
		Output->set_z(Value.Z);
	}

	void FillWheels(quarryworks::v1::VehicleTelemetry& Output, const UQuarryVehicleMovementComponent* Movement)
	{
		for (const FQuarryWheelState& Wheel : Movement->GetWheelStates())
		{
			auto* WheelOutput = Output.add_wheels();
			WheelOutput->set_slip_ratio(Wheel.SlipRatio);
			WheelOutput->set_slip_angle_rad(Wheel.SlipAngleRad);
			WheelOutput->set_load_n(Wheel.LoadForceUE * 0.01);
			WheelOutput->set_angular_velocity_rad_s(Wheel.AngularVelocityRadS);
			WheelOutput->set_longitudinal_force_n(Wheel.TireLongForceUE * 0.01);
			WheelOutput->set_lateral_force_n(Wheel.TireLatForceUE * 0.01);
			WheelOutput->set_contact_friction(Wheel.ContactFriction);
			WheelOutput->set_in_contact(Wheel.bInContact);
		}
	}

	void FillVehicle(quarryworks::v1::VehicleTelemetry& Output, const AQuarryVehiclePawn* Vehicle,
		const UQuarryCargoComponent* Cargo, const char* Id)
	{
		if (!Vehicle || !Cargo) return;
		Output.set_vehicle_id(Id);
		Output.set_speed_kmh(Vehicle->VehicleMovement->GetSpeedKmh());
		Output.set_parking_brake(Vehicle->VehicleMovement->IsParkingBrakeEnabled());
		const FVector Location = Vehicle->GetActorLocation();
		FillVector3(Output.mutable_location_cm(), Location);

		auto* CargoOutput = Output.mutable_cargo();
		CargoOutput->set_material(ToProtocolMaterial(Cargo->MaterialType));
		CargoOutput->set_current_mass_kg(Cargo->CurrentMassKg);
		CargoOutput->set_capacity_kg(Cargo->CapacityKg);

		// CARLA-style ground truth: pose, twist, IMU, powertrain, per-wheel state.
		const FQuat Rotation = Vehicle->GetActorQuat();
		auto* Orientation = Output.mutable_orientation();
		Orientation->set_w(Rotation.W);
		Orientation->set_x(Rotation.X);
		Orientation->set_y(Rotation.Y);
		Orientation->set_z(Rotation.Z);
		FillVector3(Output.mutable_velocity_cm_s(), Vehicle->Chassis->GetPhysicsLinearVelocity());
		FillVector3(Output.mutable_angular_velocity_rad_s(), Vehicle->Chassis->GetPhysicsAngularVelocityInRadians());
		Output.set_engine_rpm(Vehicle->VehicleMovement->GetEngineRPM());
		Output.set_gear(Vehicle->VehicleMovement->GetCurrentGear());
		Output.set_grounded_wheel_count(Vehicle->VehicleMovement->GetGroundedWheelCount());
		Output.set_any_wheel_slipping(Vehicle->VehicleMovement->IsAnyWheelSlipping());
		FillWheels(Output, Vehicle->VehicleMovement);
		if (const UQuarryImuSensorComponent* Imu = Vehicle->ImuSensor)
		{
			auto* ImuOutput = Output.mutable_imu();
			FillVector3(ImuOutput->mutable_specific_force_m_s2(), Imu->GetSpecificForceMps2());
			FillVector3(ImuOutput->mutable_angular_velocity_rad_s(), Imu->GetAngularVelocityRadS());
		}
	}
}

TArray<uint8> FQuarryProtocol::EncodeTelemetry(const UQuarryWorksiteSubsystem* Worksite)
{
	quarryworks::v1::QuarryTelemetry Telemetry;
	Telemetry.set_sequence(FPlatformTime::Cycles64());
	if (Worksite)
	{
		Telemetry.set_delivered_mass_kg(Worksite->GetDeliveredMassKg());
		Telemetry.set_simulation_time_s(Worksite->GetSimulationTimeSeconds());
		if (const AQuarryExcavator* Excavator = Worksite->GetExcavator())
		{
			FillVehicle(*Telemetry.add_vehicles(), Excavator, Excavator->BucketCargo, "excavator");
		}
		if (const AQuarryMiningTruck* Truck = Worksite->GetMiningTruck())
		{
			FillVehicle(*Telemetry.add_vehicles(), Truck, Truck->Cargo, "mining_truck");
		}
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(Telemetry.ByteSizeLong());
	if (Bytes.Num() > 0 && !Telemetry.SerializeToArray(Bytes.GetData(), Bytes.Num()))
	{
		Bytes.Empty();
	}
	return Bytes;
}

bool FQuarryProtocol::ApplyVehicleCommand(const TArray<uint8>& CommandBytes, UQuarryWorksiteSubsystem* Worksite)
{
	if (!Worksite || CommandBytes.Num() == 0) return false;
	quarryworks::v1::VehicleCommand Command;
	if (!Command.ParseFromArray(CommandBytes.GetData(), CommandBytes.Num())) return false;

	AQuarryVehiclePawn* Vehicle = nullptr;
	if (Command.vehicle_id() == "excavator") Vehicle = Worksite->GetExcavator();
	if (Command.vehicle_id() == "mining_truck") Vehicle = Worksite->GetMiningTruck();
	if (!Vehicle) return false;

	Vehicle->SetDriveInput(Command.throttle(), Command.steering());
	Vehicle->SetServiceBrake(Command.service_brake());
	Vehicle->SetParkingBrake(Command.parking_brake());

	// Excavator actuators (slew/boom/stick/bucket/dig/dump).
	if (AQuarryExcavator* Excavator = Cast<AQuarryExcavator>(Vehicle))
	{
		if (Command.has_excavator())
		{
			const quarryworks::v1::ExcavatorCommand& Actuators = Command.excavator();
			Excavator->ApplyActuatorCommands(Actuators.slew(), Actuators.boom(), Actuators.stick(), Actuators.bucket());
			Excavator->SetDiggingActive(Actuators.dig());
			Excavator->SetDumpingActive(Actuators.dump());
		}
	}
	return true;
}

FString FQuarryProtocol::DecodeTelemetryForDebug(const TArray<uint8>& TelemetryBytes)
{
	quarryworks::v1::QuarryTelemetry Telemetry;
	if (TelemetryBytes.Num() == 0 || !Telemetry.ParseFromArray(TelemetryBytes.GetData(), TelemetryBytes.Num()))
	{
		return FString();
	}
	std::string Output;
	google::protobuf::TextFormat::PrintToString(Telemetry, &Output);
	return UTF8_TO_TCHAR(Output.c_str());
}
