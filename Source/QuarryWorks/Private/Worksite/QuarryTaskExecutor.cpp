#include "Worksite/QuarryTaskExecutor.h"

#include "Cargo/QuarryCargoComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/QuarryMaterialPile.h"
#include "Misc/ConfigCacheIni.h"
#include "Protocol/QuarryJsonCodec.h"
#include "Terrain/QuarryDigSite.h"
#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"
#include "Vehicles/QuarryVehiclePawn.h"
#include "Worksite/QuarryWorksiteSubsystem.h"

namespace
{
	enum class EStepResult : uint8 { Running, Completed, Failed };

	// Config keys under [QuarryWorks] in DefaultGame.ini.
	float GetConfigFloat(const TCHAR* Key, float Default)
	{
		float Value = Default;
		GConfig->GetFloat(TEXT("QuarryWorks"), Key, Value, GGameIni);
		return Value;
	}
	double GetConfigDouble(const TCHAR* Key, double Default)
	{
		double Value = Default;
		GConfig->GetDouble(TEXT("QuarryWorks"), Key, Value, GGameIni);
		return Value;
	}

	float FlatDistanceM(const FVector& A, const FVector& B)
	{
		return static_cast<float>(FVector::Dist2D(A, B)) * 0.01f;
	}

	float YawToTarget(const FVector& From, const FVector& To)
	{
		const FVector Delta = To - From;
		return FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	}
}

void FQuarryTaskExecutor::Initialize(UQuarryWorksiteSubsystem* InWorksite)
{
	Worksite = InWorksite;
}

bool FQuarryTaskExecutor::EnqueueTask(const FQuarryTaskCommand& Task)
{
	if (PendingTasks.Num() >= 8)
	{
		return false;
	}
	if (!Task.TaskId.IsEmpty())
	{
		PendingTasks.Add(Task);
		return true;
	}
	PendingTasks.Add(Task);
	return true;
}

void FQuarryTaskExecutor::Tick(float DeltaSeconds)
{
	if (bIdle)
	{
		if (PendingTasks.IsEmpty())
		{
			return;
		}
		StartTask(PendingTasks[0]);
		PendingTasks.RemoveAt(0, 1, EAllowShrinking::No);
	}

	TaskElapsedS += DeltaSeconds;
	switch (Current.Type)
	{
	case EQuarryTaskType::DriveTo: TickDriveTo(DeltaSeconds); break;
	case EQuarryTaskType::Dig: TickDig(DeltaSeconds); break;
	case EQuarryTaskType::Haul: TickHaul(DeltaSeconds); break;
	case EQuarryTaskType::TestSuite: TickTestSuite(DeltaSeconds); break;
	}
}

void FQuarryTaskExecutor::StartTask(const FQuarryTaskCommand& Task)
{
	Current = Task;
	bIdle = false;
	TaskElapsedS = 0.0f;
	LastDistanceM = TNumericLimits<float>::Max();
	DigPhase = 0;
	DigPhaseElapsedS = 0.0f;
	bWheeledReversing = false;
	SweepDirection = 1.0f;
	HaulPhase = 0;
	Test = FTestState();
	TestResults.Reset();
	DeliveredAtStartKg = Worksite ? Worksite->GetDeliveredMassKg() : 0.0;
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks task started: id=%s type=%d vehicle=%s"),
		*Current.TaskId, static_cast<int32>(Current.Type), *Current.VehicleId);
}

void FQuarryTaskExecutor::FinishTask(const FString& Result, const FString& Reason)
{
	// Zero every controlled input: a finished task must not leave throttles or
	// brakes latched on the vehicles.
	if (Worksite)
	{
		for (AQuarryVehiclePawn* Vehicle : { static_cast<AQuarryVehiclePawn*>(Worksite->GetExcavator()), static_cast<AQuarryVehiclePawn*>(Worksite->GetMiningTruck()) })
		{
			if (Vehicle)
			{
				Vehicle->SetDriveInput(0.0f, 0.0f);
				Vehicle->SetServiceBrake(0.0f);
			}
		}
		if (AQuarryExcavator* Excavator = Worksite->GetExcavator())
		{
			Excavator->SetDiggingActive(false);
			Excavator->SetDumpingActive(false);
		}
		if (AQuarryMiningTruck* Truck = Worksite->GetMiningTruck())
		{
			Truck->StopUnloading();
		}
	}
	bIdle = true;
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks task finished: id=%s result=%s (%s) elapsed=%.1fs"),
		*Current.TaskId, *Result, *Reason, TaskElapsedS);
	PublishTaskResult(Result, Reason);
}

void FQuarryTaskExecutor::PublishTaskResult(const FString& Result, const FString& Reason)
{
	if (!Worksite)
	{
		return;
	}
	const FString Json = FQuarryJsonCodec::EncodeTaskResult(Current, Result, Reason,
		Worksite->GetDeliveredMassKg(), TaskElapsedS);
	Worksite->PublishMqtt(TEXT("/task/result"), Json);
}

