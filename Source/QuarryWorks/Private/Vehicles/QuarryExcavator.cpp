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
#include "Vehicles/QuarryVehicleMeshComponent.h"
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

// ============================================================================
// AQuarryExcavator 构造 —— 轮式挖掘机的全部组件装配
// 视觉：分件真实资产（车身/驾驶室/门/4 轮/动臂/小臂/铲斗）由
//        RealVehicleMesh 在 BeginPlay 挂载；以下方块件全部会被隐藏
// 交互：铰接链 BoomPivot→StickPivot→BucketPivot + BucketTip（挖掘采样）
// ============================================================================
AQuarryExcavator::AQuarryExcavator()
{
	// 挖掘机需要每帧更新（动臂角度/挖掘判定/行驶输入）
	PrimaryActorTick.bCanEverTick = true;
	// 物理底盘盒半尺寸 250×170×65 cm（碰撞与质心的载体）
	Chassis->InitBoxExtent(FVector(250.0f, 170.0f, 65.0f));
	VehicleMovement->DriveType = EQuarryDriveType::Tracked;
	VehicleMovement->BaseMassKg = 24000.0f;
	VehicleMovement->EngineForce = 100000.0f;
	VehicleMovement->BrakeForce = 180000.0f;
	VehicleMovement->MaxForwardSpeed = 500.0f;
	VehicleMovement->MaxReverseSpeed = 350.0f;
	VehicleMovement->SetPayloadCenterOfMass(FVector(300.0, 0.0, 100.0));
	// Hydraulic skid steering: full pivot turns, low ground pressure.
	VehicleMovement->TrackYawRateDegPerSecond = 45.0f;
	VehicleMovement->GroundFriction = 0.95f;
	VehicleMovement->LooseMaterialFriction = 0.4f;
	Chassis->SetAngularDamping(1.2f);
	CameraBoom->TargetArmLength = 1200.0f;

	// Every blockout piece is collected so BeginPlay can hide them all once
	// the real machine mesh loads (the dig arm stays functional invisibly:
	// BucketTip is a scene component and keeps articulating).
	auto AddBlockoutCube = [this](FName Name, USceneComponent* Parent, FVector Size, FVector Location,
		FLinearColor Color, bool bRuntime = false, FRotator Rotation = FRotator::ZeroRotator) -> UQuarryBlockoutComponent*
	{
		UQuarryBlockoutComponent* Part = FQuarryBlockoutVisuals::AddCube(this, Name, Parent, Size, Location, Color, bRuntime, Rotation);
		if (Part)
		{
			ReplacedVisualParts.Add(Part);
		}
		return Part;
	};
	auto AddBlockoutCylinder = [this](FName Name, USceneComponent* Parent, float Radius, float Length,
		const FVector& Location, const FRotator& Rotation, const FLinearColor& Color, bool bRuntime = false) -> UQuarryBlockoutComponent*
	{
		UQuarryBlockoutComponent* Part = FQuarryBlockoutVisuals::AddCylinder(this, Name, Parent, Radius, Length, Location, Rotation, Color, bRuntime);
		if (Part)
		{
			ReplacedVisualParts.Add(Part);
		}
		return Part;
	};
	const FLinearColor Yellow(0.78f, 0.4f, 0.035f);
	const FLinearColor Highlight(0.98f, 0.65f, 0.08f);
	const FLinearColor Dark(0.025f, 0.03f, 0.035f);
	const FLinearColor Steel(0.12f, 0.15f, 0.17f);
	const FLinearColor Glass(0.025f, 0.11f, 0.16f);
	AddBlockoutCube(TEXT("LowerBody"), VisualRoot, FVector(400.0, 280.0, 80.0), FVector(0.0, 0.0, 10.0), Yellow);
	AddBlockoutCube(TEXT("SlewRing"), VisualRoot, FVector(220.0, 220.0, 25.0), FVector(0.0, 0.0, 65.0), Steel);
	for (float Y : {-155.0f, 155.0f})
	{
		const FName TrackName = *FString::Printf(TEXT("Track_%d"), FMath::RoundToInt(Y));
		UQuarryBlockoutComponent* TrackBlock = FQuarryBlockoutVisuals::AddCube(this, TrackName, VisualRoot, FVector(450.0, 85.0, 100.0), FVector(0.0, Y, -35.0), Dark);
		if (TrackBlock)
		{
			ReplacedVisualParts.Add(TrackBlock);
		}
		for (float X : {-180.0f, -90.0f, 0.0f, 90.0f, 180.0f})
		{
			const FName WheelName = *FString::Printf(TEXT("TrackWheel_%d_%d"), FMath::RoundToInt(X), FMath::RoundToInt(Y));
			UQuarryBlockoutComponent* WheelBlock = FQuarryBlockoutVisuals::AddCylinder(this, WheelName, VisualRoot, 36.0f, 18.0f,
				FVector(X, Y + FMath::Sign(Y) * 48.0f, -35.0), FRotator(90.0f, 0.0f, 0.0f), Steel);
			if (WheelBlock)
			{
				ReplacedVisualParts.Add(WheelBlock);
			}
		}
	}
	// Wheeled excavator: front axle steers, matching the real wheel visuals.
	// 物理轮位：与真实轮分件的模型位置一致（轴距 300、轮距 310、半径 59.6）
	// 前轮转向（X>0 为前轴）；视觉轮组件由分件装配挂载
	for (float X : {150.0f, -150.0f})
	{
		for (float Y : {-155.0f, 155.0f})
		{
			VehicleMovement->AddWheel(FVector(X, Y, -20.0), 59.6f, true, X > 0.0f, nullptr);
		}
	}

	// The user-modeled wheeled excavator (split parts from 建模/大挖掘机) with
		// a REAL articulated arm: boom/stick/bucket parts are parented to a
		// measured joint chain (J1=(0,0,80), J2=(411,0,56), J3=(423,0,-175)) and
		// driven by the same Boom/Stick/Bucket angles as the invisible blockout
		// arm. Drive type switches to wheeled to match the 4-wheel visual.
	RealVehicleMesh = CreateDefaultSubobject<UQuarryVehicleMeshComponent>(TEXT("RealVehicleMesh"));
	RealVehicleMesh->BodyMeshPath = TEXT("");
	// 分件资产目录：建模/大挖掘机 导出后按部件拆分的静态网格
	RealVehicleMesh->PartsFolder = TEXT("/Game/MyExcavator/Static1");
	RealVehicleMesh->PartsFitLength = 650.0f;
	// 真实铰点链（模型空间，实测自分件包围盒）：
	//   J1 动臂铰点 = 大臂根部；J2 = J1 + 大臂长度方向的偏移；J3 = 小臂末端
	RealVehicleMesh->BoomJointLocal = FVector(0.0f, 0.0f, 80.0f);
	RealVehicleMesh->StickJointOffset = FVector(411.0f, 0.0f, -24.0f);
	RealVehicleMesh->BucketJointOffset = FVector(12.0f, 0.0f, -231.0f);
	// 分件绑定表：PartName / 父节点(0=底盘 1=动臂 2=小臂 3=铲斗) / 相对位姿 / 是否随轮转
	// 动臂/小臂/铲斗的 RelativeLocation 是各自铰点模型坐标的负值——
	// 使关节旋转时部件绕铰点运动而非绕模型原点公转
	RealVehicleMesh->PartBindings = {
		// body + cab (chassis root, model space)
		{TEXT("SK_ExcavatorWheels_Body1"), 0, FVector(0, 0, 0), FRotator::ZeroRotator, false},
		{TEXT("SK_ExcavatorWheels_BodyUp"), 0, FVector(0, 0, 0), FRotator::ZeroRotator, false},
		{TEXT("SK_ExcavatorWheels_Door"), 0, FVector(0, 0, 0), FRotator::ZeroRotator, false},
		// boom parts on the boom joint (offset = -joint model position)
		{TEXT("SK_ExcavatorWheels_大臂"), 1, FVector(0, 0, -80), FRotator::ZeroRotator, false},
		{TEXT("SK_ExcavatorWheels_大臂2"), 1, FVector(0, 0, -80), FRotator::ZeroRotator, false},
		// stick parts on the stick joint
		{TEXT("SK_ExcavatorWheels_小臂"), 2, FVector(-411, 0, -56), FRotator::ZeroRotator, false},
		// bucket parts on the bucket joint
		{TEXT("SK_ExcavatorWheels_大铲子"), 3, FVector(-423, 0, 175), FRotator::ZeroRotator, false},
		{TEXT("SK_ExcavatorWheels_铲"), 3, FVector(-423, 0, 175), FRotator::ZeroRotator, false},
		// four wheels on the chassis, pivots centered -> they spin in place
		{TEXT("SK_ExcavatorWheels_WheelFL"), 0, FVector(150, -155, 59.6f), FRotator::ZeroRotator, true},
		{TEXT("SK_ExcavatorWheels_WheelFR"), 0, FVector(150, 155, 59.6f), FRotator::ZeroRotator, true},
		{TEXT("SK_ExcavatorWheels_WheelRL"), 0, FVector(-150, -155, 59.6f), FRotator::ZeroRotator, true},
		{TEXT("SK_ExcavatorWheels_WheelRR"), 0, FVector(-150, 155, 59.6f), FRotator::ZeroRotator, true}
	};
	// 用户模型是 4 轮轮挖 → 驱动改为轮式（前轮转向），与视觉一致
	VehicleMovement->DriveType = EQuarryDriveType::Wheeled;
	// 轮挖行驶限速（低于矿卡：作业机械特性）
	VehicleMovement->MaxForwardSpeed = 400.0f;
	VehicleMovement->MaxReverseSpeed = 300.0f;

	UpperStructurePivot = CreateDefaultSubobject<USceneComponent>(TEXT("UpperStructurePivot"));
	UpperStructurePivot->SetupAttachment(VisualRoot);
	UpperStructurePivot->SetRelativeLocation(FVector(0.0, 0.0, 120.0));
	AddBlockoutCube(TEXT("UpperBody"), UpperStructurePivot, FVector(260.0, 250.0, 90.0), FVector(-40.0, 0.0, 35.0), Yellow);
	AddBlockoutCube(TEXT("Counterweight"), UpperStructurePivot, FVector(90.0, 255.0, 100.0), FVector(-155.0, 0.0, 40.0), Highlight);
	AddBlockoutCube(TEXT("Cab"), UpperStructurePivot, FVector(130.0, 105.0, 125.0), FVector(25.0, -72.0, 100.0), Yellow);
	AddBlockoutCube(TEXT("CabGlass"), UpperStructurePivot, FVector(105.0, 108.0, 75.0), FVector(35.0, -72.0, 120.0), Glass, false, FRotator(-8.0f, 0.0f, 0.0f));
	AddBlockoutCube(TEXT("CabRoof"), UpperStructurePivot, FVector(145.0, 120.0, 12.0), FVector(25.0, -72.0, 168.0), Dark);
	AddBlockoutCube(TEXT("CabLight"), UpperStructurePivot, FVector(25.0, 35.0, 18.0), FVector(95.0, -72.0, 160.0), FLinearColor(1.0f, 0.9f, 0.45f));
	AddBlockoutCylinder(TEXT("Exhaust"), UpperStructurePivot, 12.0f, 100.0f, FVector(-105.0, 75.0, 115.0), FRotator::ZeroRotator, Dark);

	BoomPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BoomPivot"));
	BoomPivot->SetupAttachment(UpperStructurePivot);
	BoomPivot->SetRelativeLocation(FVector(100.0, 0.0, 80.0));
	AddBlockoutCube(TEXT("Boom"), BoomPivot, FVector(300.0, 58.0, 58.0), FVector(150.0, 0.0, 0.0), Yellow);
	AddBlockoutCube(TEXT("BoomHighlight"), BoomPivot, FVector(240.0, 62.0, 12.0), FVector(140.0, 0.0, 25.0), Highlight);
	AddBlockoutCylinder(TEXT("BoomHydraulic"), BoomPivot, 13.0f, 190.0f, FVector(80.0, -48.0f, 15.0), FRotator(55.0f, 0.0f, 0.0f), Steel);

	StickPivot = CreateDefaultSubobject<USceneComponent>(TEXT("StickPivot"));
	StickPivot->SetupAttachment(BoomPivot);
	StickPivot->SetRelativeLocation(FVector(300.0, 0.0, 0.0));
	AddBlockoutCube(TEXT("Stick"), StickPivot, FVector(220.0, 45.0, 45.0), FVector(110.0, 0.0, 0.0), Yellow);
	AddBlockoutCylinder(TEXT("StickHydraulic"), StickPivot, 11.0f, 150.0f, FVector(85.0, 35.0f, 20.0), FRotator(60.0f, 0.0f, 0.0f), Steel);

	BucketPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BucketPivot"));
	BucketPivot->SetupAttachment(StickPivot);
	BucketPivot->SetRelativeLocation(FVector(220.0, 0.0, 0.0));
	AddBlockoutCube(TEXT("BucketBottom"), BucketPivot, FVector(100.0, 115.0, 14.0), FVector(20.0, 0.0, -38.0), Dark);
	AddBlockoutCube(TEXT("BucketBack"), BucketPivot, FVector(18.0, 115.0, 70.0), FVector(-25.0, 0.0, -8.0), Steel);
	for (float Y : {-52.0f, 52.0f})
	{
		const FName SideName = *FString::Printf(TEXT("BucketSide_%d"), FMath::RoundToInt(Y));
		AddBlockoutCube(SideName, BucketPivot, FVector(95.0, 12.0, 65.0), FVector(20.0, Y, -8.0), Steel);
	}
	for (float Y : {-38.0f, 0.0f, 38.0f})
	{
		const FName ToothName = *FString::Printf(TEXT("BucketTooth_%d"), FMath::RoundToInt(Y));
		AddBlockoutCube(ToothName, BucketPivot, FVector(32.0, 20.0, 14.0), FVector(75.0, Y, -42.0), Dark);
	}
	BucketTip = CreateDefaultSubobject<USceneComponent>(TEXT("BucketTip"));
	BucketTip->SetupAttachment(BucketPivot);
	BucketTip->SetRelativeLocation(FVector(80.0, 0.0, -35.0));
	BucketFill = AddBlockoutCube(TEXT("BucketFill"), BucketPivot,
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
	if (const UQuarryConfigSubsystem* Config = GetGameInstance() ? GetGameInstance()->GetSubsystem<UQuarryConfigSubsystem>() : nullptr) // pbtxt 配置覆盖
	{
		const auto& Vehicle = Config->GetExcavatorConfig();
		if (Vehicle.base_mass_kg() > 0.0) VehicleMovement->BaseMassKg = Vehicle.base_mass_kg();
		if (Vehicle.engine_force_n() > 0.0) VehicleMovement->EngineForce = Vehicle.engine_force_n();
		if (Vehicle.brake_force_n() > 0.0) VehicleMovement->BrakeForce = Vehicle.brake_force_n();
		if (Vehicle.max_forward_speed_cm_s() > 0.0) VehicleMovement->MaxForwardSpeed = Vehicle.max_forward_speed_cm_s();
		if (Vehicle.max_reverse_speed_cm_s() > 0.0) VehicleMovement->MaxReverseSpeed = Vehicle.max_reverse_speed_cm_s();
		if (Vehicle.cargo_capacity_kg() > 0.0) BucketCargo->CapacityKg = Vehicle.cargo_capacity_kg();
		if (Vehicle.work_rate_kg_s() > 0.0) WorkRateKgPerSecond = Vehicle.work_rate_kg_s();
		if (Vehicle.track_yaw_rate_deg_s() > 0.0) VehicleMovement->TrackYawRateDegPerSecond = Vehicle.track_yaw_rate_deg_s();
		if (Vehicle.tire_friction() > 0.0) VehicleMovement->TireFrictionScale = Vehicle.tire_friction();
		if (Vehicle.loose_material_friction() > 0.0) VehicleMovement->LooseMaterialFriction = Vehicle.loose_material_friction();
		if (Vehicle.engine_peak_torque_nm() > 0.0) VehicleMovement->EnginePeakTorqueNm = Vehicle.engine_peak_torque_nm();
		VehicleMovement->bLegacyKinematic = Vehicle.legacy_kinematic();
		ScoopMassKg = BucketCargo->CapacityKg;
	}
	BucketCargo->OnCargoChanged.AddDynamic(this, &AQuarryExcavator::UpdateCargoVisual);
	UpdateCargoVisual(BucketCargo->MaterialType, BucketCargo->CurrentMassKg, BucketCargo->CapacityKg);

	// Swap in the real parts assembly with the articulated arm (falls back to
	// blockout visuals if the assets are missing).
	if (RealVehicleMesh)
	{
		RealVehicleMesh->Initialize(VehicleMovement, VisualRoot, ReplacedVisualParts);
		RealVehicleMesh->AttachPartsAssembly(VisualRoot);
	}
}

// 每帧：1) 按摇杆积分动臂/小臂/铲斗角度 2) 更新三条 Pivot 链 3) 驱动真实
// 分件关节 4) 追踪真实铲斗尖端速度（挖掘判定输入）5) 执行挖掘/卸料逻辑
void AQuarryExcavator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 回转：上部结构绕 Z 轴旋转（Q/E 控制，45°/s）
	SlewAngle = FMath::UnwindDegrees(SlewAngle + SlewCommand * SlewSpeedDegreesPerSecond * DeltaSeconds);
	// 动臂油缸：R 举升 / F 下放（行程 −45°..65°，35°/s）
	BoomAngle = FMath::Clamp(BoomAngle + BoomCommand * HydraulicSpeedDegreesPerSecond * DeltaSeconds, -45.0f, 65.0f);
	// 小臂油缸：T 收 / G 伸（行程 −130°..25°）
	StickAngle = FMath::Clamp(StickAngle + StickCommand * HydraulicSpeedDegreesPerSecond * DeltaSeconds, -130.0f, 25.0f);
	// 铲斗油缸：Y 内卷 / H 外翻（行程 −110°..85°）
	BucketAngle = FMath::Clamp(BucketAngle + BucketCommand * HydraulicSpeedDegreesPerSecond * DeltaSeconds, -110.0f, 85.0f);
	UpperStructurePivot->SetRelativeRotation(FRotator(0.0f, SlewAngle, 0.0f));
	BoomPivot->SetRelativeRotation(FRotator(BoomAngle, 0.0f, 0.0f));
	StickPivot->SetRelativeRotation(FRotator(StickAngle, 0.0f, 0.0f));
	BucketPivot->SetRelativeRotation(FRotator(BucketAngle, 0.0f, 0.0f));

	// Track bucket tip speed for the dig-rate model (real FK tip when present).
	const FVector TipLocation = GetDigSampleLocation();
	if (bHasLastTipLocation && DeltaSeconds > 1e-5f)
	{
		// 尖端速度 = 位移/Δt，FInterpTo 平滑（挖掘速率 ∝ 尖端扫掠速度）
		const float RawSpeed = FVector::Dist(TipLocation, LastTipLocation) / DeltaSeconds;
		BucketTipSpeedCmS = FMath::FInterpTo(BucketTipSpeedCmS, RawSpeed, DeltaSeconds, 12.0f);
	}
	LastTipLocation = TipLocation;
	bHasLastTipLocation = true;

	// Drive the real arm joints with the same angles as the blockout chain.
	if (RealVehicleMesh)
	{
		RealVehicleMesh->DriveArticulatedParts(BoomAngle, StickAngle, BucketAngle);
	}
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

// 开始挖掘前置校验：空闲状态 + 车辆静止 + 料堆在作业半径内
void AQuarryExcavator::StartDigging()
{
	if (WorkState != EQuarryExcavatorWorkState::Idle || !IsNearlyStationary())
	{
		OperationStatus = TEXT("STOP VEHICLE BEFORE WORKING");
		return;
	}
	if (BucketCargo->GetFreeCapacityKg() <= 0.0f)   // 铲斗已满
	{
		OperationStatus = TEXT("BUCKET FULL");
		return;
	}
	WorkPile = FindNearestPile(WorkRadius);   // 寻找作业半径内的料堆
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

// 挖掘/卸料状态机：Idle→Digging→Dumping，每帧校验前置条件
void AQuarryExcavator::TickWork(float DeltaSeconds)
{
	if (WorkState == EQuarryExcavatorWorkState::Idle) return; // 空闲不消耗逻辑
	if (!IsNearlyStationary())   // 作业中断条件：车辆移动中
	{
		StopDigging();
		StopDumping();
		OperationStatus = TEXT("WORK CANCELLED: VEHICLE MOVING");
		return;
	}

	if (WorkState == EQuarryExcavatorWorkState::Digging)
	{
		if (!IsValid(WorkPile) || WorkPile->GetDistanceToWorkSurface(GetDigSampleLocation()) > WorkRadius)
		{
			StopDigging();
			OperationStatus = TEXT("WORK CANCELLED: OUT OF RANGE");
			return;
		}

		// 挖掘速率 = 基础速率 × 尖端速度因子 × 切入角因子 × 材料硬度
		const float DigEfficiency = ComputeDigEfficiency();
		if (DigEfficiency <= 0.0f)
		{
			OperationStatus = BucketTipSpeedCmS < MinDigSpeedCmS
				? TEXT("SWING BUCKET THROUGH THE PILE")
				: TEXT("ADJUST BUCKET CUT ANGLE");
			return;
		}
		// 本帧请求量 = 速率×dt×效率，钳制在剩余容量内
		const float Requested = FMath::Min(WorkRateKgPerSecond * DeltaSeconds * DigEfficiency, BucketCargo->GetFreeCapacityKg());
		// 从料堆挖出（真实铲斗 FK 尖端位置——地形同步下陷）
		const float Extracted = WorkPile->ExtractAt(GetDigSampleLocation(), Requested);
		const float Accepted = BucketCargo->Load(WorkPile->MaterialType, Extracted);
		if (Accepted < Extracted)   // 装不下的料洒回料堆（料流守恒）
		{
			WorkPile->DepositAt(GetDigSampleLocation(), WorkPile->MaterialType, Extracted - Accepted);
		}
		if (Accepted <= 0.0f || BucketCargo->GetFreeCapacityKg() <= 0.01f)
		{
			StopDigging();
			OperationStatus = TEXT("BUCKET FULL");
		}
		return;
	}

	if (!IsValid(WorkTruck) || FVector::Dist(GetDigSampleLocation(), WorkTruck->GetCargoIntakeLocation()) > WorkRadius)
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

float AQuarryExcavator::ComputeDigEfficiency() const
{
	// The teeth must sweep through the material at working speed.
	if (BucketTipSpeedCmS < MinDigSpeedCmS)
	{
		return 0.0f;
	}
	const float SpeedFactor = FMath::Clamp(BucketTipSpeedCmS / FMath::Max(1.0f, OptimalDigSpeedCmS), 0.0f, 1.0f);

	// Bucket floor pitch vs the optimal cutting angle.
	const float BucketPitchDeg = FMath::UnwindDegrees(BucketPivot->GetComponentRotation().Pitch);
	const float AngleError = FMath::Abs(FMath::UnwindDegrees(BucketPitchDeg - OptimalCutAngleDeg));
	const float AngleFactor = FMath::Clamp(1.0f - AngleError / FMath::Max(1.0f, CutAngleToleranceDeg), 0.0f, 1.0f);

	const float Hardness = IsValid(WorkPile) ? FMath::Clamp(WorkPile->MaterialHardness, 0.05f, 1.0f) : 1.0f;
	return SpeedFactor * AngleFactor * Hardness;
}

AQuarryMaterialPile* AQuarryExcavator::FindNearestPile(float Radius) const
{
	AQuarryMaterialPile* Best = nullptr;
	float BestDistance = Radius;
	const FVector Origin = GetDigSampleLocation();
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
	const FVector Origin = GetDigSampleLocation();
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
	const float Extracted = Pile->ExtractAt(GetDigSampleLocation(), Requested);
	const float Accepted = BucketCargo->Load(Pile->MaterialType, Extracted);
	if (Accepted < Extracted)
	{
		Pile->DepositAt(GetDigSampleLocation(), Pile->MaterialType, Extracted - Accepted);
	}
	return Accepted;
}

float AQuarryExcavator::DumpIntoNearestTruck()
{
	AQuarryMiningTruck* Truck = FindNearestTruck(WorkRadius);
	return Truck ? BucketCargo->TransferTo(Truck->Cargo, BucketCargo->CurrentMassKg) : 0.0f;
}

void AQuarryExcavator::ApplyActuatorCommands(float Slew, float Boom, float Stick, float Bucket)
{
	SlewCommand = FMath::Clamp(Slew, -1.0f, 1.0f);
	BoomCommand = FMath::Clamp(Boom, -1.0f, 1.0f);
	StickCommand = FMath::Clamp(Stick, -1.0f, 1.0f);
	BucketCommand = FMath::Clamp(Bucket, -1.0f, 1.0f);
}

void AQuarryExcavator::SetDiggingActive(bool bActive)
{
	if (bActive)
	{
		if (WorkState == EQuarryExcavatorWorkState::Idle)
		{
			StartDigging();
		}
	}
	else
	{
		StopDigging();
	}
}

void AQuarryExcavator::SetDumpingActive(bool bActive)
{
	if (bActive)
	{
		if (WorkState == EQuarryExcavatorWorkState::Idle)
		{
			StartDumping();
		}
	}
	else
	{
		StopDumping();
	}
}

void AQuarryExcavator::UpdateCargoVisual(EQuarryMaterialType MaterialType, float CurrentMassKg, float CapacityKg)
{
	const float Ratio = CapacityKg > 0.0f ? CurrentMassKg / CapacityKg : 0.0f;
	BucketFill->SetVisibility(Ratio > 0.001f);
	BucketFill->SetRelativeScale3D(FVector(0.6f, 0.8f, FMath::Max(0.05f, Ratio * 0.45f)));
	VehicleMovement->SetPayloadMassKg(CurrentMassKg);
}

FVector AQuarryExcavator::GetDigSampleLocation() const
{
	// Prefer the real arm's FK tip so the dig judgment matches the visible
	// bucket; fall back to the blockout tip when the real assets are absent.
	if (RealVehicleMesh && RealVehicleMesh->IsBodyMeshActive())
	{
		const FVector Tip = RealVehicleMesh->GetRealBucketTip();
		if (!Tip.IsZero())
		{
			return Tip;
		}
	}
	return BucketTip->GetComponentLocation();
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
