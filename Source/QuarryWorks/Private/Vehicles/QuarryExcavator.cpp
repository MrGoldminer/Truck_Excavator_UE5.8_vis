#include "Vehicles/QuarryExcavator.h"

#include "Cargo/QuarryCargoComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Materials/QuarryMaterialPile.h"
#include "Vehicles/QuarryBlockoutComponent.h"
#include "Vehicles/QuarryBlockoutVisuals.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"
#include "Worksite/QuarryConfigSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
	UInputAction* CreateExcavatorAction(AQuarryExcavator* Owner, const FName Name, const EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Owner, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	void MapExcavatorAxis(UInputMappingContext* Context, const UInputAction* Action, const FKey PositiveKey, const FKey NegativeKey)
	{
		Context->MapKey(Action, PositiveKey);
		FEnhancedActionKeyMapping& NegativeMapping = Context->MapKey(Action, NegativeKey);
		NegativeMapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}
}

AQuarryExcavator::AQuarryExcavator()
{
	PrimaryActorTick.bCanEverTick = true;
	Chassis->InitBoxExtent(FVector(250.0f, 170.0f, 65.0f));
	VehicleMovement->DriveType = EQuarryDriveType::Tracked;
	VehicleMovement->BaseMassKg = 24000.0f;
	VehicleMovement->EngineForce = 100000.0f;
	VehicleMovement->BrakeForce = 180000.0f;
	VehicleMovement->MaxForwardSpeed = 500.0f;
	VehicleMovement->MaxReverseSpeed = 350.0f;
	VehicleMovement->SetPayloadCenterOfMass(FVector(300.0, 0.0, 100.0));
	CameraBoom->TargetArmLength = 1200.0f;

	const FLinearColor Yellow(0.78f, 0.4f, 0.035f);
	const FLinearColor Highlight(0.98f, 0.65f, 0.08f);
	const FLinearColor Dark(0.025f, 0.03f, 0.035f);
	const FLinearColor Steel(0.12f, 0.15f, 0.17f);
	const FLinearColor Glass(0.025f, 0.11f, 0.16f);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("LowerBody"), VisualRoot, FVector(400.0, 280.0, 80.0), FVector(0.0, 0.0, 10.0), Yellow);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("SlewRing"), VisualRoot, FVector(220.0, 220.0, 25.0), FVector(0.0, 0.0, 65.0), Steel);
	for (float Y : {-155.0f, 155.0f})
	{
		const FName TrackName = *FString::Printf(TEXT("Track_%d"), FMath::RoundToInt(Y));
		FQuarryBlockoutVisuals::AddCube(this, TrackName, VisualRoot, FVector(450.0, 85.0, 100.0), FVector(0.0, Y, -35.0), Dark);
		for (float X : {-180.0f, -90.0f, 0.0f, 90.0f, 180.0f})
		{
			const FName WheelName = *FString::Printf(TEXT("TrackWheel_%d_%d"), FMath::RoundToInt(X), FMath::RoundToInt(Y));
			FQuarryBlockoutVisuals::AddCylinder(this, WheelName, VisualRoot, 36.0f, 18.0f,
				FVector(X, Y + FMath::Sign(Y) * 48.0f, -35.0), FRotator(90.0f, 0.0f, 0.0f), Steel);
		}
	}
	for (float X : {-180.0f, 180.0f})
	{
		for (float Y : {-140.0f, 140.0f})
		{
			VehicleMovement->AddWheel(FVector(X, Y, -20.0), 55.0f, true, false, nullptr);
		}
	}

	UpperStructurePivot = CreateDefaultSubobject<USceneComponent>(TEXT("UpperStructurePivot"));
	UpperStructurePivot->SetupAttachment(VisualRoot);
	UpperStructurePivot->SetRelativeLocation(FVector(0.0, 0.0, 120.0));
	FQuarryBlockoutVisuals::AddCube(this, TEXT("UpperBody"), UpperStructurePivot, FVector(260.0, 250.0, 90.0), FVector(-40.0, 0.0, 35.0), Yellow);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("Counterweight"), UpperStructurePivot, FVector(90.0, 255.0, 100.0), FVector(-155.0, 0.0, 40.0), Highlight);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("Cab"), UpperStructurePivot, FVector(130.0, 105.0, 125.0), FVector(25.0, -72.0, 100.0), Yellow);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("CabGlass"), UpperStructurePivot, FVector(105.0, 108.0, 75.0), FVector(35.0, -72.0, 120.0), Glass, false, FRotator(-8.0f, 0.0f, 0.0f));
	FQuarryBlockoutVisuals::AddCube(this, TEXT("CabRoof"), UpperStructurePivot, FVector(145.0, 120.0, 12.0), FVector(25.0, -72.0, 168.0), Dark);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("CabLight"), UpperStructurePivot, FVector(25.0, 35.0, 18.0), FVector(95.0, -72.0, 160.0), FLinearColor(1.0f, 0.9f, 0.45f));
	FQuarryBlockoutVisuals::AddCylinder(this, TEXT("Exhaust"), UpperStructurePivot, 12.0f, 100.0f, FVector(-105.0, 75.0, 115.0), FRotator::ZeroRotator, Dark);

	BoomPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BoomPivot"));
	BoomPivot->SetupAttachment(UpperStructurePivot);
	BoomPivot->SetRelativeLocation(FVector(100.0, 0.0, 80.0));
	FQuarryBlockoutVisuals::AddCube(this, TEXT("Boom"), BoomPivot, FVector(300.0, 58.0, 58.0), FVector(150.0, 0.0, 0.0), Yellow);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("BoomHighlight"), BoomPivot, FVector(240.0, 62.0, 12.0), FVector(140.0, 0.0, 25.0), Highlight);
	FQuarryBlockoutVisuals::AddCylinder(this, TEXT("BoomHydraulic"), BoomPivot, 13.0f, 190.0f, FVector(80.0, -48.0f, 15.0), FRotator(55.0f, 0.0f, 0.0f), Steel);

	StickPivot = CreateDefaultSubobject<USceneComponent>(TEXT("StickPivot"));
	StickPivot->SetupAttachment(BoomPivot);
	StickPivot->SetRelativeLocation(FVector(300.0, 0.0, 0.0));
	FQuarryBlockoutVisuals::AddCube(this, TEXT("Stick"), StickPivot, FVector(220.0, 45.0, 45.0), FVector(110.0, 0.0, 0.0), Yellow);
	FQuarryBlockoutVisuals::AddCylinder(this, TEXT("StickHydraulic"), StickPivot, 11.0f, 150.0f, FVector(85.0, 35.0f, 20.0), FRotator(60.0f, 0.0f, 0.0f), Steel);

	BucketPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BucketPivot"));
	BucketPivot->SetupAttachment(StickPivot);
	BucketPivot->SetRelativeLocation(FVector(220.0, 0.0, 0.0));
	FQuarryBlockoutVisuals::AddCube(this, TEXT("BucketBottom"), BucketPivot, FVector(100.0, 115.0, 14.0), FVector(20.0, 0.0, -38.0), Dark);
	FQuarryBlockoutVisuals::AddCube(this, TEXT("BucketBack"), BucketPivot, FVector(18.0, 115.0, 70.0), FVector(-25.0, 0.0, -8.0), Steel);
	for (float Y : {-52.0f, 52.0f})
	{
		const FName SideName = *FString::Printf(TEXT("BucketSide_%d"), FMath::RoundToInt(Y));
		FQuarryBlockoutVisuals::AddCube(this, SideName, BucketPivot, FVector(95.0, 12.0, 65.0), FVector(20.0, Y, -8.0), Steel);
	}
	for (float Y : {-38.0f, 0.0f, 38.0f})
	{
		const FName ToothName = *FString::Printf(TEXT("BucketTooth_%d"), FMath::RoundToInt(Y));
		FQuarryBlockoutVisuals::AddCube(this, ToothName, BucketPivot, FVector(32.0, 20.0, 14.0), FVector(75.0, Y, -42.0), Dark);
	}
	BucketTip = CreateDefaultSubobject<USceneComponent>(TEXT("BucketTip"));
	BucketTip->SetupAttachment(BucketPivot);
	BucketTip->SetRelativeLocation(FVector(80.0, 0.0, -35.0));
	BucketFill = FQuarryBlockoutVisuals::AddCube(this, TEXT("BucketFill"), BucketPivot,
		FVector(70.0, 85.0, 5.0), FVector(20.0, 0.0, -20.0), FLinearColor(0.55f, 0.38f, 0.16f));

	BucketCargo = CreateDefaultSubobject<UQuarryCargoComponent>(TEXT("BucketCargo"));
	BucketCargo->CapacityKg = ScoopMassKg;
	UpperStructurePivot->SetRelativeRotation(FRotator(0.0f, SlewAngle, 0.0f));
	BoomPivot->SetRelativeRotation(FRotator(BoomAngle, 0.0f, 0.0f));
	StickPivot->SetRelativeRotation(FRotator(StickAngle, 0.0f, 0.0f));
	BucketPivot->SetRelativeRotation(FRotator(BucketAngle, 0.0f, 0.0f));
}

