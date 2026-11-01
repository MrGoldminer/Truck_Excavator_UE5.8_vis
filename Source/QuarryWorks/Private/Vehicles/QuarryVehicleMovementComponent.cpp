#include "Vehicles/QuarryVehicleMovementComponent.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "QuarryCollisionChannels.h"

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

void UQuarryVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Chassis || !Chassis->IsSimulatingPhysics() || DeltaTime <= 0.0f)
	{
		return;
	}

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
		SimulateWheel(Wheel, FMath::Max(1, DrivenWheelCount), DeltaTime);
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

	for (FQuarryWheelState& Wheel : Wheels)
	{
		UpdateWheelVisual(Wheel, DeltaTime);
	}
}

void UQuarryVehicleMovementComponent::SimulateWheel(FQuarryWheelState& Wheel, int32 DrivenWheelCount, float DeltaTime)
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
	const float SteerAngle = Wheel.bSteered ? SteeringCommand * MaxSteerAngle : 0.0f;
	Wheel.SpinAngle = FMath::UnwindDegrees(Wheel.SpinAngle + GetForwardSpeed() * DeltaTime / FMath::Max(1.0f, Wheel.Radius) * 57.2958f);
	Wheel.Visual->SetRelativeRotation(Wheel.BaseVisualRotation + FRotator(Wheel.SpinAngle, SteerAngle, 0.0f));
}
