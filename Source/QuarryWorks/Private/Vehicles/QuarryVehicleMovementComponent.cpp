#include "Vehicles/QuarryVehicleMovementComponent.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "QuarryCollisionChannels.h"
#include "Terrain/QuarryDigSite.h"

namespace
{
	// 1 N = 100 kg*cm/s^2 (UE force units). Internal forces use UE units.
	constexpr float UeForcePerNewton = 100.0f;
	constexpr float RadiansToRPM = 60.0f / (2.0f * PI);
	// Slip-ratio denominator floor in cm/s; keeps low-speed ratios finite.
	constexpr float MinSlipSpeedCmS = 25.0f;
}

UQuarryVehicleMovementComponent::UQuarryVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UQuarryVehicleMovementComponent::ConfigureChassis(UBoxComponent* InChassis)
{
	Chassis = InChassis;
	SetUpdatedComponent(InChassis);
	ApplyMassProperties();
}

void UQuarryVehicleMovementComponent::AddWheel(const FVector& MountOffset, float Radius, bool bDriven, bool bSteered, USceneComponent* Visual)
{
	FQuarryWheelState Wheel;
	Wheel.MountOffset = MountOffset;
	Wheel.Radius = Radius;
	Wheel.bDriven = bDriven;
	Wheel.bSteered = bSteered;
	Wheel.Visual = Visual;
	Wheel.BaseVisualRotation = Visual ? Visual->GetRelativeRotation() : FRotator::ZeroRotator;
	Wheel.SuspensionLength = SuspensionRestLength * 0.7f;
	Wheels.Add(Wheel);
}

void UQuarryVehicleMovementComponent::SetDriveInput(float Throttle, float Steering)
{
	ThrottleCommand = FMath::Clamp(Throttle, -1.0f, 1.0f);
	SteeringCommand = FMath::Clamp(Steering, -1.0f, 1.0f);
}

void UQuarryVehicleMovementComponent::SetServiceBrake(float Brake)
{
	BrakeCommand = FMath::Clamp(Brake, 0.0f, 1.0f);
}

void UQuarryVehicleMovementComponent::SetParkingBrake(bool bEnabled)
{
	bParkingBrake = bEnabled;
}

void UQuarryVehicleMovementComponent::SetPayloadMassKg(float MassKg)
{
	PayloadMassKg = FMath::Max(0.0f, MassKg);
	ApplyMassProperties();
}

void UQuarryVehicleMovementComponent::SetPayloadCenterOfMass(const FVector& LocalOffset)
{
	PayloadCenterOfMass = LocalOffset;
	ApplyMassProperties();
}

float UQuarryVehicleMovementComponent::GetForwardSpeed() const
{
	return Chassis ? FVector::DotProduct(Chassis->GetPhysicsLinearVelocity(), Chassis->GetForwardVector()) : 0.0f;
}

float UQuarryVehicleMovementComponent::GetSpeedKmh() const
{
	return Chassis ? Chassis->GetPhysicsLinearVelocity().Size() * 0.036f : 0.0f;
}

FString UQuarryVehicleMovementComponent::GetGearLabel() const
{
	if (CurrentGear < 0) return TEXT("R");
	if (CurrentGear == 0) return TEXT("N");
	return FString::FromInt(CurrentGear);
}

bool UQuarryVehicleMovementComponent::IsAnyWheelSlipping() const
{
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bInContact && FMath::Abs(Wheel.SlipRatio) > 0.2f)
		{
			return true;
		}
	}
	return false;
}

float UQuarryVehicleMovementComponent::GetMaxWheelSlipRatio() const
{
	float MaxSlip = 0.0f;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bInContact)
		{
			MaxSlip = FMath::Max(MaxSlip, FMath::Abs(Wheel.SlipRatio));
		}
	}
	return MaxSlip;
}

void UQuarryVehicleMovementComponent::ApplyMassProperties()
{
	if (!Chassis || (GetOwner() && GetOwner()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		return;
	}

	const float TotalMass = FMath::Max(1.0f, BaseMassKg + PayloadMassKg);
	Chassis->SetMassOverrideInKg(NAME_None, TotalMass, true);
	const float PayloadRatio = PayloadMassKg / TotalMass;
	Chassis->SetCenterOfMass(PayloadCenterOfMass * PayloadRatio);
}

float UQuarryVehicleMovementComponent::GetMeanDrivenWheelRadiusM() const
{
	float Sum = 0.0f;
	int32 Count = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bDriven)
		{
			Sum += Wheel.Radius;
			++Count;
		}
	}
	if (Count == 0)
	{
		for (const FQuarryWheelState& Wheel : Wheels)
		{
			Sum += Wheel.Radius;
			++Count;
		}
	}
	return (Count > 0 ? Sum / Count : 50.0f) * 0.01f;
}

