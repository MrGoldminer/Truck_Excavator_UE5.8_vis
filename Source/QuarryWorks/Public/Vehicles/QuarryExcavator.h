#pragma once

#include "CoreMinimal.h"
#include "Vehicles/QuarryVehiclePawn.h"
#include "QuarryExcavator.generated.h"

class AQuarryMaterialPile;
class AQuarryMiningTruck;
class UInputAction;
class UQuarryBlockoutComponent;
class UQuarryCargoComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EQuarryExcavatorWorkState : uint8
{
	Idle,
	Digging,
	Dumping
};

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryExcavator : public AQuarryVehiclePawn
{
	GENERATED_BODY()

public:
	AQuarryExcavator();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	TObjectPtr<USceneComponent> UpperStructurePivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	TObjectPtr<USceneComponent> BoomPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	TObjectPtr<USceneComponent> StickPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	TObjectPtr<USceneComponent> BucketPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	TObjectPtr<USceneComponent> BucketTip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	TObjectPtr<UQuarryBlockoutComponent> BucketFill;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	TObjectPtr<UQuarryCargoComponent> BucketCargo;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Excavator")
	float SlewSpeedDegreesPerSecond = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Excavator")
	float HydraulicSpeedDegreesPerSecond = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo")
	float WorkRateKgPerSecond = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo", meta=(ClampMin="1.0"))
	float ScoopMassKg = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo", meta=(ClampMin="100.0"))
	float WorkRadius = 400.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float SlewAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float BoomAngle = -10.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float StickAngle = -25.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float BucketAngle = 15.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	EQuarryExcavatorWorkState WorkState = EQuarryExcavatorWorkState::Idle;

	UFUNCTION(BlueprintCallable, Category="Cargo")
	void StartDigging();

	UFUNCTION(BlueprintCallable, Category="Cargo")
	void StopDigging();

	UFUNCTION(BlueprintCallable, Category="Cargo")
	void StartDumping();

	UFUNCTION(BlueprintCallable, Category="Cargo")
	void StopDumping();

	UFUNCTION(BlueprintCallable, Category="Cargo")
	float DigFromNearestPile();

	UFUNCTION(BlueprintCallable, Category="Cargo")
	float DumpIntoNearestTruck();

	UFUNCTION(BlueprintPure, Category="Cargo")
	FString GetOperationStatus() const { return OperationStatus; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> SlewAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> BoomAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> StickAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> BucketAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> DigAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> DumpAction;

	virtual void CreateDefaultInputMappings() override;

private:
	void HandleSlew(const FInputActionValue& Value);
	void HandleBoom(const FInputActionValue& Value);
	void HandleStick(const FInputActionValue& Value);
	void HandleBucket(const FInputActionValue& Value);
	void TickWork(float DeltaSeconds);
	AQuarryMaterialPile* FindNearestPile(float Radius) const;
	AQuarryMiningTruck* FindNearestTruck(float Radius) const;

	UFUNCTION()
	void UpdateCargoVisual(EQuarryMaterialType MaterialType, float CurrentMassKg, float CapacityKg);

	float SlewCommand = 0.0f;
	float BoomCommand = 0.0f;
	float StickCommand = 0.0f;
	float BucketCommand = 0.0f;
	FString OperationStatus = TEXT("READY");

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMaterialPile> WorkPile = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMiningTruck> WorkTruck = nullptr;
};
