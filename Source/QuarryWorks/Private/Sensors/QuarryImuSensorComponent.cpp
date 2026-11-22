#include "Sensors/QuarryImuSensorComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	// Gravity magnitude in m/s^2 (UE gravity is 981 cm/s^2).
	constexpr float GravityMps2 = 9.81f;
	constexpr float CmPerSecondSquaredToMps2 = 0.01f;
}

UQuarryImuSensorComponent::UQuarryImuSensorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UQuarryImuSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	TrackedBody = Cast<UPrimitiveComponent>(GetOwner() ? GetOwner()->GetRootComponent() : nullptr);
	if (TrackedBody)
	{
		LastWorldVelocityCmS = TrackedBody->GetPhysicsLinearVelocity();
		bHasVelocitySample = true;
	}
}

void UQuarryImuSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!TrackedBody || DeltaTime <= 0.0f)
	{
		return;
	}

	if (SampleRateHz <= 0.0f)
	{
		SampleImu(DeltaTime);
		return;
	}

	// Fixed-rate sampling driven by accumulated wall-clock tick time.
	SampleAccumulator += DeltaTime;
	const float SampleInterval = 1.0f / FMath::Max(1.0f, SampleRateHz);
	int32 Guard = 0;
	while (SampleAccumulator >= SampleInterval && Guard++ < 8)
	{
		SampleImu(SampleInterval);
		SampleAccumulator -= SampleInterval;
	}
}

float UQuarryImuSensorComponent::GetCompassRadians() const
{
	if (!TrackedBody)
	{
		return 0.0f;
	}
	// Forward projected onto the horizontal plane, measured from +X (north).
	const FVector Forward = TrackedBody->GetForwardVector();
	const float Heading = FMath::Atan2(Forward.Y, Forward.X);
	return FMath::Frac((Heading + 2.0f * PI) / (2.0f * PI)) * 2.0f * PI;
}

void UQuarryImuSensorComponent::SampleImu(float DeltaTime)
{
	if (!TrackedBody)
	{
		return;
	}

	const FVector WorldVelocityCmS = TrackedBody->GetPhysicsLinearVelocity();
	if (!bHasVelocitySample)
	{
		LastWorldVelocityCmS = WorldVelocityCmS;
		bHasVelocitySample = true;
	}

	// World-space acceleration, converted to m/s^2.
	const FVector WorldAccelMps2 = (WorldVelocityCmS - LastWorldVelocityCmS) / FMath::Max(DeltaTime, 1e-4f) * CmPerSecondSquaredToMps2;
	LastWorldVelocityCmS = WorldVelocityCmS;
	WorldAccelerationMps2 = WorldAccelMps2;

	// Specific force: accelerometer measures reaction to gravity, f = R^T * (a - g).
	const FVector GravityWorldMps2(0.0f, 0.0f, -GravityMps2);
	const FVector SpecificForceWorld = WorldAccelMps2 - GravityWorldMps2;
	const FTransform BodyTransform = TrackedBody->GetComponentTransform();
	const FVector SpecificForceBody = BodyTransform.InverseTransformVectorNoScale(SpecificForceWorld);

	// Gyroscope: world angular velocity expressed in the body frame.
	const FVector WorldAngularVelocity = TrackedBody->GetPhysicsAngularVelocityInRadians();
	const FVector AngularVelocityBody = BodyTransform.InverseTransformVectorNoScale(WorldAngularVelocity);

	SpecificForceMps2 = ApplyNoise(SpecificForceBody, AccelerometerNoiseStdDev, AccelerometerBiasMps2);
	AngularVelocityRadS = ApplyNoise(AngularVelocityBody, GyroscopeNoiseStdDev, GyroscopeBiasRadS);
}

FVector UQuarryImuSensorComponent::ApplyNoise(const FVector& Value, float StdDev, const FVector& Bias) const
{
	if (StdDev <= 0.0f)
	{
		return Value + Bias;
	}
	// Sum-of-uniforms approximation of a zero-mean gaussian.
	auto RandomUnit = []() -> float { return FMath::FRandRange(-1.0f, 1.0f); };
	const float NoiseX = (RandomUnit() + RandomUnit() + RandomUnit()) * StdDev * 0.5f;
	const float NoiseY = (RandomUnit() + RandomUnit() + RandomUnit()) * StdDev * 0.5f;
	const float NoiseZ = (RandomUnit() + RandomUnit() + RandomUnit()) * StdDev * 0.5f;
	return Value + Bias + FVector(NoiseX, NoiseY, NoiseZ);
}
