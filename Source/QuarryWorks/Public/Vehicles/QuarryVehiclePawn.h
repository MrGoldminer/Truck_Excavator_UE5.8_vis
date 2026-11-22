#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "QuarryVehiclePawn.generated.h"

struct FInputActionValue;
class UBoxComponent;
class UCameraComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UQuarryImuSensorComponent;
class UQuarryVehicleMovementComponent;
class USpringArmComponent;

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	AQuarryVehiclePawn();

	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UBoxComponent> Chassis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UQuarryVehicleMovementComponent> VehicleMovement;

	/** CARLA-style IMU ground-truth sensor attached to the chassis. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UQuarryImuSensorComponent> ImuSensor;

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetDriveInput(float Throttle, float Steering);

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetServiceBrake(float Brake);

	UFUNCTION(BlueprintCallable, Category="Quarry|Driving")
	void SetParkingBrake(bool bEnabled);

	UFUNCTION(BlueprintPure, Category="Quarry|Driving")
	float GetForwardSpeed() const;

	UFUNCTION(BlueprintPure, Category="Quarry|Driving")
	bool IsNearlyStationary() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputMappingContext> DrivingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> ThrottleAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> SteeringAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> BrakeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> HandbrakeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> LookAction;

	virtual void CreateDefaultInputMappings();

private:
	void HandleThrottle(const FInputActionValue& Value);
	void HandleSteering(const FInputActionValue& Value);
	void HandleBrake(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem() const;
	void RemoveDrivingContext();

	float ThrottleCommand = 0.0f;
	float SteeringCommand = 0.0f;
};
