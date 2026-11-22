#include "Vehicles/QuarryVehicleMovementComponent.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "QuarryCollisionChannels.h"
#include "Terrain/QuarryDigSite.h"

// ============================================================================
// QuarryVehicleMovementComponent —— 力驱动物理核心（CARLA 同源方法论）
// 整车可视化/物理的三层结构：
//   1) 物理层：底盘 BoxComponent + Chaos 刚体，所有力通过 AddForce* 施加
//   2) 轮胎层：每轮 raycast 悬架 + 简化 Pacejka 轮胎 + 摩擦圆
//   3) 视觉层：轮网格由 UpdateWheelVisual 按悬架长度/自转角/转向角驱动
// ============================================================================
namespace
{
	// 1 N = 100 kg*cm/s^2 (UE force units). Internal forces use UE units.
	constexpr float UeForcePerNewton = 100.0f;
	constexpr float RadiansToRPM = 60.0f / (2.0f * PI);
	// Slip-ratio denominator floor in cm/s; keeps low-speed ratios finite.
	constexpr float MinSlipSpeedCmS = 25.0f;
}

UQuarryVehicleMovementComponent::UQuarryVehicleMovementComponent()
{
	// 每帧 Tick，且在物理求解之前（PrePhysics）施力，保证当帧物理使用当帧控制
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UQuarryVehicleMovementComponent::ConfigureChassis(UBoxComponent* InChassis)
{
	Chassis = InChassis;                    // 记录物理底盘（所有力施加在它上面）
	SetUpdatedComponent(InChassis);         // 声明移动组件的更新体 = 底盘
	ApplyMassProperties();                  // 立即应用质量/质心
}

void UQuarryVehicleMovementComponent::AddWheel(const FVector& MountOffset, float Radius, bool bDriven, bool bSteered, USceneComponent* Visual)
{
	FQuarryWheelState Wheel;
	Wheel.MountOffset = MountOffset;
	Wheel.Radius = Radius;
	Wheel.bDriven = bDriven;
	Wheel.bSteered = bSteered;
	Wheel.Visual = Visual;
	Wheel.BaseVisualRotation = Visual ? Visual->GetRelativeRotation() : FRotator::ZeroRotator;
	Wheel.SuspensionLength = SuspensionRestLength * 0.7f;
	Wheels.Add(Wheel);
}

void UQuarryVehicleMovementComponent::SetDriveInput(float Throttle, float Steering)
{
	// 油门 [-1 倒车, +1 前进]、转向 [-1 左, +1 右]——统一钳制防止外部脏数据
	ThrottleCommand = FMath::Clamp(Throttle, -1.0f, 1.0f);
	SteeringCommand = FMath::Clamp(Steering, -1.0f, 1.0f);
}

void UQuarryVehicleMovementComponent::SetServiceBrake(float Brake)
{
	// 行车制动踏板深度 [0, 1]（1 = 全力制动）
	BrakeCommand = FMath::Clamp(Brake, 0.0f, 1.0f);
}

void UQuarryVehicleMovementComponent::SetParkingBrake(bool bEnabled)
{
	bParkingBrake = bEnabled;
}

void UQuarryVehicleMovementComponent::SetPayloadMassKg(float MassKg)
{
	// 货载质量（挖斗/车斗里的料）——影响整车惯量与悬架压缩
	PayloadMassKg = FMath::Max(0.0f, MassKg);
	ApplyMassProperties();                  // 质量变化立即生效
}

void UQuarryVehicleMovementComponent::SetPayloadCenterOfMass(const FVector& LocalOffset)
{
	PayloadCenterOfMass = LocalOffset;
	ApplyMassProperties();
}

float UQuarryVehicleMovementComponent::GetForwardSpeed() const
{
	// 前向速度 = 速度向量在车头方向上的投影（带符号：倒车为负）
	return Chassis ? FVector::DotProduct(Chassis->GetPhysicsLinearVelocity(), Chassis->GetForwardVector()) : 0.0f;
}

float UQuarryVehicleMovementComponent::GetSpeedKmh() const
{
	// 速率模长换算 km/h（cm/s × 0.036）——与方向无关，倒车也是正数
	return Chassis ? Chassis->GetPhysicsLinearVelocity().Size() * 0.036f : 0.0f;
}

FString UQuarryVehicleMovementComponent::GetGearLabel() const
{
	if (CurrentGear < 0) return TEXT("R");
	if (CurrentGear == 0) return TEXT("N");
	return FString::FromInt(CurrentGear);
}

bool UQuarryVehicleMovementComponent::IsAnyWheelSlipping() const
{
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		// 接地且滑移率超过 20% 视为打滑（HUD 的 TIRE SLIP 指示来源）
		if (Wheel.bInContact && FMath::Abs(Wheel.SlipRatio) > 0.2f)
		{
			return true;
		}
	}
	return false;
}

float UQuarryVehicleMovementComponent::GetMaxWheelSlipRatio() const
{
	float MaxSlip = 0.0f;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bInContact)
		{
			MaxSlip = FMath::Max(MaxSlip, FMath::Abs(Wheel.SlipRatio));
		}
	}
	return MaxSlip;
}