void FQuarryTaskExecutor::PublishTestCase(const FString& CaseName, bool bPass, const FString& MetricsJson, double DurationS)
{
	const FString Json = FQuarryJsonCodec::EncodeTestCase(CaseName, bPass, MetricsJson, DurationS);
	TestResults.Add(Json);
	if (Worksite)
	{
		Worksite->PublishMqtt(TEXT("/test/result"), Json);
	}
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks test case %s -> %s (%.1fs)"), *CaseName, bPass ? TEXT("PASS") : TEXT("FAIL"), DurationS);
}

// ---------------------------------------------------------------- drive-to

AQuarryVehiclePawn* FindVehicleById(UQuarryWorksiteSubsystem* Worksite, const FString& VehicleId)
{
	if (!Worksite)
	{
		return nullptr;
	}
	if (VehicleId == TEXT("excavator"))
	{
		return Worksite->GetExcavator();
	}
	if (VehicleId == TEXT("mining_truck") || VehicleId.IsEmpty())
	{
		return Worksite->GetMiningTruck();
	}
	return nullptr;
}

/**
 * Shared heading-error controller on the force-driven model. Returns
 * EStepResult::Completed once inside tolerance with the vehicle braked to a
 * stop, Failed if the pawn or movement is missing.
 */
EStepResult DriveVehicleToward(AQuarryVehiclePawn* Vehicle, const FVector& Target2D, float ToleranceM,
	float MaxThrottle, float DeltaSeconds)
{
	UQuarryVehicleMovementComponent* Movement = Vehicle ? Vehicle->VehicleMovement : nullptr;
	if (!Movement)
	{
		return EStepResult::Failed;
	}


	const FVector Location = Vehicle->GetActorLocation();
	const float DistanceM = FlatDistanceM(Location, Target2D);
	const float SpeedKmh = Movement->GetSpeedKmh();

	if (DistanceM <= ToleranceM)
	{
		// Inside tolerance: brake to a standstill before declaring arrival.
		Vehicle->SetDriveInput(0.0f, 0.0f);
		Vehicle->SetServiceBrake(1.0f);
		if (SpeedKmh < 1.5f)
		{
			Vehicle->SetServiceBrake(0.0f);
			return EStepResult::Completed;
		}
		return EStepResult::Running;
	}

	const float HeadingError = FMath::UnwindDegrees(YawToTarget(Location, Target2D) - Vehicle->GetActorRotation().Yaw);
	// A driving controller owns the parking brake: release it on throttle-up
	// (spawned vehicles start parked), re-engage it on the stop branch above.
	Vehicle->SetParkingBrake(false);
	Vehicle->SetServiceBrake(0.0f);
	if (Movement->DriveType == EQuarryDriveType::Tracked && FMath::Abs(HeadingError) > 15.0f)
	{
		// Tracked vehicles pivot in place until roughly aligned; a saturated
		// steer plus forward speed just circles the target without converging.
		Vehicle->SetDriveInput(0.0f, FMath::Sign(HeadingError));
		return EStepResult::Running;
	}
	if (Movement->DriveType == EQuarryDriveType::Wheeled && FMath::Abs(HeadingError) > 90.0f)
		{
		// Back the truck to the target tail-first: a dump truck unloads from
		// its bed, so the nose heading is irrelevant. Reversing with the tail
		// steered toward the target avoids U-turns entirely.
		const float TailHeadingError = FMath::UnwindDegrees(
			YawToTarget(Location, Target2D) - (Vehicle->GetActorRotation().Yaw + 180.0f));
		// Distance-proportional reverse throttle: a 30t truck at full reverse
		// overshoots the tolerance band and oscillates around it.
		// Two-stage approach: full reverse far away, creep inside 8 m so the
		// 30t momentum always stops inside the tolerance band.
		const float ReverseThrottle = -(DistanceM < 8.0f ? 0.15f : 0.45f);
		Vehicle->SetDriveInput(ReverseThrottle, FMath::Clamp(-TailHeadingError / 30.0f, -1.0f, 1.0f));
		return EStepResult::Running;
	}
	// Keep the steer inside a radius the loaded truck can actually hold: full
		// lock demands a ~6 m circle whose lateral force saturates the friction
		// circle and stalls the truck at 20 cm/s. Cap at ~9 m radius instead.
	const float Steer = FMath::Clamp(HeadingError / 30.0f, -0.85f, 0.85f);
	const float Throttle = FMath::Clamp(DistanceM / 8.0f, 0.15f, 1.0f) * MaxThrottle;
	Vehicle->SetDriveInput(Throttle, Steer);
	return EStepResult::Running;
}

void FQuarryTaskExecutor::TickDriveTo(float DeltaSeconds)
{
	AQuarryVehiclePawn* Vehicle = FindVehicleById(Worksite, Current.VehicleId);
	if (!Vehicle)
	{
		FinishTask(TEXT("failed"), TEXT("vehicle not found"));
		return;
	}
	const FVector Target(Current.TargetX_m * 100.0, Current.TargetY_m * 100.0, 0.0);
	if (TaskElapsedS > 150.0f)
	{
		FinishTask(TEXT("timeout"), FString::Printf(TEXT("distance remaining %.1f m"), LastDistanceM));
		return;
	}
	const EStepResult Step = DriveVehicleToward(Vehicle, Target, Current.Tolerance_m, 1.0f, DeltaSeconds);
	LastDistanceM = FlatDistanceM(Vehicle->GetActorLocation(), Target);
	if (Step == EStepResult::Completed)
	{
		FinishTask(TEXT("completed"), FString::Printf(TEXT("reached within %.1f m"), Current.Tolerance_m));
	}
	else if (Step == EStepResult::Failed)
	{
		FinishTask(TEXT("failed"), TEXT("vehicle movement missing"));
	}
}