void UQuarryVehicleMovementComponent::CalibrateFromDefaults()
{
	if (bCalibrated)
	{
		return;
	}
	bCalibrated = true;

	const int32 NumWheels = FMath::Max(1, Wheels.Num());
	const float StaticLoadUE = BaseMassKg * 981.0f / NumWheels;

	if (SuspensionStiffness <= 0.0f)
	{
		// Aim for ~45% of travel used by the unloaded chassis weight.
		const float TargetCompression = FMath::Max(5.0f, 0.45f * SuspensionTravel);
		SuspensionStiffness = StaticLoadUE / TargetCompression;
		SuspensionMaxForce = FMath::Max(SuspensionMaxForce, 6.0f * StaticLoadUE);
	}
	if (SuspensionDamping <= 0.0f)
	{
		SuspensionDamping = 2.0f * SuspensionDampingRatio
			* FMath::Sqrt(SuspensionStiffness * BaseMassKg / NumWheels);
	}
	if (AntiRollStiffness < 0.0f)
	{
		AntiRollStiffness = 0.3f * SuspensionStiffness;
	}

	if (AirDragCoefficient <= 0.0f)
	{
		// Drag reaches ~30% of engine force at MaxForwardSpeed.
		AirDragCoefficient = 0.3f * EngineForce * UeForcePerNewton / FMath::Max(1.0f, MaxForwardSpeed * MaxForwardSpeed);
	}

	if (DriveType == EQuarryDriveType::Tracked)
	{
		const float TotalLoadUE = StaticLoadUE * NumWheels;
		if (TrackGripStiffness <= 0.0f)
		{
			TrackGripStiffness = 0.7f * TireFrictionScale * GroundFriction * TotalLoadUE / 150.0f;
		}
		if (TrackLateralStiffness <= 0.0f)
		{
			TrackLateralStiffness = 2.0f * TrackGripStiffness;
		}
		if (TrackTurnResistance <= 0.0f)
		{
			float MinX = TNumericLimits<float>::Max();
			float MaxX = -TNumericLimits<float>::Max();
			float MeanRadiusCm = 0.0f;
			for (const FQuarryWheelState& Wheel : Wheels)
			{
				MinX = FMath::Min(MinX, Wheel.MountOffset.X);
				MaxX = FMath::Max(MaxX, Wheel.MountOffset.X);
				MeanRadiusCm += Wheel.Radius;
			}
			MeanRadiusCm = Wheels.Num() > 0 ? MeanRadiusCm / Wheels.Num() : 50.0f;
			const float ContactLengthCm = FMath::Max(100.0f, MaxX - MinX + 2.0f * MeanRadiusCm);
			TrackTurnResistance = TotalLoadUE * ContactLengthCm / 60.0f;
		}
	}
}

void UQuarryVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Chassis || !Chassis->IsSimulatingPhysics() || DeltaTime <= 0.0f)
	{
		return;
	}

	if (!bCalibrated)
	{
		CalibrateFromDefaults();
	}

	if (bLegacyKinematic)
	{
		TickLegacyKinematic(DeltaTime);
		return;
	}

	if (DriveType == EQuarryDriveType::Tracked)
	{
		TickTrackedDrive(DeltaTime);
	}
	else
	{
		TickWheeledDrive(DeltaTime);
	}

	for (FQuarryWheelState& Wheel : Wheels)
	{
		UpdateWheelVisual(Wheel, DeltaTime);
	}
}

void UQuarryVehicleMovementComponent::TickWheeledDrive(float DeltaTime)
{
	GroundedWheelCount = 0;
	LastDriveForce = 0.0f;

	// Speed-sensitive steering with rate limiting; yaw emerges from tire forces.
	const float SpeedAbs = FMath::Abs(GetForwardSpeed());
	const float SteerFactor = FMath::Lerp(1.0f, MinSteerFactor,
		FMath::Clamp(SpeedAbs / FMath::Max(1.0f, SteeringFadeSpeed), 0.0f, 1.0f));
	const float TargetSteerDeg = SteeringCommand * MaxSteerAngle * SteerFactor;
	CurrentSteerAngleDeg = FMath::FInterpConstantTo(CurrentSteerAngleDeg, TargetSteerDeg, DeltaTime, SteeringRateDegPerSecond);
	const float SteerRad = FMath::DegreesToRadians(CurrentSteerAngleDeg);

	for (FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bSteered)
		{
			Wheel.SteerAngleRad = SteerRad;
		}
		UpdateSuspension(Wheel, DeltaTime);
	}
	ApplyAntiRollForces();
	UpdatePowertrain(DeltaTime);

	for (FQuarryWheelState& Wheel : Wheels)
	{
		UpdateTireForces(Wheel, DeltaTime, WheelDriveTorqueNm);
	}

	// Aerodynamic drag proportional to speed squared.
	const FVector ChassisVelocity = Chassis->GetPhysicsLinearVelocity();
	const float SpeedSquared = ChassisVelocity.SizeSquared();
	if (AirDragCoefficient > 0.0f && SpeedSquared > 1.0f)
	{
		Chassis->AddForce(ChassisVelocity.GetSafeNormal() * (-AirDragCoefficient * SpeedSquared));
	}
}

