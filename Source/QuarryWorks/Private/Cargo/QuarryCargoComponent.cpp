#include "Cargo/QuarryCargoComponent.h"

UQuarryCargoComponent::UQuarryCargoComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UQuarryCargoComponent::GetFreeCapacityKg() const
{
	return FMath::Max(0.0f, CapacityKg - CurrentMassKg);
}

float UQuarryCargoComponent::GetFillRatio() const
{
	return CapacityKg > 0.0f ? FMath::Clamp(CurrentMassKg / CapacityKg, 0.0f, 1.0f) : 0.0f;
}

bool UQuarryCargoComponent::CanAcceptMaterial(EQuarryMaterialType InMaterialType) const
{
	return InMaterialType != EQuarryMaterialType::None
		&& GetFreeCapacityKg() > 0.0f
		&& (CurrentMassKg <= KINDA_SMALL_NUMBER || MaterialType == InMaterialType);
}

// 装载：同类型物料才能混装（沙/砾石/矿分仓），返回实际接受量
float UQuarryCargoComponent::Load(EQuarryMaterialType InMaterialType, float RequestedMassKg)
{
	if (!CanAcceptMaterial(InMaterialType) || !FMath::IsFinite(RequestedMassKg) || RequestedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	const float AcceptedMassKg = FMath::Min(RequestedMassKg, GetFreeCapacityKg());
	if (AcceptedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	MaterialType = InMaterialType;
	CurrentMassKg += AcceptedMassKg;
	NotifyCargoChanged();
	return AcceptedMassKg;
}

float UQuarryCargoComponent::Unload(float RequestedMassKg, EQuarryMaterialType& OutMaterialType)
{
	OutMaterialType = MaterialType;
	if (!FMath::IsFinite(RequestedMassKg) || RequestedMassKg <= 0.0f || CurrentMassKg <= 0.0f)
	{
		return 0.0f;
	}

	const float RemovedMassKg = FMath::Min(RequestedMassKg, CurrentMassKg);
	CurrentMassKg = FMath::Max(0.0f, CurrentMassKg - RemovedMassKg);
	if (CurrentMassKg <= KINDA_SMALL_NUMBER)
	{
		CurrentMassKg = 0.0f;
		MaterialType = EQuarryMaterialType::None;
	}

	NotifyCargoChanged();
	return RemovedMassKg;
}

// 转移（挖斗→车斗）：先在目标方预留容量再扣源，保证料流守恒不超发
float UQuarryCargoComponent::TransferTo(UQuarryCargoComponent* Destination, float RequestedMassKg)
{
	if (!IsValid(Destination) || Destination == this || MaterialType == EQuarryMaterialType::None
		|| CurrentMassKg <= 0.0f || !FMath::IsFinite(RequestedMassKg) || RequestedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	EQuarryMaterialType TransferredMaterial = EQuarryMaterialType::None;
	const float AvailableMassKg = FMath::Min(CurrentMassKg, RequestedMassKg);
	const float ReservedMassKg = FMath::Min(AvailableMassKg, Destination->GetFreeCapacityKg());
	if (ReservedMassKg <= 0.0f || !Destination->CanAcceptMaterial(MaterialType))
	{
		return 0.0f;
	}

	const float AcceptedMassKg = Destination->Load(MaterialType, ReservedMassKg);
	if (AcceptedMassKg <= 0.0f)
	{
		return 0.0f;
	}

	const float RemovedMassKg = Unload(AcceptedMassKg, TransferredMaterial);
	if (!FMath::IsNearlyEqual(RemovedMassKg, AcceptedMassKg))
	{
		Destination->Unload(AcceptedMassKg - RemovedMassKg, TransferredMaterial);
		return RemovedMassKg;
	}

	return RemovedMassKg;
}

void UQuarryCargoComponent::NotifyCargoChanged()
{
	OnCargoChanged.Broadcast(MaterialType, CurrentMassKg, CapacityKg);
}