void UQuarryVehicleMovementComponent::ApplyMassProperties()
{
	if (!Chassis || (GetOwner() && GetOwner()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		return;
	}

	// 总质量 = 底盘 + 货载（下限 1kg 防除零）→ 写入物理体质量覆盖
	const float TotalMass = FMath::Max(1.0f, BaseMassKg + PayloadMassKg);
	Chassis->SetMassOverrideInKg(NAME_None, TotalMass, true);
	// 质心随货载比例前/后移（载重时操纵特性真实变化的关键）
	const float PayloadRatio = PayloadMassKg / TotalMass;
	Chassis->SetCenterOfMass(PayloadCenterOfMass * PayloadRatio);
}

float UQuarryVehicleMovementComponent::GetMeanDrivenWheelRadiusM() const
{
	// 求所有驱动轮半径的平均值（米）——发动机扭矩 → 轮缘推力的换算系数
	float Sum = 0.0f;
	int32 Count = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bDriven)                     // 只统计参与驱动的轮
		{
			Sum += Wheel.Radius;               // 累加半径
			++Count;
		}
	}
	if (Count == 0)                           // 无驱动轮时回退用全部轮
	{
		for (const FQuarryWheelState& Wheel : Wheels)
		{
			Sum += Wheel.Radius;
			++Count;
		}
	}
	return (Count > 0 ? Sum / Count : 50.0f) * 0.01f; // cm → m
}

void UQuarryVehicleMovementComponent::CalibrateFromDefaults()
{
	if (bCalibrated)                          // 只在首帧标定一次
	{
		return;
	}
	bCalibrated = true;

	// 单轮静态载荷（UE 力单位）：整车重力均摊到每个轮
	const int32 NumWheels = FMath::Max(1, Wheels.Num());
	const float StaticLoadUE = BaseMassKg * 981.0f / NumWheels; // 981 = g(cm/s²)

	if (SuspensionStiffness <= 0.0f)
	{
		// 空载时悬架压缩 ~45% 行程（乘坐姿态与通过性的折中）
		const float TargetCompression = FMath::Max(5.0f, 0.45f * SuspensionTravel);
		SuspensionStiffness = StaticLoadUE / TargetCompression;   // 刚度 = 载荷/目标压缩
		SuspensionMaxForce = FMath::Max(SuspensionMaxForce, 6.0f * StaticLoadUE); // 上限 6 倍静载防击穿
	}
	if (SuspensionDamping <= 0.0f)
	{
		// 阻尼 = 2ζ√(k·m)：临界阻尼比 ζ=DampingRatio（0.7=欠阻尼舒适档）
		SuspensionDamping = 2.0f * SuspensionDampingRatio
			* FMath::Sqrt(SuspensionStiffness * BaseMassKg / NumWheels);
	}
	if (AntiRollStiffness < 0.0f)
	{
		// 防倾杆刚度 = 30% 悬架刚度（过弯侧倾的抑制强度）
		AntiRollStiffness = 0.3f * SuspensionStiffness;
	}

	if (AirDragCoefficient <= 0.0f)
	{
		// 空气阻力系数：标定为极速时风阻 ≈ 30% 发动机推力（高速的天然限速器）
		AirDragCoefficient = 0.3f * EngineForce * UeForcePerNewton / FMath::Max(1.0f, MaxForwardSpeed * MaxForwardSpeed);
	}

	if (DriveType == EQuarryDriveType::Tracked)
	{
		const float TotalLoadUE = StaticLoadUE * NumWheels;  // 整机全部载荷
		if (TrackGripStiffness <= 0.0f)   // 履带速度伺服增益：目标速度差 → 推力
		{
			TrackGripStiffness = 0.7f * TireFrictionScale * GroundFriction * TotalLoadUE / 150.0f;
		}
		if (TrackLateralStiffness <= 0.0f)  // 横向约束：防侧滑的恢复力增益
		{
			TrackLateralStiffness = 2.0f * TrackGripStiffness;
		}
		if (TrackTurnResistance <= 0.0f)
		{
			float MinX = TNumericLimits<float>::Max();
			float MaxX = -TNumericLimits<float>::Max();
			float MeanRadiusCm = 0.0f;
			for (const FQuarryWheelState& Wheel : Wheels)
			{
				MinX = FMath::Min(MinX, Wheel.MountOffset.X);
				MaxX = FMath::Max(MaxX, Wheel.MountOffset.X);
				MeanRadiusCm += Wheel.Radius;
			}
			MeanRadiusCm = Wheels.Num() > 0 ? MeanRadiusCm / Wheels.Num() : 50.0f;
			const float ContactLengthCm = FMath::Max(100.0f, MaxX - MinX + 2.0f * MeanRadiusCm);
			// 履带转向阻力矩（土壤剪切）：接触长度越长越难转向
			TrackTurnResistance = TotalLoadUE * ContactLengthCm / 60.0f;
		}
	}
}

void UQuarryVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 物理未模拟（编辑器未 Play / 休眠体）或非法帧间隔时跳过
	if (!Chassis || !Chassis->IsSimulatingPhysics() || DeltaTime <= 0.0f)
	{
		return;
	}

	if (!bCalibrated)   // 首帧惰性标定：等 pbtxt/实例参数全部就位后再算
	{
		CalibrateFromDefaults();
	}

	if (bLegacyKinematic)   // 兼容开关：旧运动学直驱（调试/回退用）
	{
		TickLegacyKinematic(DeltaTime);
		return;
	}

	if (DriveType == EQuarryDriveType::Tracked)  // 按驱动形式分流：履带差速 / 轮式独立悬架
	{
		TickTrackedDrive(DeltaTime);
	}
	else
	{
		TickWheeledDrive(DeltaTime);
	}

	// 视觉层收尾：轮网格按悬架长度/自转角/转向角摆放（位置-旋转-视觉三同步）
	for (FQuarryWheelState& Wheel : Wheels)
	{
		UpdateWheelVisual(Wheel, DeltaTime);
	}
}