void AQuarryExcavator::BeginPlay()
{
	Super::BeginPlay();
	if (const UQuarryConfigSubsystem* Config = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuarryConfigSubsystem>() : nullptr)
	{
		const auto& Vehicle = Config->GetExcavatorConfig();
		if (Vehicle.base_mass_kg() > 0.0) VehicleMovement->BaseMassKg = Vehicle.base_mass_kg();
		if (Vehicle.engine_force_n() > 0.0) VehicleMovement->EngineForce = Vehicle.engine_force_n();
		if (Vehicle.brake_force_n() > 0.0) VehicleMovement->BrakeForce = Vehicle.brake_force_n();
		if (Vehicle.max_forward_speed_cm_s() > 0.0) VehicleMovement->MaxForwardSpeed = Vehicle.max_forward_speed_cm_s();
		if (Vehicle.max_reverse_speed_cm_s() > 0.0) VehicleMovement->MaxReverseSpeed = Vehicle.max_reverse_speed_cm_s();
		if (Vehicle.cargo_capacity_kg() > 0.0) BucketCargo->CapacityKg = Vehicle.cargo_capacity_kg();
		if (Vehicle.work_rate_kg_s() > 0.0) WorkRateKgPerSecond = Vehicle.work_rate_kg_s();
		ScoopMassKg = BucketCargo->CapacityKg;
	}
	BucketCargo->OnCargoChanged.AddDynamic(this, &AQuarryExcavator::UpdateCargoVisual);
	UpdateCargoVisual(BucketCargo->MaterialType, BucketCargo->CurrentMassKg, BucketCargo->CapacityKg);
}