void UQuarryVehicleMovementComponent::TickTrackedDrive(float DeltaTime)
{
	GroundedWheelCount = 0;
	LastDriveForce = 0.0f;

	for (FQuarryWheelState& Wheel : Wheels)
	{
		UpdateSuspension(Wheel, DeltaTime);
	}
	ApplyAntiRollForces();

	// Split wheels into left/right track groups.
	float LeftLoadUE = 0.0f;
	float RightLoadUE = 0.0f;
	float LeftFrictionSum = 0.0f;
	float RightFrictionSum = 0.0f;
	FVector LeftContactSum = FVector::ZeroVector;
	FVector RightContactSum = FVector::ZeroVector;
	int32 LeftContactCount = 0;
	int32 RightContactCount = 0;
	float LeftMeanY = -150.0f;
	float RightMeanY = 150.0f;
	int32 LeftCount = 0;
	int32 RightCount = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.MountOffset.Y < 0.0f)
		{
			LeftLoadUE += Wheel.LoadForceUE;
			LeftMeanY += Wheel.MountOffset.Y;
			++LeftCount;
			if (Wheel.bInContact)
			{
				LeftContactSum += Wheel.ContactPoint;
				LeftFrictionSum += Wheel.ContactFriction;
				++LeftContactCount;
			}
		}
		else
		{
			RightLoadUE += Wheel.LoadForceUE;
			RightMeanY += Wheel.MountOffset.Y;
			++RightCount;
			if (Wheel.bInContact)
			{
				RightContactSum += Wheel.ContactPoint;
				RightFrictionSum += Wheel.ContactFriction;
				++RightContactCount;
			}
		}
	}
	if (LeftCount > 0)
	{
		LeftMeanY = LeftMeanY / LeftCount;
	}
	if (RightCount > 0)
	{
		RightMeanY = RightMeanY / RightCount;
	}
	const float TrackHalfWidthCm = FMath::Max(50.0f, (FMath::Abs(LeftMeanY) + FMath::Abs(RightMeanY)) * 0.5f);

	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	const FVector LeftPoint = LeftContactCount > 0 ? LeftContactSum / LeftContactCount
		: ChassisTransform.TransformPosition(FVector(0.0f, LeftMeanY, -SuspensionRestLength));
	const FVector RightPoint = RightContactCount > 0 ? RightContactSum / RightContactCount
		: ChassisTransform.TransformPosition(FVector(0.0f, RightMeanY, -SuspensionRestLength));

	const FVector Forward = Chassis->GetForwardVector();
	const FVector Up = Chassis->GetUpVector();
	const FVector Lateral = FVector::CrossProduct(Up, Forward);

	const float EffectiveBrake = FMath::Max(BrakeCommand, bParkingBrake ? 1.0f : 0.0f);
	float SpeedCmd = ThrottleCommand * (ThrottleCommand >= 0.0f ? MaxForwardSpeed : MaxReverseSpeed);
	if (EffectiveBrake > 0.01f)
	{
		SpeedCmd *= FMath::Max(0.0f, 1.0f - EffectiveBrake);
	}
	// Positive steering commands a positive (CCW) yaw rate: left track slows,
	// right track speeds up. The controller's heading-error convention expects
	// this, so the sign here defines the excavator's A/D steering direction.
	const float YawCmd = -FMath::DegreesToRadians(TrackYawRateDegPerSecond) * SteeringCommand
		* FMath::Max(0.0f, 1.0f - EffectiveBrake);

	// Hydraulic skid steering: each track follows a commanded ground speed.
	const float LeftTarget = SpeedCmd - YawCmd * TrackHalfWidthCm;
	const float RightTarget = SpeedCmd + YawCmd * TrackHalfWidthCm;

	const FVector LeftVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(LeftPoint);
	const FVector RightVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(RightPoint);
	const float LeftVLong = FVector::DotProduct(LeftVelocity, Forward);
	const float LeftVLat = FVector::DotProduct(LeftVelocity, Lateral);
	const float RightVLong = FVector::DotProduct(RightVelocity, Forward);
	const float RightVLat = FVector::DotProduct(RightVelocity, Lateral);

	const float LeftMu = LeftContactCount > 0 ? (LeftFrictionSum / LeftContactCount) * TireFrictionScale : GroundFriction * TireFrictionScale;
	const float RightMu = RightContactCount > 0 ? (RightFrictionSum / RightContactCount) * TireFrictionScale : GroundFriction * TireFrictionScale;
	const float LeftMaxForceUE = LeftMu * LeftLoadUE;
	const float RightMaxForceUE = RightMu * RightLoadUE;

	float LeftLongForceUE = 0.0f;
	float LeftLatForceUE = 0.0f;
	float RightLongForceUE = 0.0f;
	float RightLatForceUE = 0.0f;
	if (LeftLoadUE > KINDA_SMALL_NUMBER)
	{
		LeftLongForceUE = FMath::Clamp(TrackGripStiffness * (LeftTarget - LeftVLong), -LeftMaxForceUE, LeftMaxForceUE);
		LeftLatForceUE = FMath::Clamp(-TrackLateralStiffness * LeftVLat, -LeftMaxForceUE, LeftMaxForceUE);
		Chassis->AddForceAtLocation(Forward * LeftLongForceUE + Lateral * LeftLatForceUE, LeftPoint);
	}
	if (RightLoadUE > KINDA_SMALL_NUMBER)
	{
		RightLongForceUE = FMath::Clamp(TrackGripStiffness * (RightTarget - RightVLong), -RightMaxForceUE, RightMaxForceUE);
		RightLatForceUE = FMath::Clamp(-TrackLateralStiffness * RightVLat, -RightMaxForceUE, RightMaxForceUE);
		Chassis->AddForceAtLocation(Forward * RightLongForceUE + Lateral * RightLatForceUE, RightPoint);
	}

	// Soil resistance opposing the turn keeps skid steering from overshooting.
	const float YawRate = Chassis->GetPhysicsAngularVelocityInRadians().Z;
	if (TrackTurnResistance > 0.0f)
	{
		Chassis->AddTorqueInRadians(FVector(0.0f, 0.0f, -TrackTurnResistance * YawRate));
	}

	// Telemetry-friendly per-wheel state: report the track-level slip.
	for (FQuarryWheelState& Wheel : Wheels)
	{
		const bool bLeft = Wheel.MountOffset.Y < 0.0f;
		const float Target = bLeft ? LeftTarget : RightTarget;
		const float Actual = bLeft ? LeftVLong : RightVLong;
		const float LateralSpeed = bLeft ? LeftVLat : RightVLat;
		Wheel.SlipRatio = (Target - Actual) / FMath::Max(FMath::Abs(Actual), 150.0f);
		Wheel.SlipAngleRad = FMath::Atan2(LateralSpeed, FMath::Abs(Actual) + 5.0f);
		Wheel.TireLongForceUE = bLeft ? LeftLongForceUE : RightLongForceUE;
		Wheel.TireLatForceUE = bLeft ? LeftLatForceUE : RightLatForceUE;
		Wheel.AngularVelocityRadS = Actual / FMath::Max(1.0f, Wheel.Radius);
	}

	LastDriveForce = (LeftLongForceUE + RightLongForceUE) / UeForcePerNewton;

	// Hydraulic pump RPM follows demand; gear reflects travel direction.
	EngineRPM = FMath::FInterpTo(EngineRPM,
		EngineIdleRPM + FMath::Abs(ThrottleCommand) * FMath::Max(0.0f, EngineMaxRPM - EngineIdleRPM), DeltaTime, 4.0f);
	if (FMath::Abs(SpeedCmd) < 1.0f)
	{
		CurrentGear = 0;
	}
	else
	{
		CurrentGear = SpeedCmd > 0.0f ? 1 : -1;
	}
}

