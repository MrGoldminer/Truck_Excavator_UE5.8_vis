#include "Vehicles/QuarryVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "QuarryCollisionChannels.h"
#include "Sensors/QuarryImuSensorComponent.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"

namespace
{
	UInputAction* CreateAxisAction(AQuarryVehiclePawn* Owner, const FName Name, const EInputActionValueType ValueType = EInputActionValueType::Axis1D)
	{
		UInputAction* Action = NewObject<UInputAction>(Owner, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	void MapSignedAxis(UInputMappingContext* Context, const UInputAction* Action, const FKey PositiveKey, const FKey NegativeKey)
	{
		Context->MapKey(Action, PositiveKey);
		FEnhancedActionKeyMapping& NegativeMapping = Context->MapKey(Action, NegativeKey);
		NegativeMapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
	}
}

AQuarryVehiclePawn::AQuarryVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Chassis = CreateDefaultSubobject<UBoxComponent>(TEXT("Chassis"));
	SetRootComponent(Chassis);
	Chassis->InitBoxExtent(FVector(250.0f, 160.0f, 70.0f));
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Chassis->SetCollisionProfileName(TEXT("Vehicle"));
		Chassis->SetCollisionResponseToChannel(QuarryCollision::Terrain, ECR_Ignore);
		Chassis->SetSimulatePhysics(true);
		Chassis->SetUseCCD(true);
		// Light damping: rolling resistance, aero drag, and anti-roll bars are
		// modelled explicitly by the force-driven movement component.
		Chassis->SetLinearDamping(0.02f);
		Chassis->SetAngularDamping(0.8f);
	}

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(Chassis);

	VehicleMovement = CreateDefaultSubobject<UQuarryVehicleMovementComponent>(TEXT("VehicleMovement"));
	VehicleMovement->ConfigureChassis(Chassis);

	ImuSensor = CreateDefaultSubobject<UQuarryImuSensorComponent>(TEXT("ImuSensor"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Chassis);
	CameraBoom->TargetArmLength = 1000.0f;
	CameraBoom->SetRelativeRotation(FRotator(-20.0f, 0.0f, 0.0f));
	CameraBoom->bUsePawnControlRotation = true;
	// Horizon-style smooth chase: lagged spring arm, wider cinematic FOV.
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 14.0f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 10.0f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	bUseControllerRotationYaw = false;
}

UPawnMovementComponent* AQuarryVehiclePawn::GetMovementComponent() const
{
	return VehicleMovement;
}

void AQuarryVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	// Park spawned vehicles until a driver takes over.
	SetParkingBrake(true);
}

void AQuarryVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	SetParkingBrake(false);
	if (!DrivingContext)
	{
		DrivingContext = NewObject<UInputMappingContext>(this, TEXT("QuarryDrivingContext"));
		CreateDefaultInputMappings();
	}

	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetInputSubsystem())
	{
		InputSubsystem->AddMappingContext(DrivingContext, 0);
	}

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (ThrottleAction)
		{
			EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &AQuarryVehiclePawn::HandleThrottle);
			EnhancedInput->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &AQuarryVehiclePawn::HandleThrottle);
		}
		if (SteeringAction)
		{
			EnhancedInput->BindAction(SteeringAction, ETriggerEvent::Triggered, this, &AQuarryVehiclePawn::HandleSteering);
			EnhancedInput->BindAction(SteeringAction, ETriggerEvent::Completed, this, &AQuarryVehiclePawn::HandleSteering);
		}
		if (BrakeAction)
		{
			EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &AQuarryVehiclePawn::HandleBrake);
			EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Completed, this, &AQuarryVehiclePawn::HandleBrake);
		}
		if (HandbrakeAction)
		{
			EnhancedInput->BindAction(HandbrakeAction, ETriggerEvent::Started, this, &AQuarryVehiclePawn::SetParkingBrake, true);
			EnhancedInput->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &AQuarryVehiclePawn::SetParkingBrake, false);
		}
		if (LookAction)
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AQuarryVehiclePawn::HandleLook);
		}
	}
}