void AQuarryExcavator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SlewAngle = FMath::UnwindDegrees(SlewAngle + SlewCommand * SlewSpeedDegreesPerSecond * DeltaSeconds);
	BoomAngle = FMath::Clamp(BoomAngle + BoomCommand * HydraulicSpeedDegreesPerSecond * DeltaSeconds, -45.0f, 65.0f);
	StickAngle = FMath::Clamp(StickAngle + StickCommand * HydraulicSpeedDegreesPerSecond * DeltaSeconds, -130.0f, 25.0f);
	BucketAngle = FMath::Clamp(BucketAngle + BucketCommand * HydraulicSpeedDegreesPerSecond * DeltaSeconds, -110.0f, 85.0f);
	UpperStructurePivot->SetRelativeRotation(FRotator(0.0f, SlewAngle, 0.0f));
	BoomPivot->SetRelativeRotation(FRotator(BoomAngle, 0.0f, 0.0f));
	StickPivot->SetRelativeRotation(FRotator(StickAngle, 0.0f, 0.0f));
	BucketPivot->SetRelativeRotation(FRotator(BucketAngle, 0.0f, 0.0f));
	TickWork(DeltaSeconds);
}

void AQuarryExcavator::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input) return;
	if (SlewAction)
	{
		Input->BindAction(SlewAction, ETriggerEvent::Triggered, this, &AQuarryExcavator::HandleSlew);
		Input->BindAction(SlewAction, ETriggerEvent::Completed, this, &AQuarryExcavator::HandleSlew);
	}
	if (BoomAction)
	{
		Input->BindAction(BoomAction, ETriggerEvent::Triggered, this, &AQuarryExcavator::HandleBoom);
		Input->BindAction(BoomAction, ETriggerEvent::Completed, this, &AQuarryExcavator::HandleBoom);
	}
	if (StickAction)
	{
		Input->BindAction(StickAction, ETriggerEvent::Triggered, this, &AQuarryExcavator::HandleStick);
		Input->BindAction(StickAction, ETriggerEvent::Completed, this, &AQuarryExcavator::HandleStick);
	}
	if (BucketAction)
	{
		Input->BindAction(BucketAction, ETriggerEvent::Triggered, this, &AQuarryExcavator::HandleBucket);
		Input->BindAction(BucketAction, ETriggerEvent::Completed, this, &AQuarryExcavator::HandleBucket);
	}
	if (DigAction)
	{
		Input->BindAction(DigAction, ETriggerEvent::Started, this, &AQuarryExcavator::StartDigging);
		Input->BindAction(DigAction, ETriggerEvent::Completed, this, &AQuarryExcavator::StopDigging);
	}
	if (DumpAction)
	{
		Input->BindAction(DumpAction, ETriggerEvent::Started, this, &AQuarryExcavator::StartDumping);
		Input->BindAction(DumpAction, ETriggerEvent::Completed, this, &AQuarryExcavator::StopDumping);
	}
}