void UQuarryVehicleMovementComponent::TickWheeledDrive(float DeltaTime)
{
	GroundedWheelCount = 0;
	LastDriveForce = 0.0f;

	// 车速感应转向：高速时转向角衰减（MinSteerFactor），避免高速猛打方向
	const float SpeedAbs = FMath::Abs(GetForwardSpeed());
	// SteerFactor: 0 km/h = 1.0（全角度）→ SteeringFadeSpeed = MinSteerFactor
	const float SteerFactor = FMath::Lerp(1.0f, MinSteerFactor,
		FMath::Clamp(SpeedAbs / FMath::Max(1.0f, SteeringFadeSpeed), 0.0f, 1.0f));
	// 目标前轮角 + 恒速逼近（转向速率限制——模拟液压转向的响应延迟）
	const float TargetSteerDeg = SteeringCommand * MaxSteerAngle * SteerFactor;
	CurrentSteerAngleDeg = FMath::FInterpConstantTo(CurrentSteerAngleDeg, TargetSteerDeg, DeltaTime, SteeringRateDegPerSecond);
	const float SteerRad = FMath::DegreesToRadians(CurrentSteerAngleDeg);

	// 把转向角写进转向轮状态 + 逐轮更新悬架（raycast → 弹簧力）
	for (FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bSteered)
		{
			Wheel.SteerAngleRad = SteerRad;   // 只有转向轮吃到角度
		}
		UpdateSuspension(Wheel, DeltaTime);   // 悬架：raycast → 弹簧阻尼力
	}
	ApplyAntiRollForces();                 // 防倾杆：左右轮压缩差 → 反向力偶
	UpdatePowertrain(DeltaTime);           // 动力总成：转速/挡位/轮驱动扭矩

	// 轮胎层：滑移率/侧偏角 → Pacejka 力（摩擦圆联合钳制）→ 施加到接地点
	for (FQuarryWheelState& Wheel : Wheels)
	{
		UpdateTireForces(Wheel, DeltaTime, WheelDriveTorqueNm);
	}

	// 空气阻力 ∝ 速度平方（方向与速度相反）
	const FVector ChassisVelocity = Chassis->GetPhysicsLinearVelocity();
	const float SpeedSquared = ChassisVelocity.SizeSquared();
	if (AirDragCoefficient > 0.0f && SpeedSquared > 1.0f)
	{
		Chassis->AddForce(ChassisVelocity.GetSafeNormal() * (-AirDragCoefficient * SpeedSquared));
	}
}

