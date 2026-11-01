#include "Vehicles/QuarryMiningTruck.h"

#include "Cargo/QuarryCargoComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "EnhancedInputComponent.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/QuarryMaterialPile.h"
#include "Vehicles/QuarryBlockoutComponent.h"
#include "Vehicles/QuarryBlockoutVisuals.h"
#include "Vehicles/QuarryTireComponent.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"
#include "Worksite/QuarryConfigSubsystem.h"
#include "Engine/GameInstance.h"

AQuarryMiningTruck::AQuarryMiningTruck()
{
	PrimaryActorTick.bCanEverTick = true;
	Chassis->InitBoxExtent(FVector(325.0f, 180.0f, 80.0f));
	VehicleMovement->BaseMassKg = 30000.0f;
	VehicleMovement->EngineForce = 140000.0f;
	VehicleMovement->BrakeForce = 240000.0f;
	VehicleMovement->MaxForwardSpeed = 1300.0f;
	VehicleMovement->MaxReverseSpeed = 450.0f;
	VehicleMovement->SetPayloadCenterOfMass(FVector(-80.0, 0.0, 140.0));
	CameraBoom->TargetArmLength = 1400.0f;

	const FLinearColor Orange(0.82f, 0.3f, 0.035f);
	const FLinearColor Highlight(0.98f, 0.52f, 0.06f);
	const FLinearColor Dark(0.025f, 0.03f, 0.035f);
	const FLinearColor Steel(0.12f, 0.15f, 0.17f);
	const FLinearColor Glass(0.025f, 0.11f, 0.16f);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("TruckFrame"), VisualRoot, FVector(650.0, 300.0, 75.0), FVector(0.0, 0.0, 5.0), Dark);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("TruckCab"), VisualRoot, FVector(185.0, 280.0, 175.0), FVector(220.0, 0.0, 110.0), Orange);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("TruckWindshield"), VisualRoot, FVector(18.0, 235.0, 72.0), FVector(316.0, 0.0, 140.0), Glass, false, FRotator(-12.0f, 0.0f, 0.0f));
	FQuarryBlockoutVisuals::AddCube(this, TEXT("TruckRoof"), VisualRoot, FVector(200.0, 295.0, 15.0), FVector(215.0, 0.0, 205.0), Highlight);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("TruckBumper"), VisualRoot, FVector(35.0, 310.0, 45.0), FVector(330.0, 0.0, 45.0), Steel);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("TruckGrille"), VisualRoot, FVector(12.0, 130.0, 55.0), FVector(321.0, 0.0, 85.0), Dark);
	for (float Y : {-90.0f, 90.0f})
	{
		const FName LightName = *FString::Printf(TEXT("Headlight_%d"), FMath::RoundToInt(Y));
		FQuarryBlockoutVisuals::AddCube(this, LightName, VisualRoot, FVector(15.0, 55.0, 30.0), FVector(326.0, Y, 95.0), FLinearColor(1.0f, 0.9f, 0.5f));
	}

	BedPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BedPivot"));
	BedPivot->SetupAttachment(VisualRoot);
	BedPivot->SetRelativeLocation(FVector(-80.0, 0.0, 140.0));
	FQuarryBlockoutVisuals::AddCube(this, TEXT("BedFloor"), BedPivot, FVector(440.0, 315.0, 25.0), FVector(-50.0, 0.0, 0.0), Orange);
	for (float Y : {-150.0f, 150.0f})
	{
		const FName WallName = *FString::Printf(TEXT("BedWall_%d"), FMath::RoundToInt(Y));
		FQuarryBlockoutVisuals::AddCube(this, WallName, BedPivot, FVector(440.0, 20.0, 105.0), FVector(-50.0, Y, 55.0), Orange, false, FRotator(-FMath::Sign(Y) * 10.0f, 0.0f, 0.0f));
		FQuarryBlockoutVisuals::AddCube(this, *FString::Printf(TEXT("BedRail_%d"), FMath::RoundToInt(Y)), BedPivot,
			FVector(445.0, 25.0, 12.0), FVector(-50.0, Y, 112.0), Highlight);
	}
	FQuarryBlockoutVisuals::AddCube(this, TEXT("BedFront"), BedPivot, FVector(25.0, 315.0, 115.0), FVector(165.0, 0.0, 60.0), Highlight);
	BedIntake = CreateDefaultSubobject<USceneComponent>(TEXT("BedIntake"));
	BedIntake->SetupAttachment(BedPivot);
	BedIntake->SetRelativeLocation(FVector(-50.0, 0.0, 100.0));
	DumpPoint = CreateDefaultSubobject<USceneComponent>(TEXT("DumpPoint"));
	DumpPoint->SetupAttachment(BedPivot);
	DumpPoint->SetRelativeLocation(FVector(-280.0, 0.0, 0.0));
	CargoFill = FQuarryBlockoutVisuals::AddCube(this, TEXT("CargoFill"), BedPivot,
		FVector(390.0, 260.0, 5.0), FVector(-50.0, 0.0, 55.0), FLinearColor(0.55f, 0.38f, 0.16f));

	for (float X : {-230.0f, -20.0f, 220.0f})
	{
		for (float Y : {-175.0f, 175.0f})
		{
			const FName WheelName = *FString::Printf(TEXT("Wheel_%d_%d"), FMath::RoundToInt(X), FMath::RoundToInt(Y));
			UQuarryTireComponent* Wheel = CreateDefaultSubobject<UQuarryTireComponent>(WheelName);
			Wheel->SetupAttachment(VisualRoot);
			Wheel->SetRelativeLocation(FVector(X, Y, -55.0));
			Wheel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
			Wheel->Color = Dark;
			FQuarryBlockoutVisuals::AddCylinder(this, *FString::Printf(TEXT("Hub_%d_%d"), FMath::RoundToInt(X), FMath::RoundToInt(Y)),
				Wheel, 28.0f, 50.0f, FVector::ZeroVector, FRotator::ZeroRotator, Steel);
			VehicleMovement->AddWheel(FVector(X, Y, -20.0), 65.0f, true, X > 100.0f, Wheel);
		}
	}

	Cargo = CreateDefaultSubobject<UQuarryCargoComponent>(TEXT("Cargo"));
	Cargo->CapacityKg = 60000.0f;
}

