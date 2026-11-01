#include "Worksite/QuarryPlayerController.h"

#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Worksite/QuarryWorksiteSubsystem.h"

AQuarryPlayerController::AQuarryPlayerController()
{
	bShowMouseCursor = false;
}

void AQuarryPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
}

void AQuarryPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AQuarryPlayerController::SwitchVehicle);
	InputComponent->BindKey(FInputChord(EKeys::One, EModifierKey::Control), IE_Pressed, this, &AQuarryPlayerController::SelectExcavator);
	InputComponent->BindKey(FInputChord(EKeys::NumPadOne, EModifierKey::Control), IE_Pressed, this, &AQuarryPlayerController::SelectExcavator);
	InputComponent->BindKey(FInputChord(EKeys::Two, EModifierKey::Control), IE_Pressed, this, &AQuarryPlayerController::SelectMiningTruck);
	InputComponent->BindKey(FInputChord(EKeys::NumPadTwo, EModifierKey::Control), IE_Pressed, this, &AQuarryPlayerController::SelectMiningTruck);
}

void AQuarryPlayerController::SwitchVehicle()
{
	UQuarryWorksiteSubsystem* Subsystem = GetWorld()->GetSubsystem<UQuarryWorksiteSubsystem>();
	if (!Subsystem) return;

	AQuarryVehiclePawn* Target = Cast<AQuarryExcavator>(GetPawn())
		? static_cast<AQuarryVehiclePawn*>(Subsystem->GetMiningTruck())
		: static_cast<AQuarryVehiclePawn*>(Subsystem->GetExcavator());
	PossessVehicle(Target);
}

void AQuarryPlayerController::SelectExcavator()
{
	if (UQuarryWorksiteSubsystem* Subsystem = GetWorld()->GetSubsystem<UQuarryWorksiteSubsystem>())
	{
		PossessVehicle(Subsystem->GetExcavator());
	}
}

void AQuarryPlayerController::SelectMiningTruck()
{
	if (UQuarryWorksiteSubsystem* Subsystem = GetWorld()->GetSubsystem<UQuarryWorksiteSubsystem>())
	{
		PossessVehicle(Subsystem->GetMiningTruck());
	}
}

void AQuarryPlayerController::PossessVehicle(AQuarryVehiclePawn* Target)
{
	if (!Target || Target == GetPawn()) return;

	UQuarryWorksiteSubsystem* Subsystem = GetWorld()->GetSubsystem<UQuarryWorksiteSubsystem>();
	if (GetPawn() && FVector::Dist(GetPawn()->GetActorLocation(), Target->GetActorLocation()) > 1500.0f)
	{
		if (Subsystem) Subsystem->Notify(TEXT("MOVE CLOSER TO SWITCH VEHICLE"));
		return;
	}

	APawn* PreviousPawn = GetPawn();
	Possess(Target);
	Target->SetParkingBrake(false);
	if (Subsystem)
	{
		Subsystem->Notify(Cast<AQuarryExcavator>(Target) ? TEXT("EXCAVATOR") : TEXT("MINING TRUCK"));
	}
	if (PreviousPawn)
	{
		PreviousPawn->SetActorTickEnabled(true);
	}
}