void UQuarryVehicleMovementComponent::TickTrackedDrive(float DeltaTime)
{
	GroundedWheelCount = 0;
	LastDriveForce = 0.0f;

	// 履带层：先更新悬架获得每轮载荷，再按左右履带分组施加伺服力
	for (FQuarryWheelState& Wheel : Wheels)
	{
		UpdateSuspension(Wheel, DeltaTime);
	}
	ApplyAntiRollForces();

	// Split wheels into left/right track groups.  // 按 Y<0 分左右履带组
	// 左右履带各自的：载荷合计 / 摩擦系数累计 / 接地点累计 / 均值基线
	float LeftLoadUE = 0.0f;
	float RightLoadUE = 0.0f;
	float LeftFrictionSum = 0.0f;
	float RightFrictionSum = 0.0f;
	FVector LeftContactSum = FVector::ZeroVector;
	FVector RightContactSum = FVector::ZeroVector;
	int32 LeftContactCount = 0;
	int32 RightContactCount = 0;
	float LeftMeanY = -150.0f;
	float RightMeanY = 150.0f;
	int32 LeftCount = 0;
	int32 RightCount = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.MountOffset.Y < 0.0f)   // Y<0 = 左侧履带组
		{
			LeftLoadUE += Wheel.LoadForceUE;      // 累加悬架载荷（伺服力上限）
			LeftMeanY += Wheel.MountOffset.Y;     // 累加横向位置（求履带中心）
			++LeftCount;
			if (Wheel.bInContact)
			{
				LeftContactSum += Wheel.ContactPoint;
				LeftFrictionSum += Wheel.ContactFriction;
				++LeftContactCount;
			}
		}
		else
		{
			RightLoadUE += Wheel.LoadForceUE;
			RightMeanY += Wheel.MountOffset.Y;
			++RightCount;
			if (Wheel.bInContact)
			{
				RightContactSum += Wheel.ContactPoint;
				RightFrictionSum += Wheel.ContactFriction;
				++RightContactCount;
			}
		}
	}
	if (LeftCount > 0)  // 求左右履带的横向中心（决定差速力偶的力臂）
	{
		LeftMeanY = LeftMeanY / LeftCount;
	}
	// 履带半距 = 左右履带中心距的一半（差速转向的力臂，下限 50cm 防退化）
	const float TrackHalfWidthCm = FMath::Max(50.0f, (FMath::Abs(LeftMeanY) + FMath::Abs(RightMeanY)) * 0.5f);

	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	// 左履带施力点：全部接地点均值（悬空时回退到模型位置）
	const FVector LeftPoint = LeftContactCount > 0 ? LeftContactSum / LeftContactCount
		: ChassisTransform.TransformPosition(FVector(0.0f, LeftMeanY, -SuspensionRestLength));
	const FVector RightPoint = RightContactCount > 0 ? RightContactSum / RightContactCount
		: ChassisTransform.TransformPosition(FVector(0.0f, RightMeanY, -SuspensionRestLength));

	// 底盘三轴：前/上/侧向（力的分解基准）
	const FVector Forward = Chassis->GetForwardVector();
	const FVector Up = Chassis->GetUpVector();
	const FVector Lateral = FVector::CrossProduct(Up, Forward);

	// 有效制动 = 行车制动与驻车取大（驻车即全制动）
	const float EffectiveBrake = FMath::Max(BrakeCommand, bParkingBrake ? 1.0f : 0.0f);
	// 油门指令 → 目标履带速度（前进/倒车限速不同）
	float SpeedCmd = ThrottleCommand * (ThrottleCommand >= 0.0f ? MaxForwardSpeed : MaxReverseSpeed);
	if (EffectiveBrake > 0.01f)
	{
		SpeedCmd *= FMath::Max(0.0f, 1.0f - EffectiveBrake);   // 制动按比例衰减目标速度
	}
	// Positive steering commands a positive (CCW) yaw rate: left track slows,
	// right track speeds up. The controller's heading-error convention expects
	// this, so the sign here defines the excavator's A/D steering direction.
	// 转向指令 → 目标偏航角速度（rad/s）；负号 = 与底盘 +Y 右向约定对齐
	const float YawCmd = -FMath::DegreesToRadians(TrackYawRateDegPerSecond) * SteeringCommand
		* FMath::Max(0.0f, 1.0f - EffectiveBrake);

	// 差速转向：左右履带目标速度 = 直行速度 ∓ 偏航角速度 × 履带半距
	// （左侧减速+右侧加速 = 向左转；同向为直行）
	const float LeftTarget = SpeedCmd - YawCmd * TrackHalfWidthCm;
	const float RightTarget = SpeedCmd + YawCmd * TrackHalfWidthCm;

	const FVector LeftVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(LeftPoint);
	const FVector RightVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(RightPoint);
	// 左履带接地点速度投影到前/侧向（伺服误差的反馈量）
	const float LeftVLong = FVector::DotProduct(LeftVelocity, Forward);
	const float LeftVLat = FVector::DotProduct(LeftVelocity, Lateral);
	const float RightVLong = FVector::DotProduct(RightVelocity, Forward);
	const float RightVLat = FVector::DotProduct(RightVelocity, Lateral);

	// 左右履带各自摩擦系数（多点接触取均值；材质缺失回退默认地面摩擦）
	const float LeftMu = LeftContactCount > 0 ? (LeftFrictionSum / LeftContactCount) * TireFrictionScale : GroundFriction * TireFrictionScale;
	const float RightMu = RightContactCount > 0 ? (RightFrictionSum / RightContactCount) * TireFrictionScale : GroundFriction * TireFrictionScale;
	const float LeftMaxForceUE = LeftMu * LeftLoadUE;
	const float RightMaxForceUE = RightMu * RightLoadUE;

	float LeftLongForceUE = 0.0f;
	float LeftLatForceUE = 0.0f;
	float RightLongForceUE = 0.0f;
	float RightLatForceUE = 0.0f;
	if (LeftLoadUE > KINDA_SMALL_NUMBER)
	{
		// 伺服律：推力 ∝ (目标速度 − 实际速度)，钳制在摩擦上限内（超出即空转）
		LeftLongForceUE = FMath::Clamp(TrackGripStiffness * (LeftTarget - LeftVLong), -LeftMaxForceUE, LeftMaxForceUE);
		LeftLatForceUE = FMath::Clamp(-TrackLateralStiffness * LeftVLat, -LeftMaxForceUE, LeftMaxForceUE); // 侧滑恢复力
		// 力施加在履带接地点（产生推力+转向力偶）
		Chassis->AddForceAtLocation(Forward * LeftLongForceUE + Lateral * LeftLatForceUE, LeftPoint);
	}
	if (RightLoadUE > KINDA_SMALL_NUMBER)
	{
		RightLongForceUE = FMath::Clamp(TrackGripStiffness * (RightTarget - RightVLong), -RightMaxForceUE, RightMaxForceUE);
		RightLatForceUE = FMath::Clamp(-TrackLateralStiffness * RightVLat, -RightMaxForceUE, RightMaxForceUE);
		Chassis->AddForceAtLocation(Forward * RightLongForceUE + Lateral * RightLatForceUE, RightPoint);
	}

	// Soil resistance opposing the turn keeps skid steering from overshooting.
	const float YawRate = Chassis->GetPhysicsAngularVelocityInRadians().Z;
	if (TrackTurnResistance > 0.0f)
	{
		Chassis->AddTorqueInRadians(FVector(0.0f, 0.0f, -TrackTurnResistance * YawRate));
	}

	// Telemetry-friendly per-wheel state: report the track-level slip.
	// 遥测回填：把履带级滑移/受力折算到每轮状态（供 HUD/遥测/测试用例）
	for (FQuarryWheelState& Wheel : Wheels)
	{
		const bool bLeft = Wheel.MountOffset.Y < 0.0f;
		const float Target = bLeft ? LeftTarget : RightTarget;
		const float Actual = bLeft ? LeftVLong : RightVLong;
		const float LateralSpeed = bLeft ? LeftVLat : RightVLat;
		Wheel.SlipRatio = (Target - Actual) / FMath::Max(FMath::Abs(Actual), 150.0f); // 履带滑转率
		Wheel.SlipAngleRad = FMath::Atan2(LateralSpeed, FMath::Abs(Actual) + 5.0f);
		Wheel.TireLongForceUE = bLeft ? LeftLongForceUE : RightLongForceUE;
		Wheel.TireLatForceUE = bLeft ? LeftLatForceUE : RightLatForceUE;
		Wheel.AngularVelocityRadS = Actual / FMath::Max(1.0f, Wheel.Radius);
	}

	// 总驱动推力（N）→ HUD/遥测展示
	LastDriveForce = (LeftLongForceUE + RightLongForceUE) / UeForcePerNewton;

	// Hydraulic pump RPM follows demand; gear reflects travel direction.
	// 液压泵转速跟随油门需求（挖掘机无变速箱，转速≈负载指标）
	EngineRPM = FMath::FInterpTo(EngineRPM,
		EngineIdleRPM + FMath::Abs(ThrottleCommand) * FMath::Max(0.0f, EngineMaxRPM - EngineIdleRPM), DeltaTime, 4.0f);
	if (FMath::Abs(SpeedCmd) < 1.0f)
	{
		CurrentGear = 0;
	}
	else
	{
		CurrentGear = SpeedCmd > 0.0f ? 1 : -1;
	}
}