// ---------------------------------------------------------------- dig

namespace
{
	/**
	 * Reusable excavator dig state machine (approach slew -> reach -> sweep ->
	 * load truck). Operates purely through actuator commands so the same code
	 * backs both the standalone dig task and the auto haul-cycle test case.
	 */
	EStepResult TickDigMachine(UQuarryWorksiteSubsystem* Worksite, int32& Phase, float& Sweep, float PhaseTime, float DeltaSeconds)
	{
		AQuarryExcavator* Excavator = Worksite ? Worksite->GetExcavator() : nullptr;
		if (!Excavator || !Excavator->BucketCargo)
		{
			return EStepResult::Failed;
		}
		AQuarryMaterialPile* Pile = Worksite->GetDigSite();


		switch (Phase)
		{
		case 0: // slew the upper structure toward the pile
		{
			if (!Pile)
			{
				return EStepResult::Failed;
			}
			// The slew rotates the upper structure, not the actor, so measure
			// the error against the structure's world yaw (actor + SlewAngle).
			const float StructureYaw = Excavator->UpperStructurePivot->GetComponentRotation().Yaw;
			const float HeadingError = FMath::UnwindDegrees(
				YawToTarget(Excavator->GetActorLocation(), Pile->GetActorLocation()) - StructureYaw);
			Excavator->ApplyActuatorCommands(FMath::Clamp(HeadingError / 45.0f, -1.0f, 1.0f), 0.0f, 0.0f, 0.0f);
			if (FMath::Abs(HeadingError) < 10.0f)
			{
				Excavator->ApplyActuatorCommands(0.0f, 0.0f, 0.0f, 0.0f);
				Phase = 1;
			}
			return PhaseTime > 15.0f ? EStepResult::Failed : EStepResult::Running;
		}
		case 1: // lower the boom and extend the stick until the teeth reach the pile surface
		{
			const float TipDistance = Pile->GetDistanceToWorkSurface(Excavator->BucketTip->GetComponentLocation());
			const float DepthError = FMath::Clamp(TipDistance / 400.0f, -1.0f, 1.0f);
			Excavator->ApplyActuatorCommands(0.0f, -0.45f, 0.6f, -0.4f);
			if (TipDistance < 160.0f)
			{
				Excavator->SetDiggingActive(true);
				Phase = 2;
				Sweep = 1.0f;
			}
			return PhaseTime > 20.0f ? EStepResult::Failed : EStepResult::Running;
		}
		case 2: // sweep the stick while holding the optimal cut angle with the bucket
		{
			const float BucketPitch = FMath::UnwindDegrees(Excavator->BucketPivot->GetComponentRotation().Pitch);
			const float CutError = FMath::UnwindDegrees(BucketPitch - Excavator->OptimalCutAngleDeg);
			const float BucketCmd = FMath::Clamp(CutError / 40.0f, -0.9f, 0.9f);
			// Oscillate the stick to keep the teeth moving through the material.
			Sweep += DeltaSeconds * 0.7f;
			const float StickCmd = 0.55f * FMath::Sin(Sweep * PI);
			Excavator->ApplyActuatorCommands(0.0f, -0.35f, StickCmd, BucketCmd);
			if (Excavator->WorkState != EQuarryExcavatorWorkState::Digging)
			{
				// The dig auto-cancels whenever the teeth leave the work radius
				// mid-sweep; re-arm and keep biting like a real operator does.
				Excavator->SetDiggingActive(false);
				if (Excavator->BucketCargo->GetFreeCapacityKg() > 100.0f)
				{
					Excavator->SetDiggingActive(true);
					return EStepResult::Running;
				}
				// The machine stopped because the bucket is full.
				return EStepResult::Completed;
			}
			// A partially loaded bucket covers the haul metric comfortably.
			if (Excavator->BucketCargo->CurrentMassKg >= 600.0f)
			{
				return EStepResult::Completed;
			}
			return PhaseTime > 200.0f ? EStepResult::Failed : EStepResult::Running;
		}
		case 3: // swing to the truck and dump
		{
			AQuarryMiningTruck* Truck = Worksite->GetMiningTruck();
			const float BucketLoadKg = Excavator->BucketCargo->CurrentMassKg;
			if (BucketLoadKg <= 1.0f)
			{
				return EStepResult::Completed; // already unloaded (or nothing dug)
			}
			if (!Truck)
			{
				return BucketLoadKg > 0.0f ? EStepResult::Completed : EStepResult::Failed;
			}
			const float IntakeDistance = static_cast<float>(FVector::Dist(
				Excavator->BucketTip->GetComponentLocation(), Truck->GetCargoIntakeLocation()));
			if (IntakeDistance > Excavator->WorkRadius)
			{
				// Swing the upper structure over the truck bed. The slew error is
				// measured on the structure yaw (actor + slew) and damped near the
				// target so the kinematic slew does not oscillate past it.
				const float StructureYaw = Excavator->UpperStructurePivot->GetComponentRotation().Yaw;
				const float HeadingError = FMath::UnwindDegrees(
					YawToTarget(Excavator->GetActorLocation(), Truck->GetCargoIntakeLocation()) - StructureYaw);
				const float SlewCmd = FMath::Clamp(HeadingError / 20.0f, -1.0f, 1.0f);
				// Reach toward the truck: stick out, boom near level, bucket curled.
				Excavator->ApplyActuatorCommands(SlewCmd, -0.05f, 0.6f, -0.3f);
				return EStepResult::Running;
			}
			Excavator->ApplyActuatorCommands(0.0f, 0.15f, 0.35f, -0.7f);
			Excavator->SetDumpingActive(true);
			return EStepResult::Running;
		}
		default:
			return EStepResult::Failed;
		}
	}
}

