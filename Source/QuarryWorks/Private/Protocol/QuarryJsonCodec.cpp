#include "Protocol/QuarryJsonCodec.h"

#include "Cargo/QuarryCargoComponent.h"
#include "Components/BoxComponent.h"
#include "Sensors/QuarryImuSensorComponent.h"
#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"
#include "Vehicles/QuarryVehiclePawn.h"
#include "Worksite/QuarryTaskExecutor.h"
#include "Worksite/QuarryWorksiteSubsystem.h"
#include "Dom/JsonObject.h"
#include "Json.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* MaterialToName(EQuarryMaterialType Material)
	{
		switch (Material)
		{
		case EQuarryMaterialType::Sand: return TEXT("sand");
		case EQuarryMaterialType::Gravel: return TEXT("gravel");
		case EQuarryMaterialType::Ore: return TEXT("ore");
		default: return TEXT("none");
		}
	}

	// UE world centimeters -> CARLA-style meters.
	constexpr double CmToM = 0.01;
	// Meters per degree of latitude/longitude at the geo origin.
	constexpr double MetersPerDegLat = 111320.0;
	double MetersPerDegLon(double LatDeg)
	{
		return MetersPerDegLat * FMath::Max(0.01, FMath::Cos(FMath::DegreesToRadians(LatDeg)));
	}
}

bool FQuarryJsonCodec::ParseControl(const FString& JsonText, FCarlaControl& OutControl)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	OutControl.VehicleId = JsonObject->GetStringField(TEXT("vehicle_id"));
	OutControl.Throttle = static_cast<float>(JsonObject->GetNumberField(TEXT("throttle")));
	OutControl.Steer = static_cast<float>(JsonObject->GetNumberField(TEXT("steer")));
	OutControl.Brake = static_cast<float>(JsonObject->GetNumberField(TEXT("brake")));
	OutControl.bHandBrake = JsonObject->GetBoolField(TEXT("hand_brake"));
	OutControl.bReverse = JsonObject->GetBoolField(TEXT("reverse"));
	OutControl.bValid = !OutControl.VehicleId.IsEmpty();
	return OutControl.bValid;
}

bool FQuarryJsonCodec::ParseTask(const FString& JsonText, FQuarryTaskCommand& OutTask)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	const FString Type = JsonObject->GetStringField(TEXT("type")).ToLower();
	if (Type == TEXT("drive_to")) OutTask.Type = EQuarryTaskType::DriveTo;
	else if (Type == TEXT("dig")) OutTask.Type = EQuarryTaskType::Dig;
	else if (Type == TEXT("haul")) OutTask.Type = EQuarryTaskType::Haul;
	else if (Type == TEXT("test_suite") || Type == TEXT("test")) OutTask.Type = EQuarryTaskType::TestSuite;
	else return false;

	OutTask.VehicleId = JsonObject->GetStringField(TEXT("vehicle"));
	if (OutTask.VehicleId.IsEmpty())
	{
		OutTask.VehicleId = OutTask.Type == EQuarryTaskType::Dig ? TEXT("excavator") : TEXT("mining_truck");
	}
	OutTask.TaskId = JsonObject->GetStringField(TEXT("task_id"));
	OutTask.TargetX_m = JsonObject->GetNumberField(TEXT("x"));
	OutTask.TargetY_m = JsonObject->GetNumberField(TEXT("y"));
	OutTask.Tolerance_m = JsonObject->HasField(TEXT("tolerance"))
		? static_cast<float>(JsonObject->GetNumberField(TEXT("tolerance"))) : 2.5f;
	return true;
}

