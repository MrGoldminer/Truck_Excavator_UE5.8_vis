#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "QuarryVehicleMovementComponent.generated.h"

class UBoxComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EQuarryDriveType : uint8
{
	Wheeled,
	Tracked
};

USTRUCT()
struct FQuarryWheelState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector MountOffset = FVector::ZeroVector;

	UPROPERTY()
	float Radius = 50.0f;

	UPROPERTY()
	bool bDriven = true;

	UPROPERTY()
	bool bSteered = false;

	UPROPERTY()
	float SuspensionLength = 35.0f;

	UPROPERTY()
	float SpinAngle = 0.0f;

	UPROPERTY()
	FRotator BaseVisualRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TObjectPtr<USceneComponent> Visual = nullptr;

	// Force-driven simulation state, updated every physics tick. Internal forces
	// use UE force units (kg*cm/s^2); telemetry converts them to newtons.
	float SteerAngleRad = 0.0f;
	float AngularVelocityRadS = 0.0f;
	float LoadForceUE = 0.0f;
	float SlipRatio = 0.0f;
	float SlipAngleRad = 0.0f;
	float TireLongForceUE = 0.0f;
	float TireLatForceUE = 0.0f;
	float SuspensionCompression = 0.0f;
	float SuspensionVelocity = 0.0f;
	float ContactFriction = 0.8f;
	bool bInContact = false;
	FVector ContactPoint = FVector::ZeroVector;
	FVector ContactNormal = FVector::UpVector;
};

/**
 * CARLA-style force-driven vehicle model: raycast spring/damper suspension per
 * wheel, simplified Pacejka tire forces clamped by a friction circle, engine
 * torque curve with automatic gearbox, and differential skid steering for
 * tracked vehicles. Set bLegacyKinematic to fall back to the original
 * kinematic movement.
 */