void FQuarryTaskExecutor::TickDig(float DeltaSeconds)
{
	const int32 PhaseBefore = DigPhase;
	const EStepResult Step = TickDigMachine(Worksite, DigPhase, SweepDirection, DigPhaseElapsedS, DeltaSeconds);
	if (PhaseBefore != DigPhase)
	{
		DigPhaseElapsedS = 0.0f; // reset the per-phase timeout budget
	}
	else
	{
		DigPhaseElapsedS += DeltaSeconds;
	}
	if (Step == EStepResult::Completed)
	{
		FinishTask(TEXT("completed"), FString::Printf(TEXT("dig cycle done, phase=%d"), DigPhase));
	}
	else if (Step == EStepResult::Failed)
	{
		FinishTask(TEXT("failed"), FString::Printf(TEXT("dig stalled in phase %d"), DigPhase));
		if (AQuarryExcavator* Excavator = Worksite ? Worksite->GetExcavator() : nullptr)
		{
			Excavator->SetDiggingActive(false);
			Excavator->SetDumpingActive(false);
		}
	}
}

// ---------------------------------------------------------------- haul

void FQuarryTaskExecutor::TickHaul(float DeltaSeconds)
{
	AQuarryMiningTruck* Truck = Cast<AQuarryMiningTruck>(FindVehicleById(Worksite, Current.VehicleId));
	AQuarryMaterialPile* Depot = Worksite ? Worksite->GetDeliveryPile() : nullptr;
	if (!Truck || !Depot)
	{
		FinishTask(TEXT("failed"), TEXT("truck or depot missing"));
		return;
	}

	if (TaskElapsedS > 150.0f)
	{
		Truck->StopUnloading();
		FinishTask(TEXT("timeout"), FString::Printf(TEXT("haul stalled in phase %d"), HaulPhase));
		return;
	}

	if (HaulPhase == 0)
	{
		// Pull up next to the depot pad so the dump point sits in range.
		const FVector Target = Depot->GetActorLocation() + FVector(250.0, 0.0, 0.0);
		const EStepResult Step = DriveVehicleToward(Truck, Target, 2.5f, 0.8f, DeltaSeconds);
		if (Step == EStepResult::Completed)
		{
			Truck->ToggleUnloading();
			HaulPhase = 1;
		}
		else if (Step == EStepResult::Failed)
		{
			FinishTask(TEXT("failed"), TEXT("drive to depot failed"));
		}
		return;
	}

	if (Truck->Cargo && Truck->Cargo->CurrentMassKg < 1.0f)
	{
		Truck->StopUnloading();
		FinishTask(TEXT("completed"), FString::Printf(TEXT("delivered %.0f kg total"),
			Worksite->GetDeliveredMassKg()));
	}
}

// ---------------------------------------------------------------- test suite

void FQuarryTaskExecutor::TickTestSuite(float DeltaSeconds)
{
	if (Test.CaseIndex == 0 && Test.NumTotal == 0)
	{
		Test.NumTotal = 6;
		Test.TotalElapsedS = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("QuarryWorks test suite starting (6 cases)"));
		BeginTestCase(0);
	}

	Test.TotalElapsedS += DeltaSeconds;
	Test.CaseElapsedS += DeltaSeconds;
	Test.PhaseElapsedS += DeltaSeconds;
	TickTestCase(DeltaSeconds);
}

void FQuarryTaskExecutor::BeginTestCase(int32 Index)
{
	Test.CaseIndex = Index;
	Test.CaseElapsedS = 0.0f;
	Test.PhaseElapsedS = 0.0f;
	Test.bBrakePhase = false;
	Test.bSlipSeen = false;
	Test.DriveDistanceM = 0.0f;
	Test.MaxZRangeCm = 0.0f;
	Test.SettleWindowS = 0.0f;
	AQuarryMiningTruck* Truck = Worksite ? Worksite->GetMiningTruck() : nullptr;
	if (Truck)
	{
		Test.LastLocationCm = Truck->GetActorLocation();
		Test.StartLocationCm = Test.LastLocationCm;
	}
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks test case %d begins"), Index);
}