void UQuarryVehicleMovementComponent::UpdatePowertrain(float DeltaTime)
{
	// 动力总成：发动机扭矩曲线 × 变速箱 × 主减速器 → 每轮驱动扭矩
	const int32 NumForwardGears = FMath::Max(1, ForwardGearRatios.Num());
	const float ForwardSpeed = GetForwardSpeed();

	// 挡位方向切换：仅允许接近静止时倒挡/前进挡互换（模拟同步器）
	if (ThrottleCommand < -0.02f && CurrentGear > 0 && ForwardSpeed < 30.0f)
	{
		CurrentGear = -1;
		ShiftTimer = ShiftDuration;
	}
	else if (ThrottleCommand > 0.02f && CurrentGear < 0 && ForwardSpeed > -30.0f)
	{
		CurrentGear = 1;
		ShiftTimer = ShiftDuration;
	}
	if (CurrentGear == 0)
	{
		CurrentGear = 1;
	}

	// 驱动轮平均角速度（rad/s）——发动机转速的反馈来源
	float MeanDrivenOmega = 0.0f;
	int32 DrivenCount = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		if (Wheel.bDriven)
		{
			MeanDrivenOmega += Wheel.AngularVelocityRadS;
			++DrivenCount;
		}
	}
	if (DrivenCount == 0)
	{
		DrivenCount = 1;
	}
	MeanDrivenOmega /= DrivenCount;

	auto GearRatioOf = [this, NumForwardGears](int32 Gear) -> float // 挡位 → 传动比
	{
		if (Gear < 0)
		{
			return ReverseGearRatio;
		}
		const int32 Index = FMath::Clamp(Gear, 1, NumForwardGears) - 1;
		return ForwardGearRatios.Num() > 0 ? ForwardGearRatios[Index] : 1.0f;
	};

	const float GearRatio = GearRatioOf(CurrentGear);
	const float TotalRatio = GearRatio * FinalDriveRatio;

	// 发动机转速被轮速通过传动比反向拖动（液力耦合的软化由 FInterpTo 模拟）
	const float TargetRPM = FMath::Abs(MeanDrivenOmega) * TotalRatio * RadiansToRPM;
	EngineRPM = FMath::FInterpTo(EngineRPM,
		FMath::Clamp(TargetRPM, EngineIdleRPM, EngineMaxRPM), DeltaTime, 6.0f);

	// Automatic shifting (torque interrupted during shifts).
	if (ShiftTimer > 0.0f)
	{
		ShiftTimer -= DeltaTime;
	}
	else if (CurrentGear > 0 && NumForwardGears > 1)
	{
		if (EngineRPM > ShiftUpRPM && CurrentGear < NumForwardGears && FMath::Abs(ThrottleCommand) > 0.1f) // 到升挡转速
		{
			++CurrentGear;
			ShiftTimer = ShiftDuration;
		}
		else if (EngineRPM < ShiftDownRPM && CurrentGear > 1)
		{
			--CurrentGear;
			ShiftTimer = ShiftDuration * 0.5f;
		}
	}

	// 分段线性扭矩曲线：怠速 0.55 → 峰值转速 1.0 → 红线 0.72（外特性简化）
	float TorqueShape = 1.0f;
	if (EngineRPM <= EnginePeakTorqueRPM)
	{
		TorqueShape = FMath::Lerp(0.55f, 1.0f, FMath::Clamp(
			(EngineRPM - EngineIdleRPM) / FMath::Max(1.0f, EnginePeakTorqueRPM - EngineIdleRPM), 0.0f, 1.0f));
	}
	else
	{
		TorqueShape = FMath::Lerp(1.0f, 0.72f, FMath::Clamp(
			(EngineRPM - EnginePeakTorqueRPM) / FMath::Max(1.0f, EngineMaxRPM - EnginePeakTorqueRPM), 0.0f, 1.0f));
	}

	// Peak torque is derived from the current EngineForce unless set explicitly,
	// matching the legacy EngineForce as first-gear wheel force at full throttle.
	const float PeakTorqueNm = EnginePeakTorqueNm > 0.0f ? EnginePeakTorqueNm
		: EngineForce * GetMeanDrivenWheelRadiusM()
			/ FMath::Max(0.1f, GearRatioOf(1) * FinalDriveRatio * DrivelineEfficiency);
	// 净输出扭矩 = 峰值 × 曲线形状 × 油门
	float EngineTorqueNm = PeakTorqueNm * TorqueShape * ThrottleCommand;
	if (FMath::Abs(ThrottleCommand) < 0.05f && FMath::Abs(ForwardSpeed) > 20.0f)
	{
		// 发动机制动：松油门且在动时，扭矩反号阻碍运转（柴油机排气制动简化）
		EngineTorqueNm = -EngineBrakeTorqueNm * FMath::Clamp(EngineRPM / EngineMaxRPM, 0.0f, 1.0f)
			* FMath::Sign(ForwardSpeed);
	}

	// Soft speed limiter: no drive torque pushing beyond the speed limits.
	// 软限速：超出当前方向限速时禁止继续加速（不再给正向扭矩）
	const float SpeedLimit = CurrentGear < 0 ? MaxReverseSpeed : MaxForwardSpeed;
	if (FMath::Abs(ForwardSpeed) > SpeedLimit && FMath::Sign(ForwardSpeed) == FMath::Sign(ThrottleCommand))
	{
		EngineTorqueNm = FMath::Min(EngineTorqueNm, 0.0f);
	}

	// 换挡中断动力 → 轮驱动扭矩 = 发动机扭矩 × 总传动比 × 效率 ÷ 驱动轮数
	WheelDriveTorqueNm = ShiftTimer > 0.0f ? 0.0f
		: EngineTorqueNm * GearRatio * FinalDriveRatio * DrivelineEfficiency / DrivenCount;
}

