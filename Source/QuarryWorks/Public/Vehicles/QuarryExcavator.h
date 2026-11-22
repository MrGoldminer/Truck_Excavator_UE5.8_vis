#pragma once

#include "CoreMinimal.h"
#include "Vehicles/QuarryVehiclePawn.h"
#include "QuarryExcavator.generated.h"

class AQuarryMaterialPile;
class AQuarryMiningTruck;
class UInputAction;
class UQuarryBlockoutComponent;
class UQuarryCargoComponent;
class UQuarryVehicleMeshComponent;
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

	/** Real track chains + road wheels from the ConstructionMachines pack. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vehicle")
	TObjectPtr<UQuarryVehicleMeshComponent> RealVehicleMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Excavator")
	float SlewSpeedDegreesPerSecond = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Excavator")
	float HydraulicSpeedDegreesPerSecond = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo")
	float WorkRateKgPerSecond = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo", meta=(ClampMin="1.0"))
	float ScoopMassKg = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo", meta=(ClampMin="100.0"))
	float WorkRadius = 600.0f;

	// --- Dig mechanics: rate scales with bucket tip speed, cut angle, and material hardness ---

	/** Bucket tip speed below which the bucket cannot load material, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digging")
	float MinDigSpeedCmS = 60.0f;

	/** Bucket tip speed at which the dig rate reaches 100%, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digging")
	float OptimalDigSpeedCmS = 350.0f;

	/** World pitch of the bucket that gives the best cut, degrees. Negative = teeth bite downward, which is what a real dig pose reaches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digging")
	float OptimalCutAngleDeg = -10.0f;

	/** Cut angle error at which digging efficiency reaches zero, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digging")
	float CutAngleToleranceDeg = 30.0f;

	/** Smoothed bucket tip speed for dig mechanics, cm/s. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Digging")
	float BucketTipSpeedCmS = 0.0f;

	/** Dig-sample location: the real FK bucket tip when available, else BucketTip. */
	FVector GetDigSampleLocation() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float SlewAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float BoomAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float StickAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Excavator")
	float BucketAngle = 0.0f;

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

	/** Remote actuator control (protocol/TCP path); each axis clamped to [-1, 1]. */
	UFUNCTION(BlueprintCallable, Category="Quarry|Remote")
	void ApplyActuatorCommands(float Slew, float Boom, float Stick, float Bucket);

	/** Hold-to-dig / hold-to-dump semantics for remote control. */
	UFUNCTION(BlueprintCallable, Category="Quarry|Remote")
	void SetDiggingActive(bool bActive);

	UFUNCTION(BlueprintCallable, Category="Quarry|Remote")
	void SetDumpingActive(bool bActive);

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
	float ComputeDigEfficiency() const;
	AQuarryMaterialPile* FindNearestPile(float Radius) const;
	AQuarryMiningTruck* FindNearestTruck(float Radius) const;

	UFUNCTION()
	void UpdateCargoVisual(EQuarryMaterialType MaterialType, float CurrentMassKg, float CapacityKg);

	float SlewCommand = 0.0f;
	float BoomCommand = 0.0f;
	float StickCommand = 0.0f;
	float BucketCommand = 0.0f;
	FString OperationStatus = TEXT("READY");
	FVector LastTipLocation = FVector::ZeroVector;
	bool bHasLastTipLocation = false;
	TArray<TObjectPtr<UPrimitiveComponent>> ReplacedVisualParts; // blockout tracks hidden when real meshes load

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMaterialPile> WorkPile = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMiningTruck> WorkTruck = nullptr;
};
