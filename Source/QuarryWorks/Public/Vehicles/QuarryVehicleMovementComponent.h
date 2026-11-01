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
};

UCLASS(ClassGroup=(Quarry), meta=(BlueprintSpawnableComponent))
class QUARRYWORKS_API UQuarryVehicleMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UQuarryVehicleMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	EQuarryDriveType DriveType = EQuarryDriveType::Wheeled;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle", meta=(ClampMin="1.0"))
	float BaseMassKg = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle")
	float EngineForce = 80000.0f;

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

	void ConfigureChassis(UBoxComponent* InChassis);
	void AddWheel(const FVector& MountOffset, float Radius, bool bDriven, bool bSteered, USceneComponent* Visual);

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

private:
	void ApplyMassProperties();
	void SimulateWheel(FQuarryWheelState& Wheel, int32 DrivenWheelCount, float DeltaTime);
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
};
