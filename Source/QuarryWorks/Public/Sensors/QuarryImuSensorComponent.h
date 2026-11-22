#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "QuarryImuSensorComponent.generated.h"

class UPrimitiveComponent;

/**
 * CARLA-style IMU ground-truth sensor: accelerometer specific force (gravity
 * removed, body frame, m/s^2), gyroscope angular velocity (body frame, rad/s),
 * and world-space acceleration. Samples at a fixed rate with optional bias and
 * noise so RL/autonomy stacks can train against realistic sensor streams.
 */
UCLASS(ClassGroup=(Quarry), meta=(BlueprintSpawnableComponent))
class QUARRYWORKS_API UQuarryImuSensorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UQuarryImuSensorComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Fixed sample rate in Hz; 0 samples every tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Imu")
	float SampleRateHz = 100.0f;

	/** Constant accelerometer bias in m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Imu|Noise")
	FVector AccelerometerBiasMps2 = FVector::ZeroVector;

	/** Accelerometer white noise standard deviation in m/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Imu|Noise")
	float AccelerometerNoiseStdDev = 0.0f;

	/** Constant gyroscope bias in rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Imu|Noise")
	FVector GyroscopeBiasRadS = FVector::ZeroVector;

	/** Gyroscope white noise standard deviation in rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Imu|Noise")
	float GyroscopeNoiseStdDev = 0.0f;

	/** Latest accelerometer specific force in the body frame, m/s^2. */
	UFUNCTION(BlueprintPure, Category="Quarry|Imu")
	FVector GetSpecificForceMps2() const { return SpecificForceMps2; }

	/** Latest gyroscope angular velocity in the body frame, rad/s. */
	UFUNCTION(BlueprintPure, Category="Quarry|Imu")
	FVector GetAngularVelocityRadS() const { return AngularVelocityRadS; }

	/** Latest world-space linear acceleration of the tracked body, m/s^2. */
	UFUNCTION(BlueprintPure, Category="Quarry|Imu")
	FVector GetWorldAccelerationMps2() const { return WorldAccelerationMps2; }

	/**
	 * CARLA-style compass heading in radians [0, 2*PI), 0 = north (+X of the
	 * configured geo frame), measured clockwise when viewed from above.
	 */
	UFUNCTION(BlueprintPure, Category="Quarry|Imu")
	float GetCompassRadians() const;

private:
	void SampleImu(float DeltaTime);
	FVector ApplyNoise(const FVector& Value, float StdDev, const FVector& Bias) const;

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> TrackedBody;

	FVector LastWorldVelocityCmS = FVector::ZeroVector;
	FVector SpecificForceMps2 = FVector(0.0f, 0.0f, 9.81f);
	FVector AngularVelocityRadS = FVector::ZeroVector;
	FVector WorldAccelerationMps2 = FVector::ZeroVector;
	float SampleAccumulator = 0.0f;
	bool bHasVelocitySample = false;
};
