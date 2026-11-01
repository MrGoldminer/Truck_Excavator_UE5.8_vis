#include "Protocol/QuarryProtocol.h"

#include "Cargo/QuarryCargoComponent.h"
#include "Protocol/QuarryProtobuf.h"
#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"
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

	void FillVehicle(quarryworks::v1::VehicleTelemetry& Output, const AQuarryVehiclePawn* Vehicle,
		const UQuarryCargoComponent* Cargo, const char* Id)
	{
		if (!Vehicle || !Cargo) return;
		Output.set_vehicle_id(Id);
		Output.set_speed_kmh(Vehicle->VehicleMovement->GetSpeedKmh());
		Output.set_parking_brake(Vehicle->VehicleMovement->IsParkingBrakeEnabled());
		const FVector Location = Vehicle->GetActorLocation();
		auto* Position = Output.mutable_location_cm();
		Position->set_x(Location.X);
		Position->set_y(Location.Y);
		Position->set_z(Location.Z);
		auto* CargoOutput = Output.mutable_cargo();
		CargoOutput->set_material(ToProtocolMaterial(Cargo->MaterialType));
		CargoOutput->set_current_mass_kg(Cargo->CurrentMassKg);
		CargoOutput->set_capacity_kg(Cargo->CapacityKg);
	}
}

TArray<uint8> FQuarryProtocol::EncodeTelemetry(const UQuarryWorksiteSubsystem* Worksite)
{
	quarryworks::v1::QuarryTelemetry Telemetry;
	Telemetry.set_sequence(FPlatformTime::Cycles64());
	if (Worksite)
	{
		Telemetry.set_delivered_mass_kg(Worksite->GetDeliveredMassKg());
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