void FQuarryTaskExecutor::AdvanceTestCase(bool bPass, const FString& MetricsJson)
{
	PublishTestCase(TestCaseName(Test.CaseIndex), bPass, MetricsJson, Test.CaseElapsedS);
	Test.NumPassed += bPass ? 1 : 0;
	if (Test.CaseIndex + 1 >= Test.NumTotal)
	{
		const FString Summary = FQuarryJsonCodec::EncodeTestSummary(TestResults, Test.NumPassed, Test.NumTotal);
		if (Worksite)
		{
			Worksite->PublishMqtt(TEXT("/test/result"), Summary);
		}
		UE_LOG(LogTemp, Log, TEXT("QuarryWorks test suite done: %d/%d passed"), Test.NumPassed, Test.NumTotal);
		FinishTask(Test.NumPassed == Test.NumTotal ? TEXT("completed") : TEXT("failed"),
			FString::Printf(TEXT("suite %d/%d passed"), Test.NumPassed, Test.NumTotal));
		return;
	}
	BeginTestCase(Test.CaseIndex + 1);
}

FString FQuarryTaskExecutor::TestCaseName(int32 Index) const
{
	switch (Index)
	{
	case 0: return TEXT("spawn_stability");
	case 1: return TEXT("wheel_contact");
	case 2: return TEXT("drive_brake");
	case 3: return TEXT("suspension_settle");
	case 4: return TEXT("sand_slip");
	case 5: return TEXT("auto_haul_cycle");
	default: return FString::Printf(TEXT("case_%d"), Index);
	}
}

