#include "Worksite/QuarryWorksiteSubsystem.h"

#include "Materials/QuarryMaterialPile.h"
#include "Protocol/QuarryProtocol.h"

void UQuarryWorksiteSubsystem::RegisterExcavator(AQuarryExcavator* InExcavator)
{
	Excavator = InExcavator;
}

void UQuarryWorksiteSubsystem::RegisterMiningTruck(AQuarryMiningTruck* Truck)
{
	MiningTruck = Truck;
}

void UQuarryWorksiteSubsystem::RegisterDigSite(AQuarryDigSite* InDigSite)
{
	DigSite = InDigSite;
}

void UQuarryWorksiteSubsystem::RegisterDeliveryPile(AQuarryMaterialPile* Pile)
{
	if (DeliveryPile)
	{
		DeliveryPile->OnPileChanged.RemoveDynamic(this, &UQuarryWorksiteSubsystem::HandlePileChanged);
	}
	DeliveryPile = Pile;
	if (DeliveryPile)
	{
		DeliveryPile->OnPileChanged.AddDynamic(this, &UQuarryWorksiteSubsystem::HandlePileChanged);
	}
}

FString UQuarryWorksiteSubsystem::GetNotification() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() <= NotificationExpiresAt ? Notification : FString();
}

void UQuarryWorksiteSubsystem::Notify(const FString& Message, float Duration)
{
	Notification = Message;
	NotificationExpiresAt = GetWorld() ? GetWorld()->GetTimeSeconds() + Duration : 0.0f;
}

void UQuarryWorksiteSubsystem::HandlePileChanged(AQuarryMaterialPile* Pile, float DeltaMassKg, bool bDeposited)
{
	if (Pile == DeliveryPile && bDeposited && DeltaMassKg > 0.0f)
	{
		DeliveredMassKg += DeltaMassKg;
	}
}

TArray<uint8> UQuarryWorksiteSubsystem::GetTelemetryBytes() const
{
	return FQuarryProtocol::EncodeTelemetry(this);
}

bool UQuarryWorksiteSubsystem::ApplyVehicleCommand(const TArray<uint8>& CommandBytes)
{
	return FQuarryProtocol::ApplyVehicleCommand(CommandBytes, this);
}

FString UQuarryWorksiteSubsystem::GetTelemetryDebugString() const
{
	return FQuarryProtocol::DecodeTelemetryForDebug(GetTelemetryBytes());
}

void UQuarryWorksiteSubsystem::Deinitialize()
{
	if (DeliveryPile)
	{
		DeliveryPile->OnPileChanged.RemoveDynamic(this, &UQuarryWorksiteSubsystem::HandlePileChanged);
	}
	Super::Deinitialize();
}
