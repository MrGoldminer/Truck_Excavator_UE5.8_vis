#include "Materials/QuarryMaterialPile.h"

#include "Components/SceneComponent.h"
#include "Vehicles/QuarryBlockoutComponent.h"
#include "Vehicles/QuarryBlockoutVisuals.h"

AQuarryMaterialPile::AQuarryMaterialPile()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	PileVisual = FQuarryBlockoutVisuals::AddSphere(
		this, TEXT("PileVisual"), GetRootComponent(), FVector::OneVector,
		FVector::ZeroVector, FLinearColor(0.55f, 0.38f, 0.16f));
}

void AQuarryMaterialPile::BeginPlay()
{
	Super::BeginPlay();
	UpdateVisual();
}

float AQuarryMaterialPile::GetRemainingVolumeM3() const
{
	return BulkDensityKgPerM3 > 0.0f ? RemainingMassKg / BulkDensityKgPerM3 : 0.0f;
}

// 挖取：无限源直接给量；有限源按剩余量给（挖空即止）+ 广播变化事件
float AQuarryMaterialPile::Extract(float RequestedMassKg)
{
	if (MaterialType == EQuarryMaterialType::None || !FMath::IsFinite(RequestedMassKg) || RequestedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	const float ExtractedMassKg = bInfiniteSource ? RequestedMassKg : FMath::Min(RequestedMassKg, RemainingMassKg);
	if (ExtractedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	if (!bInfiniteSource)
	{
		RemainingMassKg = FMath::Max(0.0f, RemainingMassKg - ExtractedMassKg);
	}
	UpdateVisual();
	OnPileChanged.Broadcast(this, -ExtractedMassKg, false);
	return ExtractedMassKg;
}

float AQuarryMaterialPile::ExtractAt(const FVector& WorldLocation, float RequestedMassKg)
{
	return Extract(RequestedMassKg);
}

float AQuarryMaterialPile::Deposit(EQuarryMaterialType InMaterialType, float OfferedMassKg)
{
	if (!bAllowDeposits || MaterialType == EQuarryMaterialType::None || InMaterialType != MaterialType
		|| !FMath::IsFinite(OfferedMassKg) || OfferedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	const float FreeCapacityKg = DepositCapacityKg > 0.0f
		? FMath::Max(0.0f, DepositCapacityKg - RemainingMassKg) : OfferedMassKg;
	const float AcceptedMassKg = FMath::Min(OfferedMassKg, FreeCapacityKg);
	if (AcceptedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	RemainingMassKg += AcceptedMassKg;
	UpdateVisual();
	OnPileChanged.Broadcast(this, AcceptedMassKg, true);
	return AcceptedMassKg;
}

float AQuarryMaterialPile::DepositAt(const FVector& WorldLocation, EQuarryMaterialType InMaterialType, float OfferedMassKg)
{
	return Deposit(InMaterialType, OfferedMassKg);
}

float AQuarryMaterialPile::GetDistanceToWorkSurface(const FVector& WorldLocation) const
{
	return FVector::Dist(WorldLocation, GetActorLocation() + FVector(0.0, 0.0, 50.0));
}

void AQuarryMaterialPile::UpdateVisual()
{
	if (!PileVisual)
	{
		return;
	}

	const float VolumeM3 = GetRemainingVolumeM3();
	PileVisual->SetVisibility(VolumeM3 > 0.001f);
	const float Radius = FMath::Clamp(FMath::Pow(FMath::Max(0.0f, VolumeM3), 1.0f / 3.0f) * 60.0f, 20.0f, 400.0f);
	PileVisual->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, Radius / 100.0f));
}