void FQuarryTaskExecutor::TickTestCase(float DeltaSeconds)
{
	AQuarryMiningTruck* Truck = Worksite ? Worksite->GetMiningTruck() : nullptr;
	AQuarryExcavator* Excavator = Worksite ? Worksite->GetExcavator() : nullptr;
	const float StabilityMaxCm = GetConfigFloat(TEXT("TestStabilityMaxCm"), 5.0f);
	const float BrakeStopMaxM = GetConfigFloat(TEXT("TestBrakeStopMaxM"), 20.0f);
	const float RollMaxDeg = GetConfigFloat(TEXT("TestRollMaxDeg"), 25.0f);
	const float SettleMaxCm = GetConfigFloat(TEXT("TestSettleMaxCm"), 1.5f);
	const float HaulMinKg = GetConfigFloat(TEXT("TestHaulCycleMinKg"), 500.0f);
	const float HaulTimeoutS = GetConfigFloat(TEXT("TestHaulCycleTimeoutS"), 150.0f);

	switch (Test.CaseIndex)
	{
	case 0: // spawn_stability: parked truck must not drift
	{
		if (Truck && Truck->VehicleMovement)
		{
			Truck->SetParkingBrake(true);
			const float DisplacementCm = static_cast<float>(FVector::Dist2D(Truck->GetActorLocation(), Test.StartLocationCm));
			if (DisplacementCm > Test.MaxZRangeCm)
			{
				Test.MaxZRangeCm = DisplacementCm;
			}
			if (Test.CaseElapsedS >= 5.0f)
			{
				Truck->SetParkingBrake(false);
				AdvanceTestCase(Test.MaxZRangeCm <= StabilityMaxCm,
					FString::Printf(TEXT("{\"max_displacement_cm\":%.2f,\"limit_cm\":%.2f}"), Test.MaxZRangeCm, StabilityMaxCm));
			}
			return;
		}
		AdvanceTestCase(false, TEXT("{\"error\":\"truck missing\"}"));
		return;
	}
	case 1: // wheel_contact: all wheels grounded at rest
	{
		const int32 TruckGrounded = Truck && Truck->VehicleMovement ? Truck->VehicleMovement->GetGroundedWheelCount() : -1;
		const int32 TruckTotal = Truck && Truck->VehicleMovement ? Truck->VehicleMovement->GetNumWheels() : 0;
		const int32 ExcavatorGrounded = Excavator && Excavator->VehicleMovement ? Excavator->VehicleMovement->GetGroundedWheelCount() : -1;
		const int32 ExcavatorTotal = Excavator && Excavator->VehicleMovement ? Excavator->VehicleMovement->GetNumWheels() : 0;
		if (Test.CaseElapsedS >= 2.0f)
		{
			const bool bPass = TruckGrounded == TruckTotal && ExcavatorGrounded == ExcavatorTotal && TruckTotal > 0 && ExcavatorTotal > 0;
			AdvanceTestCase(bPass, FString::Printf(
				TEXT("{\"truck_ground\":%d/%d,\"excavator_ground\":%d/%d}"),
				TruckGrounded, TruckTotal, ExcavatorGrounded, ExcavatorTotal));
		}
		return;
	}
	case 2: // drive_brake: accelerate 60 m straight, full brake, measure stop distance and roll
	{
		if (!Truck || !Truck->VehicleMovement)
		{
			AdvanceTestCase(false, TEXT("{\"error\":\"truck missing\"}"));
			return;
		}
		const float DistanceFromStartM = FlatDistanceM(Truck->GetActorLocation(), Test.StartLocationCm);
		if (!Test.bBrakePhase)
		{
			Truck->SetParkingBrake(false);
			Truck->SetServiceBrake(0.0f);
			Truck->SetDriveInput(0.75f, 0.0f);
			if (DistanceFromStartM >= 30.0f)
			{
				Test.bBrakePhase = true;
				Test.LastLocationCm = Truck->GetActorLocation();
				Test.DriveDistanceM = DistanceFromStartM;
				Test.PhaseElapsedS = 0.0f; // braking gets its own budget
				Test.SettleWindowS = 0.0f; // time below the stop threshold
			}
			if (Test.PhaseElapsedS > 25.0f)
			{
				Truck->SetServiceBrake(1.0f);
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks drive_brake: drive phase distance %.1f m speed %.1f km/h"),
					DistanceFromStartM, Truck->VehicleMovement->GetSpeedKmh());
				AdvanceTestCase(false, FString::Printf(TEXT("{\"reason\":\"did not reach 60m\",\"distance_m\":%.1f}"), DistanceFromStartM));
			}
			return;
		}

		// Braking phase: require the truck to hold below the threshold briefly,
		// so suspension micro-oscillation does not fake a moving vehicle.
		Truck->SetDriveInput(0.0f, 0.0f);
		Truck->SetServiceBrake(1.0f);
		const float SpeedKmh = Truck->VehicleMovement->GetSpeedKmh();
		Test.SettleWindowS = SpeedKmh < 1.0f ? Test.SettleWindowS + DeltaSeconds : 0.0f;
		if (Test.SettleWindowS >= 1.5f)
		{
			const float StopDistanceM = FlatDistanceM(Truck->GetActorLocation(), Test.LastLocationCm);
			const float RollDeg = FMath::Abs(FMath::UnwindDegrees(Truck->GetActorRotation().Roll));
			Truck->SetServiceBrake(0.0f);
			const bool bPass = StopDistanceM <= BrakeStopMaxM && RollDeg <= RollMaxDeg;
			AdvanceTestCase(bPass, FString::Printf(
				TEXT("{\"stop_distance_m\":%.2f,\"limit_m\":%.2f,\"roll_deg\":%.1f,\"limit_deg\":%.1f}"),
				StopDistanceM, BrakeStopMaxM, RollDeg, RollMaxDeg));
			return;
		}
		if (Test.PhaseElapsedS > 15.0f)
		{
			UE_LOG(LogTemp, Log, TEXT("QuarryWorks drive_brake: brake timeout speed %.1f km/h grounded %d slip %d"),
				SpeedKmh, Truck->VehicleMovement->GetGroundedWheelCount(), Truck->VehicleMovement->IsAnyWheelSlipping());
			AdvanceTestCase(false, FString::Printf(TEXT("{\"reason\":\"braking did not stop\",\"speed_kmh\":%.2f}"), SpeedKmh));
		}
		return;
	}
	case 4: // sand_slip: full throttle near the dig mound must break traction
	{
		if (!Truck || !Truck->VehicleMovement || !Worksite->GetDigSite())
		{
			AdvanceTestCase(false, TEXT("{\"error\":\"truck or dig site missing\"}"));
			return;
		}
		const FVector MoundTarget = Worksite->GetDigSite()->GetActorLocation();
		if (!Test.bBrakePhase) // reuse the flag as "approach done"
		{
			// 12 m tolerance stops just outside the loose footprint; the slip
			// window then drives onto the mound and must break traction there.
			const EStepResult Step = DriveVehicleToward(Truck, MoundTarget, 12.0f, 0.9f, DeltaSeconds);
			if (Step == EStepResult::Completed)
			{
				Test.bBrakePhase = true;
				Test.PhaseElapsedS = 0.0f; // fresh slip window
			}
			if (Test.PhaseElapsedS > 40.0f)
			{
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks sand_slip: approach timeout at %.1f m from mound"),
					FlatDistanceM(Truck->GetActorLocation(), MoundTarget));
				Truck->SetDriveInput(0.0f, 0.0f);
				Truck->SetServiceBrake(1.0f);
				AdvanceTestCase(false, TEXT("{\"reason\":\"could not approach the mound\"}"));
			}
			return;
		}
		Truck->SetDriveInput(1.0f, 0.0f);
		Truck->SetServiceBrake(0.0f);
		Test.bSlipSeen = Test.bSlipSeen || Truck->VehicleMovement->IsAnyWheelSlipping();
		if (Test.bSlipSeen)
		{
			Truck->SetDriveInput(0.0f, 0.0f);
			Truck->SetServiceBrake(1.0f);
			AdvanceTestCase(true, FString::Printf(TEXT("{\"slip_after_s\":%.2f}"), Test.PhaseElapsedS));
			return;
		}
		if (Test.PhaseElapsedS > 5.0f)
		{
			Truck->SetDriveInput(0.0f, 0.0f);
			Truck->SetServiceBrake(1.0f);
			AdvanceTestCase(false, TEXT("{\"reason\":\"no wheel slip within 5s on the mound\"}"));
		}
		return;
	}
	case 3: // suspension_settle: after the drive_brake stop, Z oscillation must decay
	{
		if (!Truck)
		{
			AdvanceTestCase(false, TEXT("{\"error\":\"truck missing\"}"));
			return;
		}
		Truck->SetDriveInput(0.0f, 0.0f);
		Truck->SetServiceBrake(1.0f);
		const float ZCm = Truck->GetActorLocation().Z;
		Test.MinZCm = Test.SettleWindowS > 0.0f ? FMath::Min(Test.MinZCm, ZCm) : ZCm;
		Test.MaxZCm = Test.SettleWindowS > 0.0f ? FMath::Max(Test.MaxZCm, ZCm) : ZCm;
		Test.SettleWindowS += DeltaSeconds;
		if (Test.SettleWindowS >= 2.5f)
		{
			const float RangeCm = Test.MaxZCm - Test.MinZCm;
			const bool bPass = RangeCm <= SettleMaxCm;
			Truck->SetServiceBrake(0.0f);
			AdvanceTestCase(bPass, FString::Printf(TEXT("{\"z_range_cm\":%.2f,\"limit_cm\":%.2f}"), RangeCm, SettleMaxCm));
		}
		return;
	}
	case 5: // auto_haul_cycle: drive excavator to the mound, park the truck in
			// reach, dig + load, dump into the truck, haul to the depot, unload.
	{
		if (Test.CaseElapsedS > HaulTimeoutS)
		{
			if (Excavator)
			{
				Excavator->SetDiggingActive(false);
				Excavator->SetDumpingActive(false);
			}
			if (Truck)
			{
				Truck->StopUnloading();
				Truck->SetDriveInput(0.0f, 0.0f);
			}
			AdvanceTestCase(false, FString::Printf(TEXT("{\"reason\":\"timeout\",\"delivered_kg\":%.0f,\"marker\":%.0f}"),
				Worksite ? Worksite->GetDeliveredMassKg() - DeliveredAtStartKg : 0.0, Test.DriveDistanceM));
			return;
		}
		if (!Truck || !Excavator || !Worksite->GetDigSite())
		{
			AdvanceTestCase(false, TEXT("{\"error\":\"vehicles missing\"}"));
			return;
		}

		const FVector PileCenter = Worksite->GetDigSite()->GetActorLocation();
		FVector ToExcavator = Excavator->GetActorLocation() - PileCenter;
		ToExcavator.Z = 0.0f;
		FVector ApproachDir = ToExcavator.GetSafeNormal2D();
		if (ApproachDir.IsNearlyZero())
		{
			ApproachDir = FVector(1.0, 0.0, 0.0);
		}
		const FVector SideDir = FVector(ApproachDir.Y, -ApproachDir.X, 0.0f);

		// Phase A (marker < 1): park the truck on flat ground outside the loose
		// footprint, offset beside the dig axis so neither vehicle blocks the other.
		if (Test.DriveDistanceM < 1.0f)
		{
			const FVector TruckTarget = PileCenter + ApproachDir * 1300.0f + SideDir * 400.0f;
			const EStepResult Step = DriveVehicleToward(Truck, TruckTarget, 3.0f, 0.6f, DeltaSeconds);
			if (Step == EStepResult::Completed)
			{
				// Park AND hold: without the brake the truck creeps during the
				// long dig phase and its heading drifts away from the depot leg.
				Truck->SetParkingBrake(true);
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks haul_cycle: truck parked behind the dig spot"));
				Test.DriveDistanceM = 1.0f;
			}
			return;
		}

		// Phase B (marker < 2): drive the excavator close enough that even a
		// curled arm keeps the bucket teeth inside the mound footprint.
		if (Test.DriveDistanceM < 2.0f)
		{
			const FVector ExcavatorTarget = PileCenter + ApproachDir * 700.0f - SideDir * 250.0f;
			const EStepResult Step = DriveVehicleToward(Excavator, ExcavatorTarget, 3.0f, 0.8f, DeltaSeconds);
			if (Step == EStepResult::Completed)
			{
				// Park the excavator for the dig: without the brake it creeps on
				// the loose slope under its own reaction forces.
				Excavator->SetParkingBrake(true);
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks haul_cycle: excavator in dig position"));
				Test.DriveDistanceM = 2.0f;
				DigPhase = 0;
				DigPhaseElapsedS = 0.0f;
			}
			return;
		}

		// Phase C (marker 2.0-2.75): dig with the machine; on a loaded bucket
			// re-drive the excavator next to the parked truck (it drifts as the
				// mound deforms under it), then run the machine's swing-and-dump.
		if (Test.DriveDistanceM >= 2.0f && Test.DriveDistanceM < 2.5f)
		{
			const int32 PhaseBefore = DigPhase;
			const EStepResult DigResult = TickDigMachine(Worksite, DigPhase, SweepDirection, DigPhaseElapsedS, DeltaSeconds);
			if (DigPhase != PhaseBefore)
			{
				DigPhaseElapsedS = 0.0f;
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks haul_cycle: dig phase -> %d"), DigPhase);
			}
			else
			{
				DigPhaseElapsedS += DeltaSeconds;
			}
			if (DigResult == EStepResult::Failed)
			{
				Excavator->SetDiggingActive(false);
				Excavator->SetDumpingActive(false);
				AdvanceTestCase(false, FString::Printf(TEXT("{\"reason\":\"dig stalled phase %d\"}"), DigPhase));
				return;
			}
			if (DigResult == EStepResult::Completed && Excavator->BucketCargo->CurrentMassKg > 100.0f)
			{
				Excavator->SetDiggingActive(false);
				Excavator->ApplyActuatorCommands(0.0f, 0.5f, 0.0f, 0.0f); // raise the boom
				Test.DriveDistanceM = 2.5f; // reposition for dumping
			}
			return;
		}

		// Phase C2 (marker 2.5): re-drive the loaded excavator alongside the
			// parked truck so its tip reaches the bed.
		if (Test.DriveDistanceM >= 2.5f && Test.DriveDistanceM < 2.75f)
		{
			const FVector DumpStance = PileCenter + ApproachDir * 1300.0f + SideDir * 900.0f;
			const EStepResult Step = DriveVehicleToward(Excavator, DumpStance, 4.5f, 0.7f, DeltaSeconds);
			if (Step == EStepResult::Completed)
			{
				Excavator->SetParkingBrake(true);
				DigPhase = 3; // machine: swing to the truck and dump
				DigPhaseElapsedS = 0.0f;
				Test.DriveDistanceM = 2.75f;
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks haul_cycle: excavator at dump stance"));
			}
			return;
		}

		// Phase C3 (marker 2.75): machine swings and dumps into the truck.
		if (Test.DriveDistanceM >= 2.75f && Test.DriveDistanceM < 3.0f)
		{
			const int32 PhaseBefore = DigPhase;
			const EStepResult DigResult = TickDigMachine(Worksite, DigPhase, SweepDirection, DigPhaseElapsedS, DeltaSeconds);
			if (DigPhase != PhaseBefore)
			{
				DigPhaseElapsedS = 0.0f;
			}
			else
			{
				DigPhaseElapsedS += DeltaSeconds;
			}
			if (DigResult == EStepResult::Failed)
			{
				Excavator->SetDiggingActive(false);
				Excavator->SetDumpingActive(false);
				AdvanceTestCase(false, FString::Printf(TEXT("{\"reason\":\"dump stalled phase %d\"}"), DigPhase));
				return;
			}
			if (DigResult == EStepResult::Completed)
			{
				Excavator->SetDiggingActive(false);
				Excavator->SetDumpingActive(false);
				Excavator->ApplyActuatorCommands(0.0f, 0.5f, 0.0f, 0.0f); // raise the boom out of the way
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks haul_cycle: dig+load done, hauling to the depot"));
				Test.DriveDistanceM = 3.0f;
				DigPhase = 0;
				DigPhaseElapsedS = 0.0f;
			}
			return;
		}

		// Phase D (marker >= 3): haul the loaded truck to the depot and unload.
		if (Test.DriveDistanceM < 3.0f)
		{
			return;
		}
		AQuarryMaterialPile* Depot = Worksite->GetDeliveryPile();
		if (!Depot)
		{
			AdvanceTestCase(false, TEXT("{\"error\":\"depot missing\"}"));
			return;
		}
		// Yield (non-blocking, latched): the excavator just dumped next to the
		// depot pad and would block the truck's approach — drive it behind the
		// mound while the truck hauls; the latch keeps commands flowing until
		// the yield target is reached (a throttle must never latch open-loop).
		if (Excavator && (bExcavatorYielding
			|| FlatDistanceM(Excavator->GetActorLocation(), Depot->GetActorLocation()) < 8.0f))
		{
			if (!bExcavatorYielding)
			{
				bExcavatorYielding = true;
				Excavator->SetParkingBrake(false);
			}
			if (DriveVehicleToward(Excavator, PileCenter - ApproachDir * 300.0f, 8.0f, 0.8f, DeltaSeconds)
				== EStepResult::Completed)
			{
				bExcavatorYielding = false;
				Excavator->SetParkingBrake(true);
				Excavator->SetDriveInput(0.0f, 0.0f);
			}
		}
		if (HaulPhase == 0)
		{
			const FVector DepotTarget = Depot->GetActorLocation() + FVector(250.0, 0.0, 0.0);
			const EStepResult Step = DriveVehicleToward(Truck, DepotTarget, 5.0f, 0.9f, DeltaSeconds);
			if (Step == EStepResult::Completed)
			{
				Truck->ToggleUnloading();
				HaulPhase = 1;
				UE_LOG(LogTemp, Log, TEXT("QuarryWorks haul_cycle: unloading at the depot"));
			}
			return;
		}
		if (Truck->Cargo && Truck->Cargo->CurrentMassKg < 1.0f)
		{
			Truck->StopUnloading();
			const double Delta = Worksite->GetDeliveredMassKg() - DeliveredAtStartKg;
			AdvanceTestCase(Delta >= HaulMinKg,
				FString::Printf(TEXT("{\"delivered_kg\":%.0f,\"minimum_kg\":%.0f}"), Delta, HaulMinKg));
		}
		return;
	}
	default:
		AdvanceTestCase(false, TEXT("{\"reason\":\"unknown case\"}"));
	}
}