void UQuarryVehicleMovementComponent::UpdateSuspension(FQuarryWheelState& Wheel, float DeltaTime)
{
	// 悬架：raycast 从悬架顶向下探测地面 → 弹簧阻尼力决定轮的支撑
	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	const FVector UpVector = Chassis->GetUpVector();
	const FVector MountLocation = ChassisTransform.TransformPosition(Wheel.MountOffset); // 悬架顶（世界）
	// 射线长度 = 静止长 + 行程 + 轮半径（保证满压缩/满伸展都能探测）
	const float TraceLength = SuspensionRestLength + SuspensionTravel + Wheel.Radius;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(QuarryWheelTrace), false, GetOwner());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, MountLocation, MountLocation - UpVector * TraceLength, QuarryCollision::Terrain, QueryParams);

	Wheel.bInContact = bHit;
	if (!bHit)   // 悬空：悬架自然伸展到满行程
	{
		Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, SuspensionRestLength + SuspensionTravel, DeltaTime, 8.0f);
		Wheel.SuspensionVelocity = 0.0f;
		Wheel.SuspensionCompression = 0.0f;
		Wheel.LoadForceUE = 0.0f;   // 悬空轮无支撑力
		return;
	}

	++GroundedWheelCount;   // 接地轮计数（遥测 wheel_contact 用例）
	GroundHeightSum += Hit.ImpactPoint.Z + Wheel.Radius - Wheel.MountOffset.Z;
	Wheel.ContactPoint = Hit.ImpactPoint;      // 接地点（轮胎力施加点）
	Wheel.ContactNormal = Hit.ImpactNormal;    // 地面法线
	Wheel.ContactFriction = ResolveContactFriction(Hit); // 材质摩擦（沙地/硬地区分）

	// 悬架当前长度 = raycast 距离 − 轮半径（钳制在 [0, 满行程]）
	const float MaxLength = SuspensionRestLength + SuspensionTravel;
	const float CurrentLength = FMath::Clamp(Hit.Distance - Wheel.Radius, 0.0f, MaxLength);
	Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, CurrentLength, DeltaTime, 30.0f);

	const float PreviousCompression = Wheel.SuspensionCompression;
	Wheel.SuspensionCompression = MaxLength - CurrentLength;
	// 压缩速率（cm/s）——阻尼力的输入
	Wheel.SuspensionVelocity = (Wheel.SuspensionCompression - PreviousCompression) / FMath::Max(DeltaTime, 1e-4f);

	// Spring + damper along the chassis up axis, applied at the wheel mount.
	// 弹簧阻尼力 F = k·x + c·ẋ（只能推不能拉 → 下限 0）
	const float SuspensionForceUE = FMath::Clamp(
		SuspensionStiffness * Wheel.SuspensionCompression + SuspensionDamping * Wheel.SuspensionVelocity,
		0.0f, SuspensionMaxForce);
	Wheel.LoadForceUE = SuspensionForceUE;
	// 力沿底盘上方向施加在悬架顶（产生支撑 + 俯仰/侧倾力矩）
	Chassis->AddForceAtLocation(UpVector * SuspensionForceUE, MountLocation);
}

void UQuarryVehicleMovementComponent::ApplyAntiRollForces()
{
	// 防倾杆：左右轮压缩差 → 一对上下反向力（抑制过弯侧倾）
	if (AntiRollStiffness <= 0.0f || Wheels.Num() < 2)
	{
		return;
	}

	TArray<bool> Paired;
	Paired.Init(false, Wheels.Num());
	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	const FVector Up = Chassis->GetUpVector();
	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		if (Paired[i])
		{
			continue;
		}
		for (int32 j = i + 1; j < Wheels.Num(); ++j)
		{
			if (Paired[j])
			{
				continue;
			}
			const FQuarryWheelState& A = Wheels[i];
			const FQuarryWheelState& B = Wheels[j];
			if (FMath::Abs(A.MountOffset.X - B.MountOffset.X) > 1.0f
				|| FMath::Sign(A.MountOffset.Y) == FMath::Sign(B.MountOffset.Y))
			{
				continue;
			}
			Paired[i] = true;
			Paired[j] = true;
			if (A.bInContact && B.bInContact)
			{
				// 压缩差越大，反向力偶越强（抗侧倾的核心）
			const float ForceUE = AntiRollStiffness * (A.SuspensionCompression - B.SuspensionCompression);
				Chassis->AddForceAtLocation(Up * ForceUE, ChassisTransform.TransformPosition(A.MountOffset));
				Chassis->AddForceAtLocation(Up * -ForceUE, ChassisTransform.TransformPosition(B.MountOffset));
			}
			break;
		}
	}
}

float UQuarryVehicleMovementComponent::GetBrakeTorqueNm() const
{
	const float EffectiveBrake = FMath::Max(BrakeCommand, bParkingBrake ? 1.0f : 0.0f);
	if (EffectiveBrake <= 0.01f)
	{
		return 0.0f;
	}
	const int32 NumWheels = FMath::Max(1, Wheels.Num());
	// 制动扭矩 = 制动力 × 轮半径 ÷ 轮数 × 制动深度（均分到每轮）
	return BrakeForce * GetMeanDrivenWheelRadiusM() / NumWheels * EffectiveBrake;
}

float UQuarryVehicleMovementComponent::ResolveContactFriction(const FHitResult& Hit) const
{
	if (const UPhysicalMaterial* PhysMaterial = Hit.PhysMaterial.Get())
	{
		// Level floors often ship with a low demo friction; a quarry surface is
		// compacted ground, so never fall below ~85% of the calibrated value.
		const float FloorFriction = FMath::Clamp(PhysMaterial->Friction, 0.05f, 2.0f);
		return FMath::Max(FloorFriction, GroundFriction * 0.85f);
	}
	// 挖掘场料堆 = 松散物料（低摩擦 → 打滑/下陷）
	if (const AActor* HitActor = Hit.GetActor())
	{
		if (HitActor->IsA<AQuarryDigSite>())
		{
			return LooseMaterialFriction;   // 沙堆专用低摩擦
		}
	}
	return GroundFriction;
}

