#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cargo/QuarryMaterialTypes.h"
#include "QuarryMaterialPile.generated.h"

class UQuarryBlockoutComponent;
class AQuarryMaterialPile;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FQuarryPileChanged, AQuarryMaterialPile*, Pile, float, DeltaMassKg, bool, bDeposited);

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryMaterialPile : public AActor
{
	GENERATED_BODY()

public:
	AQuarryMaterialPile();
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material")
	EQuarryMaterialType MaterialType = EQuarryMaterialType::Sand;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material", meta=(ClampMin="0.0"))
	float RemainingMassKg = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material", meta=(ClampMin="1.0"))
	float BulkDensityKgPerM3 = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material")
	bool bInfiniteSource = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material")
	bool bAllowDeposits = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material", meta=(ClampMin="0.0"))
	float DepositCapacityKg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material")
	bool bCountsAsDelivery = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Material")
	float WorkAreaRadius = 400.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Visual")
	TObjectPtr<UQuarryBlockoutComponent> PileVisual;

	UPROPERTY(BlueprintAssignable, Category="Material")
	FQuarryPileChanged OnPileChanged;

	UFUNCTION(BlueprintPure, Category="Material")
	float GetRemainingVolumeM3() const;

	UFUNCTION(BlueprintCallable, Category="Material")
	virtual float Extract(float RequestedMassKg);

	UFUNCTION(BlueprintCallable, Category="Material")
	virtual float ExtractAt(const FVector& WorldLocation, float RequestedMassKg);

	UFUNCTION(BlueprintCallable, Category="Material")
	virtual float Deposit(EQuarryMaterialType InMaterialType, float OfferedMassKg);

	UFUNCTION(BlueprintCallable, Category="Material")
	virtual float DepositAt(const FVector& WorldLocation, EQuarryMaterialType InMaterialType, float OfferedMassKg);

	UFUNCTION(BlueprintPure, Category="Material")
	virtual float GetDistanceToWorkSurface(const FVector& WorldLocation) const;

public:
	UFUNCTION(BlueprintCallable, Category="Visual")
	virtual void UpdateVisual();
};
