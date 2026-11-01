#include "Worksite/QuarryHUD.h"

#include "Cargo/QuarryCargoComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Materials/QuarryMaterialPile.h"
#include "Terrain/QuarryDigSite.h"
#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"
#include "Worksite/QuarryWorksiteSubsystem.h"

void AQuarryHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner) return;

	UQuarryWorksiteSubsystem* Subsystem = GetWorld()->GetSubsystem<UQuarryWorksiteSubsystem>();
	AQuarryVehiclePawn* Vehicle = Cast<AQuarryVehiclePawn>(PlayerOwner->GetPawn());
	if (!Subsystem || !Vehicle) return;

	DrawRect(FLinearColor(0.015f, 0.02f, 0.025f, 0.82f), 20.0f, 20.0f, 390.0f, 245.0f);
	DrawPanelLine(Cast<AQuarryExcavator>(Vehicle) ? TEXT("EXCAVATOR") : TEXT("MINING TRUCK"), 34.0f,
		FLinearColor(0.95f, 0.65f, 0.1f));
	DrawPanelLine(FString::Printf(TEXT("Speed: %.1f km/h   Gear: %s"),
		Vehicle->VehicleMovement->GetSpeedKmh(), Vehicle->GetForwardSpeed() < -5.0f ? TEXT("R") : TEXT("D")), 62.0f);
	DrawPanelLine(FString::Printf(TEXT("Parking brake: %s"),
		Vehicle->VehicleMovement->IsParkingBrakeEnabled() ? TEXT("ON") : TEXT("OFF")), 88.0f);

	if (AQuarryExcavator* Excavator = Cast<AQuarryExcavator>(Vehicle))
	{
		DrawPanelLine(FString::Printf(TEXT("Bucket: %.0f / %.0f kg"),
			Excavator->BucketCargo->CurrentMassKg, Excavator->BucketCargo->CapacityKg), 114.0f);
		DrawPanelLine(Excavator->GetOperationStatus(), 140.0f, FLinearColor(0.4f, 0.85f, 1.0f));
	}
	if (AQuarryMiningTruck* Truck = Cast<AQuarryMiningTruck>(Vehicle))
	{
		DrawPanelLine(FString::Printf(TEXT("Truck bed: %.0f / %.0f kg"),
			Truck->Cargo->CurrentMassKg, Truck->Cargo->CapacityKg), 114.0f);
		DrawPanelLine(Truck->GetOperationStatus(), 140.0f, FLinearColor(0.4f, 0.85f, 1.0f));
	}
	if (Subsystem->GetMiningTruck())
	{
		DrawPanelLine(FString::Printf(TEXT("Truck total: %.1f t"),
			Subsystem->GetMiningTruck()->Cargo->CurrentMassKg / 1000.0f), 174.0f);
	}
	DrawPanelLine(FString::Printf(TEXT("Delivered: %.2f t"), Subsystem->GetDeliveredMassKg() / 1000.0f), 202.0f,
		FLinearColor(0.35f, 1.0f, 0.45f));
	DrawPanelLine(TEXT("Ctrl+1/2 or V: switch vehicle"), 230.0f, FLinearColor(0.65f, 0.65f, 0.65f));

	DrawRect(FLinearColor(0.015f, 0.02f, 0.025f, 0.78f), 20.0f, Canvas->SizeY - 82.0f, 850.0f, 60.0f);
	DrawText(TEXT("W/S drive  A/D steer  Space brake  Shift parking  Mouse camera"),
		FLinearColor::White, 32.0f, Canvas->SizeY - 70.0f, GEngine->GetSmallFont(), 1.0f);
	DrawText(Cast<AQuarryExcavator>(Vehicle)
		? TEXT("Q/E slew  R/F boom  T/G stick  Y/H bucket  LMB dig  RMB load")
		: TEXT("X: raise bed / unload"),
		FLinearColor::White, 32.0f, Canvas->SizeY - 46.0f, GEngine->GetSmallFont(), 1.0f);

	const FString Notification = Subsystem->GetNotification();
	if (!Notification.IsEmpty())
	{
		DrawRect(FLinearColor(0.015f, 0.02f, 0.025f, 0.85f), Canvas->SizeX * 0.5f - 230.0f, 24.0f, 460.0f, 42.0f);
		DrawText(Notification, FLinearColor(1.0f, 0.8f, 0.2f), Canvas->SizeX * 0.5f - 215.0f, 35.0f,
			GEngine->GetSmallFont(), 1.2f);
	}

	if (Subsystem->GetDigSite())
	{
		DrawWorldMarker(TEXT("DIG SITE"), Subsystem->GetDigSite()->GetActorLocation() + FVector(0.0, 0.0, 300.0),
			FLinearColor(1.0f, 0.7f, 0.1f));
	}
	if (Subsystem->GetDeliveryPile())
	{
		DrawWorldMarker(TEXT("UNLOADING BAY"), Subsystem->GetDeliveryPile()->GetActorLocation() + FVector(0.0, 0.0, 250.0),
			FLinearColor(0.2f, 1.0f, 0.35f));
	}
}

void AQuarryHUD::DrawPanelLine(const FString& Text, float Y, const FLinearColor& Color)
{
	DrawText(Text, Color, 34.0f, Y, GEngine->GetSmallFont(), 1.15f);
}

void AQuarryHUD::DrawWorldMarker(const FString& Label, const FVector& Location, const FLinearColor& Color)
{
	const FVector Screen = Project(Location);
	if (Screen.Z > 0.0f && Screen.X > 0.0f && Screen.X < Canvas->SizeX && Screen.Y > 0.0f && Screen.Y < Canvas->SizeY)
	{
		DrawText(Label, Color, Screen.X - 45.0f, Screen.Y, GEngine->GetSmallFont(), 1.0f);
	}
}