void AQuarryExcavator::CreateDefaultInputMappings()
{
	Super::CreateDefaultInputMappings();
	if (!SlewAction) SlewAction = CreateExcavatorAction(this, TEXT("ExcavatorSlew"), EInputActionValueType::Axis1D);
	if (!BoomAction) BoomAction = CreateExcavatorAction(this, TEXT("ExcavatorBoom"), EInputActionValueType::Axis1D);
	if (!StickAction) StickAction = CreateExcavatorAction(this, TEXT("ExcavatorStick"), EInputActionValueType::Axis1D);
	if (!BucketAction) BucketAction = CreateExcavatorAction(this, TEXT("ExcavatorBucket"), EInputActionValueType::Axis1D);
	if (!DigAction) DigAction = CreateExcavatorAction(this, TEXT("ExcavatorDig"), EInputActionValueType::Boolean);
	if (!DumpAction) DumpAction = CreateExcavatorAction(this, TEXT("ExcavatorDump"), EInputActionValueType::Boolean);
	MapExcavatorAxis(DrivingContext, SlewAction, EKeys::E, EKeys::Q);
	MapExcavatorAxis(DrivingContext, BoomAction, EKeys::R, EKeys::F);
	MapExcavatorAxis(DrivingContext, StickAction, EKeys::T, EKeys::G);
	MapExcavatorAxis(DrivingContext, BucketAction, EKeys::Y, EKeys::H);
	DrivingContext->MapKey(DigAction, EKeys::LeftMouseButton);
	DrivingContext->MapKey(DumpAction, EKeys::RightMouseButton);
}

void AQuarryExcavator::StartDigging()
{
	if (WorkState != EQuarryExcavatorWorkState::Idle || !IsNearlyStationary())
	{
		OperationStatus = TEXT("STOP VEHICLE BEFORE WORKING");
		return;
	}
	if (BucketCargo->GetFreeCapacityKg() <= 0.0f)
	{
		OperationStatus = TEXT("BUCKET FULL");
		return;
	}
	WorkPile = FindNearestPile(WorkRadius);
	if (!WorkPile)
	{
		OperationStatus = TEXT("MOVE BUCKET TO SAND PILE");
		return;
	}
	WorkState = EQuarryExcavatorWorkState::Digging;
	OperationStatus = TEXT("DIGGING");
}

void AQuarryExcavator::StopDigging()
{
	if (WorkState == EQuarryExcavatorWorkState::Digging)
	{
		WorkState = EQuarryExcavatorWorkState::Idle;
		OperationStatus = TEXT("READY");
	}
	WorkPile = nullptr;
}

void AQuarryExcavator::StartDumping()
{
	if (WorkState != EQuarryExcavatorWorkState::Idle || !IsNearlyStationary())
	{
		OperationStatus = TEXT("STOP VEHICLE BEFORE WORKING");
		return;
	}
	if (BucketCargo->CurrentMassKg <= 0.0f)
	{
		OperationStatus = TEXT("BUCKET EMPTY");
		return;
	}
	WorkTruck = FindNearestTruck(WorkRadius);
	if (!WorkTruck)
	{
		OperationStatus = TEXT("MOVE BUCKET ABOVE TRUCK BED");
		return;
	}
	WorkState = EQuarryExcavatorWorkState::Dumping;
	OperationStatus = TEXT("LOADING TRUCK");
}

void AQuarryExcavator::StopDumping()
{
	if (WorkState == EQuarryExcavatorWorkState::Dumping)
	{
		WorkState = EQuarryExcavatorWorkState::Idle;
		OperationStatus = TEXT("READY");
	}
	WorkTruck = nullptr;
}