FString FQuarryJsonCodec::EncodeTelemetry(const UQuarryWorksiteSubsystem* Worksite, double GnssOriginLat, double GnssOriginLon)
{
	const double MetersPerDegLonHere = MetersPerDegLon(GnssOriginLat);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("simulation_time_s"), Worksite ? Worksite->GetSimulationTimeSeconds() : 0.0);
	Root->SetNumberField(TEXT("delivered_mass_kg"), Worksite ? Worksite->GetDeliveredMassKg() : 0.0);

	TArray<TSharedPtr<FJsonValue>> Vehicles;
	if (Worksite)
	{
		auto AddVehicle = [&](const AQuarryVehiclePawn* Vehicle, const UQuarryCargoComponent* Cargo, const FString& Id)
		{
			if (!Vehicle || !Cargo || !Vehicle->VehicleMovement || !Vehicle->Chassis)
			{
				return;
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("id"), Id);

			// carla.Actor: get_location()/get_transform in meters (UE cm / 100).
			const FVector LocationCm = Vehicle->GetActorLocation();
			{
				TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
				Loc->SetNumberField(TEXT("x"), LocationCm.X * CmToM);
				Loc->SetNumberField(TEXT("y"), LocationCm.Y * CmToM);
				Loc->SetNumberField(TEXT("z"), LocationCm.Z * CmToM);
				Entry->SetObjectField(TEXT("location"), Loc);
			}

			// carla.Actor: get_velocity() in m/s.
			const FVector VelocityCmS = Vehicle->Chassis->GetPhysicsLinearVelocity();
			{
				TSharedPtr<FJsonObject> Vel = MakeShared<FJsonObject>();
				Vel->SetNumberField(TEXT("x"), VelocityCmS.X * CmToM);
				Vel->SetNumberField(TEXT("y"), VelocityCmS.Y * CmToM);
				Vel->SetNumberField(TEXT("z"), VelocityCmS.Z * CmToM);
				Entry->SetObjectField(TEXT("velocity"), Vel);
			}
			Entry->SetNumberField(TEXT("speed_kmh"), Vehicle->VehicleMovement->GetSpeedKmh());
			Entry->SetNumberField(TEXT("forward_speed_mps"), Vehicle->GetForwardSpeed() * CmToM);

			// carla.IMUMeasurement equivalents.
			if (Vehicle->ImuSensor)
			{
				TSharedPtr<FJsonObject> Imu = MakeShared<FJsonObject>();
				const FVector Accel = Vehicle->ImuSensor->GetSpecificForceMps2();
				{
					TSharedPtr<FJsonObject> AccelObj = MakeShared<FJsonObject>();
					AccelObj->SetNumberField(TEXT("x"), Accel.X);
					AccelObj->SetNumberField(TEXT("y"), Accel.Y);
					AccelObj->SetNumberField(TEXT("z"), Accel.Z);
					Imu->SetObjectField(TEXT("accelerometer"), AccelObj);
				}
				const FVector Gyro = Vehicle->ImuSensor->GetAngularVelocityRadS();
				{
					TSharedPtr<FJsonObject> GyroObj = MakeShared<FJsonObject>();
					GyroObj->SetNumberField(TEXT("x"), Gyro.X);
					GyroObj->SetNumberField(TEXT("y"), Gyro.Y);
					GyroObj->SetNumberField(TEXT("z"), Gyro.Z);
					Imu->SetObjectField(TEXT("gyroscope"), GyroObj);
				}
				Imu->SetNumberField(TEXT("compass"), Vehicle->ImuSensor->GetCompassRadians());
				Entry->SetObjectField(TEXT("imu"), Imu);
			}

			// carla.Sensor or geodesic projection of location.
			{
				TSharedPtr<FJsonObject> Gnss = MakeShared<FJsonObject>();
				Gnss->SetNumberField(TEXT("lat"),
					GnssOriginLat + (LocationCm.Y * CmToM) / MetersPerDegLat);
				Gnss->SetNumberField(TEXT("lon"),
					GnssOriginLon + (LocationCm.X * CmToM) / MetersPerDegLonHere);
				Gnss->SetNumberField(TEXT("alt"), LocationCm.Z * CmToM);
				Entry->SetObjectField(TEXT("gnss"), Gnss);
			}

			// Powertrain / dynamics diagnostics (project-specific extension).
			Entry->SetNumberField(TEXT("engine_rpm"), Vehicle->VehicleMovement->GetEngineRPM());
			Entry->SetNumberField(TEXT("gear"), Vehicle->VehicleMovement->GetCurrentGear());
			Entry->SetNumberField(TEXT("grounded_wheel_count"), Vehicle->VehicleMovement->GetGroundedWheelCount());
			Entry->SetBoolField(TEXT("any_wheel_slipping"), Vehicle->VehicleMovement->IsAnyWheelSlipping());
			Entry->SetBoolField(TEXT("parking_brake"), Vehicle->VehicleMovement->IsParkingBrakeEnabled());

			// Wheels: slip/load per wheel, mirroring CARLA wheel telemetry dumps.
			TArray<TSharedPtr<FJsonValue>> Wheels;
			for (const FQuarryWheelState& Wheel : Vehicle->VehicleMovement->GetWheelStates())
			{
				TSharedPtr<FJsonObject> WheelObj = MakeShared<FJsonObject>();
				WheelObj->SetNumberField(TEXT("slip_ratio"), Wheel.SlipRatio);
				WheelObj->SetNumberField(TEXT("slip_angle_rad"), Wheel.SlipAngleRad);
				WheelObj->SetNumberField(TEXT("load_n"), Wheel.LoadForceUE * 0.01);
				WheelObj->SetBoolField(TEXT("in_contact"), Wheel.bInContact);
				Wheels.Add(MakeShared<FJsonValueObject>(WheelObj));
			}
			Entry->SetArrayField(TEXT("wheels"), Wheels);

			// Cargo (project-specific).
			{
				TSharedPtr<FJsonObject> CargoObj = MakeShared<FJsonObject>();
				CargoObj->SetStringField(TEXT("material"), MaterialToName(Cargo->MaterialType));
				CargoObj->SetNumberField(TEXT("current_mass_kg"), Cargo->CurrentMassKg);
				CargoObj->SetNumberField(TEXT("capacity_kg"), Cargo->CapacityKg);
				Entry->SetObjectField(TEXT("cargo"), CargoObj);
			}
			Vehicles.Add(MakeShared<FJsonValueObject>(Entry));
		};

		if (const AQuarryExcavator* Excavator = Worksite->GetExcavator())
		{
			AddVehicle(Excavator, Excavator->BucketCargo, TEXT("excavator"));
		}
		if (const AQuarryMiningTruck* Truck = Worksite->GetMiningTruck())
		{
			AddVehicle(Truck, Truck->Cargo, TEXT("mining_truck"));
		}
	}
	Root->SetArrayField(TEXT("vehicles"), Vehicles);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString FQuarryJsonCodec::EncodeTaskResult(const FQuarryTaskCommand& Task, const FString& Result, const FString& Reason, double DeliveredMassKg, double ElapsedS)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("task_id"), Task.TaskId.IsEmpty() ? TEXT("anon") : Task.TaskId);
	Root->SetStringField(TEXT("type"),
		Task.Type == EQuarryTaskType::DriveTo ? TEXT("drive_to")
		: Task.Type == EQuarryTaskType::Dig ? TEXT("dig")
		: Task.Type == EQuarryTaskType::Haul ? TEXT("haul") : TEXT("test_suite"));
	Root->SetStringField(TEXT("result"), Result); // "completed" | "failed" | "timeout"
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetNumberField(TEXT("delivered_mass_kg"), DeliveredMassKg);
	Root->SetNumberField(TEXT("elapsed_s"), ElapsedS);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString FQuarryJsonCodec::EncodeTestCase(const FString& CaseName, bool bPass, const FString& MetricsJson, double DurationS)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("case"), CaseName);
	Root->SetBoolField(TEXT("pass"), bPass);
	Root->SetNumberField(TEXT("duration_s"), DurationS);
	if (!MetricsJson.IsEmpty())
	{
		// Embed pre-serialized metrics as a nested object.
		TSharedPtr<FJsonObject> Metrics;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MetricsJson);
		if (FJsonSerializer::Deserialize(Reader, Metrics) && Metrics.IsValid())
		{
			Root->SetObjectField(TEXT("metrics"), Metrics.ToSharedRef());
		}
	}

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString FQuarryJsonCodec::EncodeTestSummary(const TArray<FString>& CaseResults, int32 NumPassed, int32 NumTotal)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("type"), TEXT("test_suite_summary"));
	Root->SetBoolField(TEXT("overall_pass"), NumPassed == NumTotal);
	Root->SetNumberField(TEXT("passed"), NumPassed);
	Root->SetNumberField(TEXT("total"), NumTotal);
	TArray<TSharedPtr<FJsonValue>> Cases;
	for (const FString& CaseResult : CaseResults)
	{
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CaseResult);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			Cases.Add(MakeShared<FJsonValueObject>(Parsed.ToSharedRef()));
		}
		else
		{
			Cases.Add(MakeShared<FJsonValueString>(CaseResult));
		}
	}
	Root->SetArrayField(TEXT("cases"), Cases);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString FQuarryJsonCodec::EncodeStatus(bool bOnline)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), bOnline ? TEXT("online") : TEXT("offline"));
	Root->SetNumberField(TEXT("time_s"), FPlatformTime::Seconds());

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