void UQuarryVehicleMovementComponent::UpdateTireForces(FQuarryWheelState& Wheel, float DeltaTime, float DriveTorqueNmPerWheel)
{
	// 轮胎层：滑移率/侧偏角 → Pacejka 力，摩擦圆联合钳制，力施加在接地点
	if (!Wheel.bInContact)
	{
		Wheel.SlipRatio = 0.0f;   // 悬空轮无滑移
		Wheel.SlipAngleRad = 0.0f;
		Wheel.TireLongForceUE = 0.0f;
		Wheel.TireLatForceUE = 0.0f;
		Wheel.LoadForceUE = 0.0f;

		// 悬空：驱动/制动扭矩仍然作用于轮自转（空转），仅损失少量风阻
		float AngularVelocity = Wheel.AngularVelocityRadS
			+ DriveTorqueNmPerWheel / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
		AngularVelocity *= FMath::Exp(-0.25f * DeltaTime);
		// 制动扭矩 → 角减速度步长（Δω = T/I·dt）
	const float BrakeOmegaStep = GetBrakeTorqueNm() / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
		if (FMath::Abs(AngularVelocity) <= BrakeOmegaStep)
		{
			AngularVelocity = 0.0f;
		}
		else
		{
			AngularVelocity -= FMath::Sign(AngularVelocity) * BrakeOmegaStep;
		}
		Wheel.AngularVelocityRadS = AngularVelocity;
		return;
	}

	const FVector Up = Chassis->GetUpVector();
	const FVector Forward = Chassis->GetForwardVector();
	// 转向轮的滚动方向 = 车头方向绕上轴旋转转向角
	const FVector SteeredForward = Wheel.SteerAngleRad != 0.0f
		? Forward.RotateAngleAxis(FMath::RadiansToDegrees(Wheel.SteerAngleRad), Up)
		: Forward;
	const FVector Lateral = FVector::CrossProduct(Up, SteeredForward);

	// 接地点瞬时速度（含车体旋转的分量——侧偏角的来源）
	const FVector ContactVelocity = Chassis->GetPhysicsLinearVelocityAtPoint(Wheel.ContactPoint);
	const float VLong = FVector::DotProduct(ContactVelocity, SteeredForward);
	const float VLat = FVector::DotProduct(ContactVelocity, Lateral);

	const float WheelSpeedCmS = Wheel.AngularVelocityRadS * Wheel.Radius;
	// 滑移率 κ = (轮速 − 车速) / max(|车速|,|轮速|, 25)——分母下限防低速除零发散
	const float SlipDenominator = FMath::Max(FMath::Max(FMath::Abs(VLong), FMath::Abs(WheelSpeedCmS)), MinSlipSpeedCmS);
	Wheel.SlipRatio = (WheelSpeedCmS - VLong) / SlipDenominator;
	Wheel.SlipAngleRad = FMath::Atan2(VLat, FMath::Abs(VLong) + 5.0f);

	// 简化 Pacejka 魔术公式：F = μN·sin(C·atan(B·slip))
	const float Friction = FMath::Max(0.05f, Wheel.ContactFriction) * TireFrictionScale;
	const float MaxGripForceUE = Friction * Wheel.LoadForceUE;
	float LongForceUE = MaxGripForceUE * FMath::Sin(TireShapeFactor * FMath::Atan(TireSlipStiffness * Wheel.SlipRatio));
	float LatForceUE = -MaxGripForceUE * FMath::Sin(TireShapeFactor * FMath::Atan(TireSlipStiffness * Wheel.SlipAngleRad));

	// 摩擦圆：纵向+侧向合力不能超过 μN（超出按比例缩回——抱死/侧滑的表现）
	const float CombinedForce = FVector2D(LongForceUE, LatForceUE).Size();
	if (CombinedForce > MaxGripForceUE && CombinedForce > KINDA_SMALL_NUMBER)
	{
		const float Scale = MaxGripForceUE / CombinedForce;
		LongForceUE *= Scale;
		LatForceUE *= Scale;
	}

	// 滚动阻力：与滚动方向相反，正比于载荷
	LongForceUE -= FMath::Sign(VLong) * RollingResistance * Wheel.LoadForceUE;

	// 合力施加在接地点（推进/侧偏/滚动阻力的合力）
	Chassis->AddForceAtLocation(SteeredForward * LongForceUE + Lateral * LatForceUE, Wheel.ContactPoint);
	Wheel.TireLongForceUE = LongForceUE;
	Wheel.TireLatForceUE = LatForceUE;
	LastDriveForce += LongForceUE / UeForcePerNewton;

	// Wheel spin integration: drive torque vs tire reaction torque and brake.
	// 地面反作用扭矩（驱动轮的负载——空转/打滑的物理来源）
	const float TireReactionNm = LongForceUE / UeForcePerNewton * (Wheel.Radius * 0.01f);
	float AngularVelocity = Wheel.AngularVelocityRadS
		+ (DriveTorqueNmPerWheel - TireReactionNm) / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
	const float BrakeOmegaStep = GetBrakeTorqueNm() / FMath::Max(0.5f, WheelInertiaKgM2) * DeltaTime;
	if (FMath::Abs(AngularVelocity) <= BrakeOmegaStep)   // 一步之内可刹停 → 直接抱死
	{
		AngularVelocity = 0.0f;
	}
	Wheel.AngularVelocityRadS = AngularVelocity;
}