void UQuarryVehicleMovementComponent::UpdatePowertrain(float DeltaTime)
{
	const int32 NumForwardGears = FMath::Max(1, ForwardGearRatios.Num());
	const float ForwardSpeed = GetForwardSpeed();

	// Direction selection: allow gear changes only near standstill.
	if (ThrottleCommand < -0.02f && CurrentGear > 0 && ForwardSpeed < 30.0f)
	{
		CurrentGear = -1;
		ShiftTimer = ShiftDuration;
	}
	else if (ThrottleCommand > 0.02f && CurrentGear < 0 && ForwardSpeed > -30.0f)
	{
		CurrentGear = 1;
		ShiftTimer = ShiftDuration;
	}
	if (CurrentGear == 0)
	{
		CurrentGear = 1;
	}

	float MeanDrivenOmega = 0.0f;
	int32 DrivenCount = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bDriven)
		{
			MeanDrivenOmega += Wheel.AngularVelocityRadS;
			++DrivenCount;
		}
	}
	if (DrivenCount == 0)
	{
		DrivenCount = 1;
	}
	MeanDrivenOmega /= DrivenCount;

	auto GearRatioOf = [this, NumForwardGears](int32 Gear) -> float
	{
		if (Gear < 0)
		{
			return ReverseGearRatio;
		}
		const int32 Index = FMath::Clamp(Gear, 1, NumForwardGears) - 1;
		return ForwardGearRatios.Num() > 0 ? ForwardGearRatios[Index] : 1.0f;
	};

	const float GearRatio = GearRatioOf(CurrentGear);
	const float TotalRatio = GearRatio * FinalDriveRatio;

	// Engine speed follows the driven wheels through the current gear.
	const float TargetRPM = FMath::Abs(MeanDrivenOmega) * TotalRatio * RadiansToRPM;
	EngineRPM = FMath::FInterpTo(EngineRPM,
		FMath::Clamp(TargetRPM, EngineIdleRPM, EngineMaxRPM), DeltaTime, 6.0f);

	// Automatic shifting (torque interrupted during shifts).
	if (ShiftTimer > 0.0f)
	{
		ShiftTimer -= DeltaTime;
	}
	else if (CurrentGear > 0 && NumForwardGears > 1)
	{
		if (EngineRPM > ShiftUpRPM && CurrentGear < NumForwardGears && FMath::Abs(ThrottleCommand) > 0.1f)
		{
			++CurrentGear;
			ShiftTimer = ShiftDuration;
		}
		else if (EngineRPM < ShiftDownRPM && CurrentGear > 1)
		{
			--CurrentGear;
			ShiftTimer = ShiftDuration * 0.5f;
		}
	}

	// Piecewise-linear torque curve: 0.55 at idle, 1.0 at peak RPM, 0.72 at redline.
	float TorqueShape = 1.0f;
	if (EngineRPM <= EnginePeakTorqueRPM)
	{
		TorqueShape = FMath::Lerp(0.55f, 1.0f, FMath::Clamp(
			(EngineRPM - EngineIdleRPM) / FMath::Max(1.0f, EnginePeakTorqueRPM - EngineIdleRPM), 0.0f, 1.0f));
	}
	else
	{
		TorqueShape = FMath::Lerp(1.0f, 0.72f, FMath::Clamp(
			(EngineRPM - EnginePeakTorqueRPM) / FMath::Max(1.0f, EngineMaxRPM - EnginePeakTorqueRPM), 0.0f, 1.0f));
	}

	// Peak torque is derived from the current EngineForce unless set explicitly,
	// matching the legacy EngineForce as first-gear wheel force at full throttle.
	const float PeakTorqueNm = EnginePeakTorqueNm > 0.0f ? EnginePeakTorqueNm
		: EngineForce * GetMeanDrivenWheelRadiusM()
			/ FMath::Max(0.1f, GearRatioOf(1) * FinalDriveRatio * DrivelineEfficiency);
	float EngineTorqueNm = PeakTorqueNm * TorqueShape * ThrottleCommand;
	if (FMath::Abs(ThrottleCommand) < 0.05f && FMath::Abs(ForwardSpeed) > 20.0f)
	{
		EngineTorqueNm = -EngineBrakeTorqueNm * FMath::Clamp(EngineRPM / EngineMaxRPM, 0.0f, 1.0f)
			* FMath::Sign(ForwardSpeed);
	}

	// Soft speed limiter: no drive torque pushing beyond the speed limits.
	const float SpeedLimit = CurrentGear < 0 ? MaxReverseSpeed : MaxForwardSpeed;
	if (FMath::Abs(ForwardSpeed) > SpeedLimit && FMath::Sign(ForwardSpeed) == FMath::Sign(ThrottleCommand))
	{
		EngineTorqueNm = FMath::Min(EngineTorqueNm, 0.0f);
	}

	WheelDriveTorqueNm = ShiftTimer > 0.0f ? 0.0f
		: EngineTorqueNm * GearRatio * FinalDriveRatio * DrivelineEfficiency / DrivenCount;
}

