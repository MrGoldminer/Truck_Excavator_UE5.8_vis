#pragma once

#include "CoreMinimal.h"

class UQuarryWorksiteSubsystem;

enum class EQuarryTaskType : uint8
{
	DriveTo,
	Dig,
	Haul,
	TestSuite
};

/** Task dispatched by the MQTT management plane and executed on the game thread. */
struct FQuarryTaskCommand
{
	FString TaskId;
	EQuarryTaskType Type = EQuarryTaskType::DriveTo;
	FString VehicleId;
	double TargetX_m = 0.0;   // CARLA-style meters
	double TargetY_m = 0.0;
	float Tolerance_m = 2.5f;
};

/**
 * Executes dispatched tasks on the force-driven vehicle model: drive-to with a
 * heading-error steering controller, scripted excavator dig cycles, truck haul
 * cycles, and a synchronous self-test suite whose cases are published to MQTT
 * as they complete. Runs entirely on the game thread from the subsystem Tick.
 */
class QUARRYWORKS_API FQuarryTaskExecutor
{
public:
	void Initialize(UQuarryWorksiteSubsystem* InWorksite);

	/** Queues a task; returns a queue-stable id for reporting. */
	bool EnqueueTask(const FQuarryTaskCommand& Task);

	/** Drives the active task; called from the subsystem tick. */
	void Tick(float DeltaSeconds);

	bool IsTaskActive() const { return !bIdle || !PendingTasks.IsEmpty(); }
	int32 GetPendingCount() const { return PendingTasks.Num(); }

private:
	// --- task implementations ---
	void TickDriveTo(float DeltaSeconds);
	void TickDig(float DeltaSeconds);
	void TickHaul(float DeltaSeconds);
	void TickTestSuite(float DeltaSeconds);

	void StartTask(const FQuarryTaskCommand& Task);
	void FinishTask(const FString& Result, const FString& Reason);
	void PublishTaskResult(const FString& Result, const FString& Reason);
	void PublishTestCase(const FString& CaseName, bool bPass, const FString& MetricsJson, double DurationS);
	FString TestCaseName(int32 Index) const;

	// --- test suite ---
	struct FTestState
	{
		int32 CaseIndex = 0;
		int32 NumPassed = 0;
		int32 NumTotal = 0;
		float CaseElapsedS = 0.0f;
		float PhaseElapsedS = 0.0f; // per-phase budget inside a case
		float TotalElapsedS = 0.0f;
		float SettleWindowS = 0.0f;
		float MaxZRangeCm = 0.0f;
		float MinZCm = 0.0f;
		float MaxZCm = 0.0f;
		FVector StartLocationCm = FVector::ZeroVector;
		float DriveDistanceM = 0.0f;
		bool bBrakePhase = false;
		bool bSlipSeen = false;
		FVector LastLocationCm = FVector::ZeroVector;
	};
	void BeginTestCase(int32 Index);
	void TickTestCase(float DeltaSeconds);
	void AdvanceTestCase(bool bPass, const FString& MetricsJson);

	UQuarryWorksiteSubsystem* Worksite = nullptr;
	FQuarryTaskCommand Current;
	TArray<FQuarryTaskCommand> PendingTasks;
	bool bIdle = true;
	float TaskElapsedS = 0.0f;

	// Drive-to controller state.
	float LastDistanceM = TNumericLimits<float>::Max();

	// Dig phase state: 0 = approach/slew, 1 = reach, 2 = sweep, 3 = load truck.
	int32 DigPhase = 0;
	float DigPhaseElapsedS = 0.0f; // per-phase budget (reset on transitions)
	float SweepDirection = 1.0f;

	// Haul phase state: 0 = drive to depot, 1 = unload.
	int32 HaulPhase = 0;
	bool bWheeledReversing = false; // U-turn hysteresis for the truck controller
	bool bExcavatorYielding = false; // latched post-dump clearance drive
	float ReverseStraightTimerS = 0.0f; // straight-back phase of the three-point turn
	double DeliveredAtStartKg = 0.0;

	// Test-suite state.
	FTestState Test;
	TArray<FString> TestResults;
};