void AQuarryExcavator::TickWork(float DeltaSeconds)
{
	if (WorkState == EQuarryExcavatorWorkState::Idle) return;
	if (!IsNearlyStationary())
	{
		StopDigging();
		StopDumping();
		OperationStatus = TEXT("WORK CANCELLED: VEHICLE MOVING");
		return;
	}

	if (WorkState == EQuarryExcavatorWorkState::Digging)
	{
		if (!IsValid(WorkPile) || WorkPile->GetDistanceToWorkSurface(BucketTip->GetComponentLocation()) > WorkRadius)
		{
			StopDigging();
			OperationStatus = TEXT("WORK CANCELLED: OUT OF RANGE");
			return;
		}
		const float Requested = FMath::Min(WorkRateKgPerSecond * DeltaSeconds, BucketCargo->GetFreeCapacityKg());
		const float Extracted = WorkPile->ExtractAt(BucketTip->GetComponentLocation(), Requested);
		const float Accepted = BucketCargo->Load(WorkPile->MaterialType, Extracted);
		if (Accepted < Extracted)
		{
			WorkPile->DepositAt(BucketTip->GetComponentLocation(), WorkPile->MaterialType, Extracted - Accepted);
		}
		if (Accepted <= 0.0f || BucketCargo->GetFreeCapacityKg() <= 0.01f)
		{
			StopDigging();
			OperationStatus = TEXT("BUCKET FULL");
		}
		return;
	}

	if (!IsValid(WorkTruck) || FVector::Dist(BucketTip->GetComponentLocation(), WorkTruck->GetCargoIntakeLocation()) > WorkRadius)
	{
		StopDumping();
		OperationStatus = TEXT("WORK CANCELLED: OUT OF RANGE");
		return;
	}
	const float Transferred = BucketCargo->TransferTo(WorkTruck->Cargo, WorkRateKgPerSecond * DeltaSeconds);
	if (Transferred <= 0.0f || BucketCargo->CurrentMassKg <= 0.01f)
	{
		StopDumping();
		OperationStatus = BucketCargo->CurrentMassKg <= 0.01f ? TEXT("BUCKET EMPTY") : TEXT("TRUCK FULL");
	}
}

AQuarryMaterialPile* AQuarryExcavator::FindNearestPile(float Radius) const
{
	AQuarryMaterialPile* Best = nullptr;
	float BestDistance = Radius;
	const FVector Origin = BucketTip->GetComponentLocation();
	for (TActorIterator<AQuarryMaterialPile> It(GetWorld()); It; ++It)
	{
		AQuarryMaterialPile* Pile = *It;
		if ((!Pile->bInfiniteSource && Pile->RemainingMassKg <= 0.0f) || !BucketCargo->CanAcceptMaterial(Pile->MaterialType)) continue;
		const float Distance = Pile->GetDistanceToWorkSurface(Origin);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Pile;
		}
	}
	return Best;
}

AQuarryMiningTruck* AQuarryExcavator::FindNearestTruck(float Radius) const
{
	AQuarryMiningTruck* Best = nullptr;
	float BestDistance = Radius;
	const FVector Origin = BucketTip->GetComponentLocation();
	for (TActorIterator<AQuarryMiningTruck> It(GetWorld()); It; ++It)
	{
		AQuarryMiningTruck* Truck = *It;
		if (!Truck->Cargo || !Truck->Cargo->CanAcceptMaterial(BucketCargo->MaterialType)) continue;
		const float Distance = FVector::Dist(Origin, Truck->GetCargoIntakeLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Truck;
		}
	}
	return Best;
}

float AQuarryExcavator::DigFromNearestPile()
{
	AQuarryMaterialPile* Pile = FindNearestPile(WorkRadius);
	if (!Pile) return 0.0f;
	const float Requested = FMath::Min(ScoopMassKg, BucketCargo->GetFreeCapacityKg());
	const float Extracted = Pile->ExtractAt(BucketTip->GetComponentLocation(), Requested);
	const float Accepted = BucketCargo->Load(Pile->MaterialType, Extracted);
	if (Accepted < Extracted)
	{
		Pile->DepositAt(BucketTip->GetComponentLocation(), Pile->MaterialType, Extracted - Accepted);
	}
	return Accepted;
}

float AQuarryExcavator::DumpIntoNearestTruck()
{
	AQuarryMiningTruck* Truck = FindNearestTruck(WorkRadius);
	return Truck ? BucketCargo->TransferTo(Truck->Cargo, BucketCargo->CurrentMassKg) : 0.0f;
}

void AQuarryExcavator::UpdateCargoVisual(EQuarryMaterialType MaterialType, float CurrentMassKg, float CapacityKg)
{
	const float Ratio = CapacityKg > 0.0f ? CurrentMassKg / CapacityKg : 0.0f;
	BucketFill->SetVisibility(Ratio > 0.001f);
	BucketFill->SetRelativeScale3D(FVector(0.6f, 0.8f, FMath::Max(0.05f, Ratio * 0.45f)));
	VehicleMovement->SetPayloadMassKg(CurrentMassKg);
}

void AQuarryExcavator::UnPossessed()
{
	StopDigging();
	StopDumping();
	Super::UnPossessed();
}

void AQuarryExcavator::HandleSlew(const FInputActionValue& Value) { SlewCommand = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f); }
void AQuarryExcavator::HandleBoom(const FInputActionValue& Value) { BoomCommand = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f); }
void AQuarryExcavator::HandleStick(const FInputActionValue& Value) { StickCommand = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f); }
void AQuarryExcavator::HandleBucket(const FInputActionValue& Value) { BucketCommand = FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f); }