void UQuarryVehicleMovementComponent::UpdateSuspension(FQuarryWheelState& Wheel, float DeltaTime)
{
	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	const FVector UpVector = Chassis->GetUpVector();
	const FVector MountLocation = ChassisTransform.TransformPosition(Wheel.MountOffset);
	const float TraceLength = SuspensionRestLength + SuspensionTravel + Wheel.Radius;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(QuarryWheelTrace), false, GetOwner());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, MountLocation, MountLocation - UpVector * TraceLength, QuarryCollision::Terrain, QueryParams);

	Wheel.bInContact = bHit;
	if (!bHit)
	{
		Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, SuspensionRestLength + SuspensionTravel, DeltaTime, 8.0f);
		Wheel.SuspensionVelocity = 0.0f;
		Wheel.SuspensionCompression = 0.0f;
		Wheel.LoadForceUE = 0.0f;
		return;
	}

	++GroundedWheelCount;
	GroundHeightSum += Hit.ImpactPoint.Z + Wheel.Radius - Wheel.MountOffset.Z;
	Wheel.ContactPoint = Hit.ImpactPoint;
	Wheel.ContactNormal = Hit.ImpactNormal;
	Wheel.ContactFriction = ResolveContactFriction(Hit);

	const float MaxLength = SuspensionRestLength + SuspensionTravel;
	const float CurrentLength = FMath::Clamp(Hit.Distance - Wheel.Radius, 0.0f, MaxLength);
	Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, CurrentLength, DeltaTime, 30.0f);

	const float PreviousCompression = Wheel.SuspensionCompression;
	Wheel.SuspensionCompression = MaxLength - CurrentLength;
	Wheel.SuspensionVelocity = (Wheel.SuspensionCompression - PreviousCompression) / FMath::Max(DeltaTime, 1e-4f);

	// Spring + damper along the chassis up axis, applied at the wheel mount.
	const float SuspensionForceUE = FMath::Clamp(
		SuspensionStiffness * Wheel.SuspensionCompression + SuspensionDamping * Wheel.SuspensionVelocity,
		0.0f, SuspensionMaxForce);
	Wheel.LoadForceUE = SuspensionForceUE;
	Chassis->AddForceAtLocation(UpVector * SuspensionForceUE, MountLocation);
}