// 旧运动学直驱（bLegacyKinematic=true 的回退路径）：直接设速度与旋转，
// 无打滑/悬架——保留用于调试对照，正常玩法不使用。
void UQuarryVehicleMovementComponent::TickLegacyKinematic(float DeltaTime)
{
	GroundedWheelCount = 0;
	GroundHeightSum = 0.0f;
	LastDriveForce = 0.0f;
	int32 DrivenWheelCount = 0;
	for (const FQuarryWheelState& Wheel : Wheels)
	{
		DrivenWheelCount += Wheel.bDriven ? 1 : 0;
	}

	for (FQuarryWheelState& Wheel : Wheels)
	{
		SimulateWheel(Wheel, DeltaTime);
	}

	const float TotalMass = FMath::Max(1.0f, BaseMassKg + PayloadMassKg);
	const float EffectiveBrake = FMath::Max(BrakeCommand, bParkingBrake ? 1.0f : 0.0f);
	const float TargetSpeed = EffectiveBrake > 0.01f ? 0.0f
		: ThrottleCommand * (ThrottleCommand >= 0.0f ? MaxForwardSpeed : MaxReverseSpeed);
	float Acceleration = FMath::Abs(ThrottleCommand) > 0.02f ? EngineForce * 100.0f / TotalMass : 50.0f;
	if (EffectiveBrake > 0.01f)
	{
		Acceleration = BrakeForce * 100.0f / TotalMass * EffectiveBrake;
	}
	const float CurrentSpeed = GetForwardSpeed();
	const float ForwardSpeed = CurrentSpeed + FMath::Clamp(TargetSpeed - CurrentSpeed, -Acceleration * DeltaTime, Acceleration * DeltaTime);
	LastDriveForce = ThrottleCommand * EngineForce;

	float YawRate = 0.0f;
	if (DriveType == EQuarryDriveType::Tracked)
	{
		YawRate = SteeringCommand * 45.0f;
	}
	else if (FMath::Abs(ForwardSpeed) > 5.0f)
	{
		YawRate = SteeringCommand * 45.0f * FMath::Clamp(FMath::Abs(ForwardSpeed) / 350.0f, 0.0f, 1.0f)
			* FMath::Sign(ForwardSpeed);
	}
	if (FMath::Abs(YawRate) > 0.01f)   // 直接转Actor（绕过轮胎物理）
	{
		GetOwner()->SetActorRotation(GetOwner()->GetActorRotation() + FRotator(0.0f, YawRate * DeltaTime, 0.0f), ETeleportType::TeleportPhysics);
	}

	float VerticalVelocity = Chassis->GetPhysicsLinearVelocity().Z - 981.0f * DeltaTime;
	if (GroundedWheelCount > 0)
	{
		const float TargetHeight = GroundHeightSum / GroundedWheelCount + SuspensionRestLength;
		VerticalVelocity = FMath::Clamp((TargetHeight - Chassis->GetComponentLocation().Z) * 6.0f, -350.0f, 350.0f);
	}
	// 直接写入线速度（力驱动物理开启前旧 demo 的实现方式）
	Chassis->SetPhysicsLinearVelocity(Chassis->GetForwardVector() * ForwardSpeed + FVector(0.0, 0.0, VerticalVelocity));
}

void UQuarryVehicleMovementComponent::SimulateWheel(FQuarryWheelState& Wheel, float DeltaTime)
{
	const FTransform ChassisTransform = Chassis->GetComponentTransform();
	const FVector MountLocation = ChassisTransform.TransformPosition(Wheel.MountOffset);
	const FVector UpVector = Chassis->GetUpVector();
	const float TraceLength = SuspensionRestLength + Wheel.Radius + SuspensionTravel;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(QuarryWheelTrace), false, GetOwner());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, MountLocation, MountLocation - UpVector * TraceLength, QuarryCollision::Terrain, QueryParams);
	if (!bHit)
	{
		Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, SuspensionRestLength + SuspensionTravel, DeltaTime, 8.0f);
		return;
	}

	++GroundedWheelCount;
	GroundHeightSum += Hit.ImpactPoint.Z + Wheel.Radius - Wheel.MountOffset.Z;
	const float TargetLength = FMath::Clamp(Hit.Distance - Wheel.Radius, 0.0f, SuspensionRestLength + SuspensionTravel);
	Wheel.SuspensionLength = FMath::FInterpTo(Wheel.SuspensionLength, TargetLength, DeltaTime, 12.0f);
}

// 轮网格视觉驱动（每轮每帧）：
//   位置 = 悬架顶 − 悬架长度（跟随悬架压缩上下浮动）
//   旋转 = 基座 + (自转角, 转向角, 0)——自转绕轮轴（Y），转向绕上轴（Z）
void UQuarryVehicleMovementComponent::UpdateWheelVisual(FQuarryWheelState& Wheel, float DeltaTime)
{
	if (!Wheel.Visual)
	{
		return;
	}

	// 轮心 = 悬架顶 − 悬架长度（悬架压缩时轮上移、伸展时下移）
	Wheel.Visual->SetRelativeLocation(Wheel.MountOffset - FVector(0.0, 0.0, Wheel.SuspensionLength));
	// 滚动方向：前进时绕 −Y 轴旋转（右手定则），故 ω 取负
	Wheel.SpinAngle = FMath::UnwindDegrees(Wheel.SpinAngle
		- FMath::RadiansToDegrees(Wheel.AngularVelocityRadS) * DeltaTime);
	// 最终旋转 = 基座 + (自转 Pitch, 转向 Yaw)——转向轮才有 Yaw 分量
	Wheel.Visual->SetRelativeRotation(Wheel.BaseVisualRotation
		+ FRotator(Wheel.SpinAngle, FMath::RadiansToDegrees(Wheel.SteerAngleRad), 0.0f));
}
