#include "Worksite/QuarryWorksite.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Materials/QuarryMaterialPile.h"
#include "Terrain/QuarryDigSite.h"
#include "Vehicles/QuarryBlockoutVisuals.h"
#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Worksite/QuarryWorksiteSubsystem.h"

AQuarryWorksite::AQuarryWorksite()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

// 采石场场景装配总入口：生成挖掘机/矿卡/料场/卸料区 + 注册到子系统
void AQuarryWorksite::InitializeWorksite(AQuarryExcavator* PlayerExcavator)
{
	if (bInitialized || !GetWorld()) return;
	bInitialized = true;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Excavator = PlayerExcavator;
	if (!Excavator)
	{
		Excavator = GetWorld()->SpawnActor<AQuarryExcavator>(
			GetActorTransform().TransformPosition(ExcavatorSpawnLocation), FRotator::ZeroRotator, SpawnParameters);
	}
	MiningTruck = GetWorld()->SpawnActor<AQuarryMiningTruck>(
		GetActorTransform().TransformPosition(TruckSpawnLocation), FRotator::ZeroRotator, SpawnParameters);
	DigSite = GetWorld()->SpawnActor<AQuarryDigSite>(
		GetActorTransform().TransformPosition(DigSiteLocation), FRotator::ZeroRotator, SpawnParameters);
	DeliveryPile = GetWorld()->SpawnActor<AQuarryMaterialPile>(
		GetActorTransform().TransformPosition(DepotLocation), FRotator::ZeroRotator, SpawnParameters);
	DeliveryPile->RemainingMassKg = 0.0f;
	DeliveryPile->bAllowDeposits = true;
	DeliveryPile->bCountsAsDelivery = true;
	DeliveryPile->MaterialType = EQuarryMaterialType::Sand;
	DeliveryPile->UpdateVisual();

	FQuarryBlockoutVisuals::AddCube(this, TEXT("DepotPad"), GetRootComponent(), FVector(500.0, 500.0, 12.0),
		DepotLocation - FVector(0.0, 0.0, 6.0), FLinearColor(0.16f, 0.35f, 0.18f), true);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("DigSiteMarker"), GetRootComponent(), FVector(80.0, 80.0, 250.0),
		DigSiteLocation + FVector(-700.0, 0.0, 125.0), FLinearColor(0.9f, 0.55f, 0.05f), true);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("DepotMarker"), GetRootComponent(), FVector(80.0, 80.0, 250.0),
		DepotLocation + FVector(-300.0, 0.0, 125.0), FLinearColor(0.1f, 0.65f, 0.25f), true);

	if (UQuarryWorksiteSubsystem* Subsystem = GetWorld()->GetSubsystem<UQuarryWorksiteSubsystem>())
	{
		Subsystem->RegisterExcavator(Excavator);
		Subsystem->RegisterMiningTruck(MiningTruck);
		Subsystem->RegisterDigSite(DigSite);
		Subsystem->RegisterDeliveryPile(DeliveryPile);
		Subsystem->Notify(TEXT("WORKSITE READY: CTRL+1 EXCAVATOR / CTRL+2 TRUCK"), 6.0f);
	}
}