void UQuarryVehicleMovementComponent::ApplyAntiRollForces()
{
	if (AntiRollStiffness <= 0.0f || Wheels.Num() < 2)
	{
		return;
	}

	TArray<bool> Paired;
	Paired.Init(false, Wheels.Num());
	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	const FVector Up = Chassis->GetUpVector();
	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		if (Paired[i])
		{
			continue;
		}
		for (int32 j = i + 1; j < Wheels.Num(); ++j)
		{
			if (Paired[j])
			{
				continue;
			}
			const FQuarryWheelState& A = Wheels[i];
			const FQuarryWheelState& B = Wheels[j];
			if (FMath::Abs(A.MountOffset.X - B.MountOffset.X) > 1.0f
				|| FMath::Sign(A.MountOffset.Y) == FMath::Sign(B.MountOffset.Y))
			{
				continue;
			}
			Paired[i] = true;
			Paired[j] = true;
			if (A.bInContact && B.bInContact)
			{
				const float ForceUE = AntiRollStiffness * (A.SuspensionCompression - B.SuspensionCompression);
				Chassis->AddForceAtLocation(Up * ForceUE, ChassisTransform.TransformPosition(A.MountOffset));
				Chassis->AddForceAtLocation(Up * -ForceUE, ChassisTransform.TransformPosition(B.MountOffset));
			}
			break;
		}
	}
}

float UQuarryVehicleMovementComponent::GetBrakeTorqueNm() const
{
	const float EffectiveBrake = FMath::Max(BrakeCommand, bParkingBrake ? 1.0f : 0.0f);
	if (EffectiveBrake <= 0.01f)
	{
		return 0.0f;
	}
	const int32 NumWheels = FMath::Max(1, Wheels.Num());
	// Match the legacy BrakeForce as total wheel force at full braking.
	return BrakeForce * GetMeanDrivenWheelRadiusM() / NumWheels * EffectiveBrake;
}

float UQuarryVehicleMovementComponent::ResolveContactFriction(const FHitResult& Hit) const
{
	if (const UPhysicalMaterial* PhysMaterial = Hit.PhysMaterial.Get())
	{
		// Level floors often ship with a low demo friction; a quarry surface is
		// compacted ground, so never fall below ~85% of the calibrated value.
		const float FloorFriction = FMath::Clamp(PhysMaterial->Friction, 0.05f, 2.0f);
		return FMath::Max(FloorFriction, GroundFriction * 0.85f);
	}
	if (const AActor* HitActor = Hit.GetActor())
	{
		if (HitActor->IsA<AQuarryDigSite>())
		{
			return LooseMaterialFriction;
		}
	}
	return GroundFriction;
}