void AQuarryMiningTruck::BeginPlay()
{
	Super::BeginPlay();
	if (const UQuarryConfigSubsystem* Config = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuarryConfigSubsystem>() : nullptr)
	{
		const auto& Vehicle = Config->GetMiningTruckConfig();
		if (Vehicle.base_mass_kg() > 0.0) VehicleMovement->BaseMassKg = Vehicle.base_mass_kg();
		if (Vehicle.engine_force_n() > 0.0) VehicleMovement->EngineForce = Vehicle.engine_force_n();
		if (Vehicle.brake_force_n() > 0.0) VehicleMovement->BrakeForce = Vehicle.brake_force_n();
		if (Vehicle.max_forward_speed_cm_s() > 0.0) VehicleMovement->MaxForwardSpeed = Vehicle.max_forward_speed_cm_s();
		if (Vehicle.max_reverse_speed_cm_s() > 0.0) VehicleMovement->MaxReverseSpeed = Vehicle.max_reverse_speed_cm_s();
		if (Vehicle.cargo_capacity_kg() > 0.0) Cargo->CapacityKg = Vehicle.cargo_capacity_kg();
		if (Vehicle.work_rate_kg_s() > 0.0) UnloadRateKgPerSecond = Vehicle.work_rate_kg_s();
	}
	Cargo->OnCargoChanged.AddDynamic(this, &AQuarryMiningTruck::UpdateCargoVisual);
	UpdateCargoVisual(Cargo->MaterialType, Cargo->CurrentMassKg, Cargo->CapacityKg);
}

void AQuarryMiningTruck::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BedAngle = FMath::FInterpTo(BedAngle, bUnloading ? -35.0f : 0.0f, DeltaSeconds, 3.0f);
	BedPivot->SetRelativeRotation(FRotator(BedAngle, 0.0f, 0.0f));
	if (!bUnloading) return;

	if (!IsNearlyStationary() || !IsValid(UnloadPile))
	{
		StopUnloading();
		OperationStatus = TEXT("UNLOAD CANCELLED");
		return;
	}
	if (UnloadPile->GetDistanceToWorkSurface(DumpPoint->GetComponentLocation()) > UnloadRadius)
	{
		StopUnloading();
		OperationStatus = TEXT("UNLOAD CANCELLED: OUT OF RANGE");
		return;
	}
	if (BedAngle > -20.0f) return;

	const float Offered = FMath::Min(UnloadRateKgPerSecond * DeltaSeconds, Cargo->CurrentMassKg);
	const float Accepted = UnloadPile->DepositAt(DumpPoint->GetComponentLocation(), Cargo->MaterialType, Offered);
	EQuarryMaterialType Material = EQuarryMaterialType::None;
	const float Removed = Accepted > 0.0f ? Cargo->Unload(Accepted, Material) : 0.0f;
	if (Removed <= 0.0f || Cargo->CurrentMassKg <= 0.01f)
	{
		StopUnloading();
		OperationStatus = Cargo->CurrentMassKg <= 0.01f ? TEXT("TRUCK EMPTY") : TEXT("DEPOT FULL");
	}
}