UCLASS(ClassGroup=(Quarry), meta=(BlueprintSpawnableComponent))
class QUARRYWORKS_API UQuarryVehicleMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UQuarryVehicleMovementComponent();

	// Calibration runs lazily on the first physics tick so pbtxt overrides
	// applied in the owning actor's BeginPlay are already in effect.
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	EQuarryDriveType DriveType = EQuarryDriveType::Wheeled;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle", meta=(ClampMin="1.0"))
	float BaseMassKg = 12000.0f;

	/** Legacy full-throttle drive force in newtons; calibrates EnginePeakTorqueNm when that is <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float EngineForce = 80000.0f;

	/** Legacy full brake force in newtons; calibrates MaxBrakeTorqueNm when that is <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float BrakeForce = 120000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float MaxForwardSpeed = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float MaxReverseSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float MaxSteerAngle = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float SuspensionRestLength = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float SuspensionTravel = 45.0f;

	// --- Powertrain (wheeled drive only; tracked vehicles use hydraulic skid steering) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	TArray<float> ForwardGearRatios = { 3.2f, 2.1f, 1.45f, 1.0f, 0.82f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain", meta=(ClampMin="0.1"))
	float ReverseGearRatio = 3.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain", meta=(ClampMin="0.1"))
	float FinalDriveRatio = 3.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain", meta=(ClampMin="0.1", ClampMax="1.0"))
	float DrivelineEfficiency = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float EngineIdleRPM = 750.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float EngineMaxRPM = 3200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float EnginePeakTorqueRPM = 1600.0f;

	/** Peak engine torque in N*m. Calibrated from EngineForce when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float EnginePeakTorqueNm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float EngineBrakeTorqueNm = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float ShiftUpRPM = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float ShiftDownRPM = 1250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain")
	float ShiftDuration = 0.3f;

	/** Combined wheel + driveline inertia per wheel in kg*m^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Powertrain", meta=(ClampMin="0.5"))
	float WheelInertiaKgM2 = 22.0f;

	// --- Suspension (UE units: force in kg*cm/s^2, length in cm) ---

	/** Spring rate per cm of compression; auto-calibrated from vehicle mass when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Suspension")
	float SuspensionStiffness = 0.0f;

	/** Damping rate per cm/s of compression velocity; auto-calibrated when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Suspension")
	float SuspensionDamping = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Suspension", meta=(ClampMin="0.0", ClampMax="1.5"))
	float SuspensionDampingRatio = 0.7f;

	/** Suspension force clamp; auto-calibrated when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Suspension")
	float SuspensionMaxForce = 0.0f;

	/** Anti-roll bar rate per cm of left/right compression difference; 0 disables, negative auto-calibrates to ~30% of spring rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Suspension")
	float AntiRollStiffness = 0.0f;

	// --- Tire model ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire", meta=(ClampMin="0.1"))
	float TireFrictionScale = 1.0f;

	/** Pacejka stiffness factor B. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire", meta=(ClampMin="1.0"))
	float TireSlipStiffness = 9.0f;

	/** Pacejka shape factor C. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire", meta=(ClampMin="1.0", ClampMax="2.5"))
	float TireShapeFactor = 1.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire", meta=(ClampMin="0.0"))
	float RollingResistance = 0.018f;

	/** Friction of solid ground surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire", meta=(ClampMin="0.05", ClampMax="2.0"))
	float GroundFriction = 0.9f;

	/** Friction of the loose sand/ore terrain (AQuarryDigSite). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire", meta=(ClampMin="0.05", ClampMax="2.0"))
	float LooseMaterialFriction = 0.4f;

	/** Speed-squared aerodynamic drag coefficient in UE force units per (cm/s)^2; auto-calibrated when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire")
	float AirDragCoefficient = 0.0f;

	// --- Steering (wheeled drive) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering", meta=(ClampMin="10.0"))
	float SteeringRateDegPerSecond = 130.0f;

	/** Forward speed in cm/s at which steering authority fades to MinSteerFactor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering")
	float SteeringFadeSpeed = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Steering", meta=(ClampMin="0.05", ClampMax="1.0"))
	float MinSteerFactor = 0.22f;

	// --- Tracked drive (skid steering) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracked", meta=(ClampMin="5.0"))
	float TrackYawRateDegPerSecond = 45.0f;

	/** Longitudinal track force per cm/s of speed error (UE force units); auto-calibrated when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracked")
	float TrackGripStiffness = 0.0f;

	/** Lateral track restraint force per cm/s of lateral slip (UE force units); auto-calibrated when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracked")
	float TrackLateralStiffness = 0.0f;

	/** Yaw damping torque per rad/s opposing track rotation (UE torque units); auto-calibrated when <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracked")
	float TrackTurnResistance = 0.0f;

	/** Fall back to the original kinematic movement model. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	bool bLegacyKinematic = false;

	void ConfigureChassis(UBoxComponent* InChassis);
	void AddWheel(const FVector& MountOffset, float Radius, bool bDriven, bool bSteered, USceneComponent* Visual);
	/** Removes all wheels (used when mounts are rebuilt from real mesh bones). */
	void ClearWheels() { Wheels.Empty(); }

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetDriveInput(float Throttle, float Steering);

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetServiceBrake(float Brake);

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetParkingBrake(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetPayloadMassKg(float MassKg);

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetPayloadCenterOfMass(const FVector& LocalOffset);

	UFUNCTION(BlueprintPure, Category="Quarry|Driving")
	float GetForwardSpeed() const;

	UFUNCTION(BlueprintPure, Category="Quarry|Driving")
	float GetSpeedKmh() const;

	UFUNCTION(BlueprintPure, Category="Quarry|Driving")
	bool IsParkingBrakeEnabled() const { return bParkingBrake; }

	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	int32 GetNumWheels() const { return Wheels.Num(); }

	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	int32 GetGroundedWheelCount() const { return GroundedWheelCount; }

	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	float GetLastDriveForce() const { return LastDriveForce; }

	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	float GetEngineRPM() const { return EngineRPM; }

	/** -1 = reverse, 0 = neutral, 1..N = forward gears. */
	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	int32 GetCurrentGear() const { return CurrentGear; }

	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	FString GetGearLabel() const;

	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	bool IsAnyWheelSlipping() const;

	UFUNCTION(BlueprintPure, Category="Quarry|Diagnostics")
	float GetMaxWheelSlipRatio() const;

	const TArray<FQuarryWheelState>& GetWheelStates() const { return Wheels; }

private:
	void ApplyMassProperties();
	void CalibrateFromDefaults();
	void TickLegacyKinematic(float DeltaTime);
	void TickWheeledDrive(float DeltaTime);
	void TickTrackedDrive(float DeltaTime);
	void UpdatePowertrain(float DeltaTime);
	void UpdateSuspension(FQuarryWheelState& Wheel, float DeltaTime);
	void ApplyAntiRollForces();
	void UpdateTireForces(FQuarryWheelState& Wheel, float DeltaTime, float DriveTorqueNmPerWheel);
	float GetBrakeTorqueNm() const;
	float ResolveContactFriction(const FHitResult& Hit) const;
	float GetMeanDrivenWheelRadiusM() const;
	void SimulateWheel(FQuarryWheelState& Wheel, float DeltaTime);
	void UpdateWheelVisual(FQuarryWheelState& Wheel, float DeltaTime);

	UPROPERTY()
	TObjectPtr<UBoxComponent> Chassis;

	UPROPERTY()
	TArray<FQuarryWheelState> Wheels;

	float ThrottleCommand = 0.0f;
	float SteeringCommand = 0.0f;
	float BrakeCommand = 0.0f;
	float PayloadMassKg = 0.0f;
	FVector PayloadCenterOfMass = FVector::ZeroVector;
	bool bParkingBrake = false;
	float GroundHeightSum = 0.0f;
	int32 GroundedWheelCount = 0;
	float LastDriveForce = 0.0f;

	// Powertrain state.
	float EngineRPM = 750.0f;
	int32 CurrentGear = 1;
	float ShiftTimer = 0.0f;
	float WheelDriveTorqueNm = 0.0f;

	// Steering state (wheeled drive).
	float CurrentSteerAngleDeg = 0.0f;

	bool bCalibrated = false;
};