void UQuarryVehicleMovementComponent::UpdateTireForces(FQuarryWheelState& Wheel, float DeltaTime, float DriveTorqueNmPerWheel)
{
	if (!Wheel.bInContact)
	{
		Wheel.SlipRatio = 0.0f;
		Wheel.SlipAngleRad = 0.0f;
		Wheel.TireLongForceUE = 0.0f;
		Wheel.TireLatForceUE = 0.0f;
		Wheel.LoadForceUE = 0.0f;

		// Airborne: drive and brake torques only, with slight windage loss.
		float AngularVelocity = Wheel.AngularVelocityRadS
			+ DriveTorqueNmPerWheel / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
		AngularVelocity *= FMath::Exp(-0.25f * DeltaTime);
		const float BrakeOmegaStep = GetBrakeTorqueNm() / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
		if (FMath::Abs(AngularVelocity) <= BrakeOmegaStep)
		{
			AngularVelocity = 0.0f;
		}
		else
		{
			AngularVelocity -= FMath::Sign(AngularVelocity) * BrakeOmegaStep;
		}
		Wheel.AngularVelocityRadS = AngularVelocity;
		return;
	}

	const FVector Up = Chassis->GetUpVector();
	const FVector Forward = Chassis->GetForwardVector();
	const FVector SteeredForward = Wheel.SteerAngleRad != 0.0f
		? Forward.RotateAngleAxis(FMath::RadiansToDegrees(Wheel.SteerAngleRad), Up)
		: Forward;
	const FVector Lateral = FVector::CrossProduct(Up, SteeredForward);

	const FVector ContactVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(Wheel.ContactPoint);
	const float VLong = FVector::DotProduct(ContactVelocity, SteeredForward);
	const float VLat = FVector::DotProduct(ContactVelocity, Lateral);

	const float WheelSpeedCmS = Wheel.AngularVelocityRadS * Wheel.Radius;
	const float SlipDenominator = FMath::Max(FMath::Max(FMath::Abs(VLong), FMath::Abs(WheelSpeedCmS)), MinSlipSpeedCmS);
	Wheel.SlipRatio = (WheelSpeedCmS - VLong) / SlipDenominator;
	Wheel.SlipAngleRad = FMath::Atan2(VLat, FMath::Abs(VLong) + 5.0f);

	// Simplified Pacejka: F = mu*N*sin(C*atan(B*slip)), clamped by the friction circle.
	const float Friction = FMath::Max(0.05f, Wheel.ContactFriction) * TireFrictionScale;
	const float MaxGripForceUE = Friction * Wheel.LoadForceUE;
	float LongForceUE = MaxGripForceUE * FMath::Sin(TireShapeFactor * FMath::Atan(TireSlipStiffness * Wheel.SlipRatio));
	float LatForceUE = -MaxGripForceUE * FMath::Sin(TireShapeFactor * FMath::Atan(TireSlipStiffness * Wheel.SlipAngleRad));

	const float CombinedForce = FVector2D(LongForceUE, LatForceUE).Size();
	if (CombinedForce > MaxGripForceUE && CombinedForce > KINDA_SMALL_NUMBER)
	{
		const float Scale = MaxGripForceUE / CombinedForce;
		LongForceUE *= Scale;
		LatForceUE *= Scale;
	}

	// Rolling resistance opposes the rolling direction.
	LongForceUE -= FMath::Sign(VLong) * RollingResistance * Wheel.LoadForceUE;

	Chassis->AddForceAtLocation(SteeredForward * LongForceUE + Lateral * LatForceUE, Wheel.ContactPoint);
	Wheel.TireLongForceUE = LongForceUE;
	Wheel.TireLatForceUE = LatForceUE;
	LastDriveForce += LongForceUE / UeForcePerNewton;

	// Wheel spin integration: drive torque vs tire reaction torque and brake.
	const float TireReactionNm = LongForceUE / UeForcePerNewton * (Wheel.Radius * 0.01f);
	float AngularVelocity = Wheel.AngularVelocityRadS
		+ (DriveTorqueNmPerWheel - TireReactionNm) / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
	const float BrakeOmegaStep = GetBrakeTorqueNm() / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
	if (FMath::Abs(AngularVelocity) <= BrakeOmegaStep)
	{
		AngularVelocity = 0.0f;
	}
	else
	{
		AngularVelocity -= FMath::Sign(AngularVelocity) * BrakeOmegaStep;
	}
	Wheel.AngularVelocityRadS = AngularVelocity;
}