void AQuarryVehiclePawn::CreateDefaultInputMappings()
{
	if (!ThrottleAction) ThrottleAction = CreateAxisAction(this, TEXT("QuarryThrottle"));
	if (!SteeringAction) SteeringAction = CreateAxisAction(this, TEXT("QuarrySteering"));
	if (!BrakeAction) BrakeAction = CreateAxisAction(this, TEXT("QuarryBrake"));
	if (!HandbrakeAction) HandbrakeAction = CreateAxisAction(this, TEXT("QuarryHandbrake"), EInputActionValueType::Boolean);
	if (!LookAction) LookAction = CreateAxisAction(this, TEXT("QuarryLook"), EInputActionValueType::Axis2D);

	MapSignedAxis(DrivingContext, ThrottleAction, EKeys::W, EKeys::S);
	MapSignedAxis(DrivingContext, SteeringAction, EKeys::D, EKeys::A);
	DrivingContext->MapKey(ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	FEnhancedActionKeyMapping& ReverseMapping = DrivingContext->MapKey(ThrottleAction, EKeys::Gamepad_LeftTriggerAxis);
	ReverseMapping.Modifiers.Add(NewObject<UInputModifierNegate>(DrivingContext));
	DrivingContext->MapKey(SteeringAction, EKeys::Gamepad_LeftX);
	DrivingContext->MapKey(BrakeAction, EKeys::SpaceBar);
	DrivingContext->MapKey(HandbrakeAction, EKeys::LeftShift);
	DrivingContext->MapKey(LookAction, EKeys::Mouse2D);
}

void AQuarryVehiclePawn::SetDriveInput(float Throttle, float Steering)
{
	ThrottleCommand = FMath::Clamp(Throttle, -1.0f, 1.0f);
	SteeringCommand = FMath::Clamp(Steering, -1.0f, 1.0f);
	VehicleMovement->SetDriveInput(ThrottleCommand, SteeringCommand);
}

void AQuarryVehiclePawn::SetServiceBrake(float Brake)
{
	VehicleMovement->SetServiceBrake(Brake);
}

void AQuarryVehiclePawn::SetParkingBrake(bool bEnabled)
{
	VehicleMovement->SetParkingBrake(bEnabled);
}

float AQuarryVehiclePawn::GetForwardSpeed() const
{
	return VehicleMovement ? VehicleMovement->GetForwardSpeed() : 0.0f;
}

bool AQuarryVehiclePawn::IsNearlyStationary() const
{
	return VehicleMovement && VehicleMovement->GetSpeedKmh() < 2.0f;
}

void AQuarryVehiclePawn::HandleThrottle(const FInputActionValue& Value)
{
	SetDriveInput(Value.Get<float>(), SteeringCommand);
}

void AQuarryVehiclePawn::HandleSteering(const FInputActionValue& Value)
{
	SetDriveInput(ThrottleCommand, Value.Get<float>());
}

void AQuarryVehiclePawn::HandleBrake(const FInputActionValue& Value)
{
	SetServiceBrake(Value.Get<float>());
}

void AQuarryVehiclePawn::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Look = Value.Get<FVector2D>();
	AddControllerYawInput(Look.X);
	AddControllerPitchInput(-Look.Y);
}

UEnhancedInputLocalPlayerSubsystem* AQuarryVehiclePawn::GetInputSubsystem() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	return PlayerController && PlayerController->GetLocalPlayer()
		? PlayerController->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

void AQuarryVehiclePawn::RemoveDrivingContext()
{
	if (DrivingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = GetInputSubsystem())
		{
			InputSubsystem->RemoveMappingContext(DrivingContext);
		}
	}
}

void AQuarryVehiclePawn::UnPossessed()
{
	RemoveDrivingContext();
	SetDriveInput(0.0f, 0.0f);
	SetServiceBrake(0.0f);
	SetParkingBrake(true);
	Super::UnPossessed();
}

void AQuarryVehiclePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveDrivingContext();
	Super::EndPlay(EndPlayReason);
}