void AQuarryMiningTruck::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (TipAction)
		{
			Input->BindAction(TipAction, ETriggerEvent::Started, this, &AQuarryMiningTruck::ToggleUnloading);
		}
	}
}

void AQuarryMiningTruck::CreateDefaultInputMappings()
{
	Super::CreateDefaultInputMappings();
	if (!TipAction)
	{
		TipAction = NewObject<UInputAction>(this, TEXT("TruckTip"));
		TipAction->ValueType = EInputActionValueType::Boolean;
	}
	DrivingContext->MapKey(TipAction, EKeys::X);
}

FVector AQuarryMiningTruck::GetCargoIntakeLocation() const
{
	return BedIntake ? BedIntake->GetComponentLocation() : GetActorLocation();
}

void AQuarryMiningTruck::ToggleUnloading()
{
	if (bUnloading)
	{
		StopUnloading();
		return;
	}
	if (!IsNearlyStationary())
	{
		OperationStatus = TEXT("STOP TRUCK BEFORE UNLOADING");
		return;
	}
	if (Cargo->CurrentMassKg <= 0.0f)
	{
		OperationStatus = TEXT("TRUCK EMPTY");
		return;
	}
	UnloadPile = FindUnloadPile();
	if (!UnloadPile)
	{
		OperationStatus = TEXT("MOVE TO UNLOADING BAY");
		return;
	}
	bUnloading = true;
	OperationStatus = TEXT("UNLOADING");
}

void AQuarryMiningTruck::StopUnloading()
{
	bUnloading = false;
	UnloadPile = nullptr;
	if (Cargo->CurrentMassKg > 0.01f)
	{
		OperationStatus = TEXT("READY");
	}
}

AQuarryMaterialPile* AQuarryMiningTruck::FindUnloadPile() const
{
	AQuarryMaterialPile* Best = nullptr;
	float BestDistance = UnloadRadius;
	const FVector Origin = DumpPoint->GetComponentLocation();
	for (TActorIterator<AQuarryMaterialPile> It(GetWorld()); It; ++It)
	{
		AQuarryMaterialPile* Pile = *It;
		if (!Pile->bAllowDeposits || !Pile->bCountsAsDelivery || Pile->MaterialType != Cargo->MaterialType) continue;
		const float Distance = Pile->GetDistanceToWorkSurface(Origin);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Pile;
		}
	}
	return Best;
}

float AQuarryMiningTruck::UnloadToNearestPile(float RequestedMassKg)
{
	if (Cargo->CurrentMassKg <= 0.0f || RequestedMassKg <= 0.0f) return 0.0f;
	AQuarryMaterialPile* Pile = FindUnloadPile();
	if (!Pile) return 0.0f;
	const float Offered = FMath::Min(RequestedMassKg, Cargo->CurrentMassKg);
	const float Accepted = Pile->DepositAt(DumpPoint->GetComponentLocation(), Cargo->MaterialType, Offered);
	EQuarryMaterialType Material = EQuarryMaterialType::None;
	return Accepted > 0.0f ? Cargo->Unload(Accepted, Material) : 0.0f;
}

void AQuarryMiningTruck::UpdateCargoVisual(EQuarryMaterialType MaterialType, float CurrentMassKg, float CapacityKg)
{
	const float Ratio = CapacityKg > 0.0f ? CurrentMassKg / CapacityKg : 0.0f;
	CargoFill->SetVisibility(Ratio > 0.001f);
	CargoFill->SetRelativeScale3D(FVector(3.6f, 2.6f, FMath::Max(0.05f, Ratio * 0.7f)));
	VehicleMovement->SetPayloadMassKg(CurrentMassKg);
}

void AQuarryMiningTruck::UnPossessed()
{
	StopUnloading();
	Super::UnPossessed();
}