void UQuarryVehicleMovementComponent::TickLegacyKinematic(float DeltaTime)
{
	GroundedWheelCount = 0;
	GroundHeightSum = 0.0f;
	LastDriveForce = 0.0f;
	int32 DrivenWheelCount = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		DrivenWheelCount += Wheel.bDriven ? 1 : 0;
	}

	for (FQuarryWheelState& Wheel : Wheels)
	{
		SimulateWheel(Wheel, DeltaTime);
	}

	const float TotalMass = FMath::Max(1.0f, BaseMassKg + PayloadMassKg);
	const float EffectiveBrake = FMath::Max(BrakeCommand, bParkingBrake ? 1.0f : 0.0f);
	const float TargetSpeed = EffectiveBrake > 0.01f ? 0.0f
		: ThrottleCommand * (ThrottleCommand >= 0.0f ? MaxForwardSpeed : MaxReverseSpeed);
	float Acceleration = FMath::Abs(ThrottleCommand) > 0.02f ? EngineForce * 100.0f / TotalMass : 50.0f;
	if (EffectiveBrake > 0.01f)
	{
		Acceleration = BrakeForce * 100.0f / TotalMass * EffectiveBrake;
	}
	const float CurrentSpeed = GetForwardSpeed();
	const float ForwardSpeed = CurrentSpeed + FMath::Clamp(TargetSpeed - CurrentSpeed, -Acceleration * DeltaTime, Acceleration * DeltaTime);
	LastDriveForce = ThrottleCommand * EngineForce;

	float YawRate = 0.0f;
	if (DriveType == EQuarryDriveType::Tracked)
	{
		YawRate = SteeringCommand * 45.0f;
	}
	else if (FMath::Abs(ForwardSpeed) > 5.0f)
	{
		YawRate = SteeringCommand * 45.0f * FMath::Clamp(FMath::Abs(ForwardSpeed) / 350.0f, 0.0f, 1.0f)
			* FMath::Sign(ForwardSpeed);
	}
	if (FMath::Abs(YawRate) > 0.01f)
	{
		GetOwner()->SetActorRotation(GetOwner()->GetActorRotation() + FRotator(0.0f, YawRate * DeltaTime, 0.0f), ETeleportType::TeleportPhysics);
	}

	float VerticalVelocity = Chassis->GetPhysicsLinearVelocity().Z - 981.0f * DeltaTime;
	if (GroundedWheelCount > 0)
	{
		const float TargetHeight = GroundHeightSum / GroundedWheelCount + SuspensionRestLength;
		VerticalVelocity = FMath::Clamp((TargetHeight - Chassis->GetComponentLocation().Z) * 6.0f, -350.0f, 350.0f);
	}
	Chassis->SetPhysicsLinearVelocity(Chassis->GetForwardVector() * ForwardSpeed + FVector(0.0, 0.0, VerticalVelocity));
}

void UQuarryVehicleMovementComponent::SimulateWheel(FQuarryWheelState& Wheel, float DeltaTime)
{
	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	const FVector MountLocation = ChassisTransform.TransformPosition(Wheel.MountOffset);
	const FVector UpVector = Chassis->GetUpVector();
	const float TraceLength = SuspensionRestLength + Wheel.Radius + SuspensionTravel;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(QuarryWheelTrace), false, GetOwner());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, MountLocation, MountLocation - UpVector * TraceLength, QuarryCollision::Terrain, QueryParams);
	if (!bHit)
	{
		Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, SuspensionRestLength + SuspensionTravel, DeltaTime, 8.0f);
		return;
	}

	++GroundedWheelCount;
	GroundHeightSum += Hit.ImpactPoint.Z + Wheel.Radius - Wheel.MountOffset.Z;
	const float TargetLength = FMath::Clamp(Hit.Distance - Wheel.Radius, 0.0f, SuspensionRestLength + SuspensionTravel);
	Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, TargetLength, DeltaTime, 12.0f);
}

void UQuarryVehicleMovementComponent::UpdateWheelVisual(FQuarryWheelState& Wheel, float DeltaTime)
{
	if (!Wheel.Visual)
	{
		return;
	}

	Wheel.Visual->SetRelativeLocation(Wheel.MountOffset - FVector(0.0, 0.0, Wheel.SuspensionLength));
	// Rolling forward means rotating about the NEGATIVE lateral axis
	// (right-hand rule: v = omega x r with contact below), so negate omega.
	Wheel.SpinAngle = FMath::UnwindDegrees(Wheel.SpinAngle
		- FMath::RadiansToDegrees(Wheel.AngularVelocityRadS) * DeltaTime);
	Wheel.Visual->SetRelativeRotation(Wheel.BaseVisualRotation
		+ FRotator(Wheel.SpinAngle, FMath::RadiansToDegrees(Wheel.SteerAngleRad), 0.0f));
}
