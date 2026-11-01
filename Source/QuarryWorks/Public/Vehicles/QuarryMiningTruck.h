#pragma once

#include "CoreMinimal.h"
#include "Vehicles/QuarryVehiclePawn.h"
#include "QuarryMiningTruck.generated.h"

class AQuarryMaterialPile;
class UInputAction;
class UQuarryBlockoutComponent;
class UQuarryCargoComponent;
class USceneComponent;

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryMiningTruck : public AQuarryVehiclePawn
{
	GENERATED_BODY()

public:
	AQuarryMiningTruck();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Truck")
	TObjectPtr<USceneComponent> BedPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Truck")
	TObjectPtr<USceneComponent> BedIntake;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Truck")
	TObjectPtr<USceneComponent> DumpPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	TObjectPtr<UQuarryBlockoutComponent> CargoFill;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	TObjectPtr<UQuarryCargoComponent> Cargo;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo")
	float UnloadRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo")
	float UnloadRateKgPerSecond = 6000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	bool bUnloading = false;

	UFUNCTION(BlueprintPure, Category="Cargo")
	FVector GetCargoIntakeLocation() const;

	UFUNCTION(BlueprintCallable, Category="Cargo")
	void ToggleUnloading();

	UFUNCTION(BlueprintCallable, Category="Cargo")
	void StopUnloading();

	UFUNCTION(BlueprintCallable, Category="Cargo")
	float UnloadToNearestPile(float RequestedMassKg);

	UFUNCTION(BlueprintPure, Category="Cargo")
	FString GetOperationStatus() const { return OperationStatus; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> TipAction;

	virtual void CreateDefaultInputMappings() override;

private:
	AQuarryMaterialPile* FindUnloadPile() const;

	UFUNCTION()
	void UpdateCargoVisual(EQuarryMaterialType MaterialType, float CurrentMassKg, float CapacityKg);

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMaterialPile> UnloadPile = nullptr;

	FString OperationStatus = TEXT("READY");
	float BedAngle = 0.0f;
};
