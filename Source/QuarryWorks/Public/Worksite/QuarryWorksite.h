#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QuarryWorksite.generated.h"

class AQuarryDigSite;
class AQuarryExcavator;
class AQuarryMaterialPile;
class AQuarryMiningTruck;

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryWorksite : public AActor
{
	GENERATED_BODY()

public:
	AQuarryWorksite();

	UFUNCTION(BlueprintCallable, Category="Worksite")
	void InitializeWorksite(AQuarryExcavator* PlayerExcavator);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Worksite")
	FVector ExcavatorSpawnLocation = FVector(-650.0, 400.0, 180.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Worksite")
	FVector TruckSpawnLocation = FVector(0.0, 500.0, 180.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Worksite")
	FVector DigSiteLocation = FVector(650.0, -450.0, 5.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Worksite")
	FVector DepotLocation = FVector(-650.0, -350.0, 5.0);

private:
	UPROPERTY(Transient)
	TObjectPtr<AQuarryExcavator> Excavator = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMiningTruck> MiningTruck = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryDigSite> DigSite = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMaterialPile> DeliveryPile = nullptr;

	bool bInitialized = false;
};
