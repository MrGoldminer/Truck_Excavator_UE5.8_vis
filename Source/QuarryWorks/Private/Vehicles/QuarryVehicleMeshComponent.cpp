#include "Vehicles/QuarryVehicleMeshComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Vehicles/QuarryVehicleMovementComponent.h"

UQuarryVehicleMeshComponent::UQuarryVehicleMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

// ============================================================================
// Initialize —— 资产绑定总入口（按 BodyMeshPath 的类型三路分发）：
//   骨骼网格（SportsCar 式：轮骨重建轮位 + 隐藏烘焙轮骨）
//   静态网格（DumpTruck/整机：显式轮位 + 可选整体滚动）
//   分件装配（PartBindings：部件挂关节链 + 铰接驱动）
// 统一副作用：PartsToHide 里的方块件全部隐藏
// ============================================================================
void UQuarryVehicleMeshComponent::Initialize(UQuarryVehicleMovementComponent* InMovement, USceneComponent* AttachRoot,
	const TArray<UPrimitiveComponent*>& PartsToHide)
{
	if (bInitialized || !InMovement || !AttachRoot || !GetOwner() || GetOwner()->HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *BodyMeshPath);
	UStaticMesh* StaticBody = nullptr;
	if (!Mesh)
	{
		StaticBody = LoadObject<UStaticMesh>(nullptr, *BodyStaticMeshPath);
	}
	const bool bHasTracks = !TracksMeshPath.IsEmpty() || !WheelsMeshPath.IsEmpty();
	if (!Mesh && !StaticBody && !bHasTracks)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuarryVehicleMesh: failed to load body meshes '%s' / '%s'; keeping blockout body"), *BodyMeshPath, *BodyStaticMeshPath);
		return;
	}

	Movement = InMovement;
	AttachRootComponent = AttachRoot;

	float FitScale = BodyScale;
	if (StaticBody)
	{
		if (BodyFitLength > 0.0f)
		{
			// Authoring axes vary: the vehicle length is the longest horizontal extent.
			const FBoxSphereBounds Bounds = StaticBody->GetBounds();
			const float NativeLength = FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * 2.0f;
			if (NativeLength > 1.0f)
			{
				FitScale = BodyFitLength / NativeLength;
				UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: static body native length %.0f cm, auto-fit scale %.3f"), NativeLength, FitScale);
			}
		}
		BodyMeshStatic = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("RealBodyMeshStatic"));
		BodyMeshStatic->SetupAttachment(AttachRoot);
		BodyMeshStatic->SetRelativeLocation(FVector(BodyOffsetX, 0.0f, BodyZOffset));
		BodyMeshStatic->SetRelativeRotation(FRotator(0.0f, BodyYawOffsetDeg, 0.0f));
		BodyMeshStatic->SetRelativeScale3D(FVector(FitScale));
		BodyMeshStatic->SetStaticMesh(StaticBody);
		BodyMeshStatic->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BodyMeshStatic->RegisterComponent();
		UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: static body '%s' active (fitted %.0f x %.0f x %.0f cm)"),
			*BodyStaticMeshPath,
			StaticBody->GetBounds().BoxExtent.X * 2.0f * FitScale,
			StaticBody->GetBounds().BoxExtent.Y * 2.0f * FitScale,
			StaticBody->GetBounds().BoxExtent.Z * 2.0f * FitScale);
	}
	else if (Mesh)
	{
		BodyMesh = NewObject<USkeletalMeshComponent>(GetOwner(), TEXT("RealBodyMesh"));
		BodyMesh->SetupAttachment(AttachRoot);
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, BodyZOffset));
		BodyMesh->SetRelativeRotation(FRotator(0.0f, BodyYawOffsetDeg, 0.0f));
		BodyMesh->SetRelativeScale3D(FVector(BodyScale));
		BodyMesh->SetSkeletalMesh(Mesh);
		BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BodyMesh->SetAnimationMode(EAnimationMode::AnimationCustomMode);
		BodyMesh->SetAnimInstanceClass(nullptr);
		BodyMesh->RegisterComponent();
	}

	for (UPrimitiveComponent* Part : PartsToHide)
	{
		if (Part)
		{
			Part->SetVisibility(false, true);
		}
	}

	DiscoverAndBindWheels();
	bInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: body '%s' active"), *BodyMeshPath);
}

void UQuarryVehicleMeshComponent::AttachTracksMeshes(USceneComponent* AttachRoot)
{
	if (!AttachRoot || (GetOwner() && GetOwner()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		return;
	}
	UStaticMesh* TracksMesh = TracksMeshPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *TracksMeshPath);
	UStaticMesh* RoadWheelsMesh = WheelsMeshPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *WheelsMeshPath);
	if (!TracksMesh && !RoadWheelsMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuarryVehicleMesh: track meshes not set; keeping blockout tracks"));
		return;
	}

	float Scale = TracksScale;
	if (Scale <= 0.0f && TracksMesh)
	{
		// Authoring axes vary: the track length is the longest horizontal extent.
		const FBoxSphereBounds Bounds = TracksMesh->GetBounds();
		const float NativeLength = FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * 2.0f;
		Scale = NativeLength > 1.0f ? TrackFitLength / NativeLength : 1.0f;
		UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: tracks native bounds %.0f x %.0f x %.0f cm, fit scale %.3f"),
			Bounds.BoxExtent.X * 2.0f, Bounds.BoxExtent.Y * 2.0f, Bounds.BoxExtent.Z * 2.0f, Scale);
	}
	if (Scale <= 0.0f)
	{
		Scale = 1.0f;
	}

	auto AttachPart = [this, AttachRoot, Scale](UStaticMesh* Mesh, const TCHAR* Name)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner(), Name);
		Part->SetupAttachment(AttachRoot);
		Part->SetRelativeLocation(TracksOffset);
		Part->SetRelativeRotation(FRotator(TracksPitchOffsetDeg, TracksYawOffsetDeg, 0.0f));
		Part->SetRelativeScale3D(FVector(Scale));
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->RegisterComponent();
		return Part;
	};
	if (TracksMesh)
	{
		AttachPart(TracksMesh, TEXT("RealTracks"));
	}
	if (RoadWheelsMesh)
	{
		AttachPart(RoadWheelsMesh, TEXT("RealRoadWheels"));
	}
}

FVector UQuarryVehicleMeshComponent::BonePositionToRootSpace(const FVector& BoneComponentPos) const
{
	if (!BodyMesh || !AttachRootComponent)
	{
		return BoneComponentPos;
	}
	return BodyMesh->GetRelativeTransform().TransformPosition(BoneComponentPos);
}

void UQuarryVehicleMeshComponent::AttachPartsAssembly(USceneComponent* AttachRoot)
{
	if (!AttachRoot || PartBindings.IsEmpty()
		|| (GetOwner() && GetOwner()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		return;
	}

	// Auto-fit scale from the first binding's mesh (the machine proxy).
	float Scale = 1.0f;
	if (!PartBindings.IsEmpty() && PartsFitLength > 0.0f)
	{
		const FString& ProxyName = PartBindings[0].PartName;
		UStaticMesh* ProxyMesh = LoadObject<UStaticMesh>(nullptr, *(PartsFolder / (ProxyName + TEXT(".") + ProxyName)));
		if (ProxyMesh)
		{
			const FBoxSphereBounds Bounds = ProxyMesh->GetBounds();
			const float NativeLength = FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * 2.0f;
			if (NativeLength > 1.0f)
			{
				Scale = PartsFitLength / NativeLength;
				UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: parts proxy '%s' native length %.0f cm, fit scale %.3f"),
					*ProxyName, NativeLength, Scale);
			}
		}
	}

	// Real pivot chain in chassis space: boom joint -> stick offset -> bucket offset.
	USceneComponent* BoomJoint = NewObject<USceneComponent>(GetOwner(), TEXT("RealBoomJoint"));
	BoomJoint->SetupAttachment(AttachRoot);
	BoomJoint->SetRelativeLocation(BoomJointLocal * Scale);
	BoomJoint->RegisterComponent();
	USceneComponent* StickJoint = NewObject<USceneComponent>(GetOwner(), TEXT("RealStickJoint"));
	StickJoint->SetupAttachment(BoomJoint);
	StickJoint->SetRelativeLocation(StickJointOffset * Scale);
	StickJoint->RegisterComponent();
	USceneComponent* BucketJoint = NewObject<USceneComponent>(GetOwner(), TEXT("RealBucketJoint"));
	BucketJoint->SetupAttachment(StickJoint);
	BucketJoint->SetRelativeLocation(BucketJointOffset * Scale);
	BucketJoint->RegisterComponent();

	WheelPartVisuals.Reset();
	RealBoomJoint = BoomJoint;
	RealStickJoint = StickJoint;
	RealBucketJoint = BucketJoint;

	for (const FQuarryPartBinding& Binding : PartBindings)
	{
		const FString AssetPath = PartsFolder / (Binding.PartName + TEXT(".") + Binding.PartName);
		UStaticMesh* PartMesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
		if (!PartMesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("QuarryVehicleMesh: part '%s' failed to load"), *AssetPath);
			continue;
		}

		USceneComponent* Parent = Binding.ParentPivot == 1 ? BoomJoint
			: Binding.ParentPivot == 2 ? StickJoint
			: Binding.ParentPivot == 3 ? BucketJoint : AttachRoot;

		UStaticMeshComponent* PartComponent = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Part_%s"), *Binding.PartName));
		PartComponent->SetupAttachment(Parent);
		PartComponent->SetRelativeLocation(Binding.RelativeLocation * Scale);
		PartComponent->SetRelativeRotation(Binding.RelativeRotation);
		PartComponent->SetRelativeScale3D(FVector(Scale));
		PartComponent->SetStaticMesh(PartMesh);
		PartComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PartComponent->RegisterComponent();

		const bool bWheelPart = Binding.bSpinWithWheels;
		const FLinearColor PartColor = bWheelPart ? PartsWheelColor : PartsBodyColor;
		for (int32 Slot = 0; Slot < PartComponent->GetNumMaterials(); ++Slot)
		{
			if (PartComponent->GetMaterial(Slot) == nullptr)
			{
				UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_QuarryBlockout.M_QuarryBlockout"));
				if (Base)
				{
					UMaterialInstanceDynamic* Mid = PartComponent->CreateDynamicMaterialInstance(Slot, Base);
					if (Mid)
					{
						Mid->SetVectorParameterValue(TEXT("Color"), PartColor);
					}
				}
			}
		}

		if (bWheelPart)
		{
			// Map the part suffix (FL/FR/RL/RR) to the movement wheel index:
			// the excavator adds wheels in FL, FR, RL, RR order.
			int32 WheelIndex = 0;
			if (Binding.PartName.EndsWith(TEXT("FR"))) WheelIndex = 1;
			else if (Binding.PartName.EndsWith(TEXT("RL"))) WheelIndex = 2;
			else if (Binding.PartName.EndsWith(TEXT("RR"))) WheelIndex = 3;
			WheelPartVisuals.Add({PartComponent, Binding.RelativeRotation, Binding.RelativeLocation * Scale, WheelIndex, 0.0f});
		}
		UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: part '%s' attached to pivot %d"), *Binding.PartName, Binding.ParentPivot);
	}
	bInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: parts assembly complete (%d spinning wheels)"),
		WheelPartVisuals.Num());
}

void UQuarryVehicleMeshComponent::DriveArticulatedParts(float BoomAngleDeg, float StickAngleDeg, float BucketAngleDeg)
{
	// Rotating the JOINT components moves every attached part with the joint
	// pinned: part transforms (rest offsets) are relative to their joint and
	// inherit the rotation automatically.
	if (RealBoomJoint.IsValid())
	{
		RealBoomJoint->SetRelativeRotation(FRotator(BoomAngleDeg, 0.0f, 0.0f));
	}
	if (RealStickJoint.IsValid())
	{
		RealStickJoint->SetRelativeRotation(FRotator(StickAngleDeg, 0.0f, 0.0f));
	}
	if (RealBucketJoint.IsValid())
	{
		RealBucketJoint->SetRelativeRotation(FRotator(BucketAngleDeg, 0.0f, 0.0f));
	}
}

void UQuarryVehicleMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bInitialized || WheelPartVisuals.IsEmpty() || !Movement.IsValid())
	{
		return;
	}

	const UQuarryVehicleMovementComponent* VehicleMovement = Movement.Get();
	const TArray<FQuarryWheelState>& WheelStates = VehicleMovement->GetWheelStates();

	// Wheel parts: per-wheel suspension travel + per-wheel spin. The wheel
	// center hangs below its suspension top by SuspensionLength, mirroring
	// UpdateWheelVisual so the visual wheels track the physics wheels.
	for (FWheelPartVisual& Part : WheelPartVisuals)
	{
		if (!Part.Component || !WheelStates.IsValidIndex(Part.WheelIndex))
		{
			continue;
		}
		const FQuarryWheelState& Wheel = WheelStates[Part.WheelIndex];

		Part.SpinRad += Wheel.AngularVelocityRadS * DeltaTime * PartsSpinSign;
		const FQuat SpinQuat(FVector::RightVector, Part.SpinRad);

		Part.Component->SetRelativeLocation(
			Part.RestLocation - FVector(0.0f, 0.0f, Wheel.SuspensionLength - VehicleMovement->SuspensionRestLength));
		Part.Component->SetRelativeRotation(FQuat(Part.BaseRotation) * SpinQuat);
	}
}

FVector UQuarryVehicleMeshComponent::GetRealBucketTip() const
{
	// Forward kinematics of the joint chain: the tip offset is defined in the
	// bucket joint's rest frame (axes align with the chassis at rest), so the
	// bucket joint's current transform maps it to the world as the arm moves.
	if (RealBucketJoint.IsValid())
	{
		return RealBucketJoint->GetComponentTransform().TransformPosition(BucketTipLocal);
	}
	return FVector::ZeroVector;
}

void UQuarryVehicleMeshComponent::DiscoverAndBindWheels()
{
	UQuarryVehicleMovementComponent* VehicleMovement = Movement.Get();
	if (!VehicleMovement)
	{
		return;
	}
	if (BodyMeshStatic)
	{
		// Static bodies have no wheel bones: use explicit mounts when provided.
		if (StaticBodyWheelMounts.Num() > 0)
		{
			UStaticMesh* WheelMesh = LoadObject<UStaticMesh>(nullptr, *WheelMeshPath);
			VehicleMovement->ClearWheels();
			float MaxMountX = StaticBodyWheelMounts[0].X;
			for (const FVector& Mount : StaticBodyWheelMounts)
			{
				MaxMountX = FMath::Max(MaxMountX, Mount.X);
			}
			for (const FVector& Mount : StaticBodyWheelMounts)
			{
				USceneComponent* WheelVisual = nullptr;
				if (WheelMesh)
				{
					UStaticMeshComponent* WheelComponent = NewObject<UStaticMeshComponent>(GetOwner(),
						*FString::Printf(TEXT("BodyWheel_%.0f_%.0f"), Mount.X, Mount.Y));
					WheelComponent->SetupAttachment(AttachRootComponent);
					WheelComponent->SetRelativeLocation(Mount);
					WheelComponent->SetRelativeRotation(WheelBaseRotation);
					WheelComponent->SetRelativeScale3D(FVector(StaticBodyWheelMeshScale));
					WheelComponent->SetStaticMesh(WheelMesh);
					WheelComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					WheelComponent->RegisterComponent();
					WheelVisual = WheelComponent;
				}
				const bool bFront = Mount.X > MaxMountX - 25.0f;
				VehicleMovement->AddWheel(Mount, StaticBodyWheelRadius, true, bFront && bSteerFrontBones, WheelVisual);
			}
			UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: rebuilt %d wheel mounts from StaticBodyWheelMounts"), StaticBodyWheelMounts.Num());
		}
		return;
	}
	if (!BodyMesh || !BodyMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	const TArray<FTransform>& ComponentBones = BodyMesh->GetComponentSpaceTransforms();
	const int32 NumBones = ComponentBones.Num();

	// Collect wheel bones by name and sort left first, front to back, so the
	// mapping between bones, rebuilt mounts, and telemetry stays stable.
	struct FWheelBoneCandidate
	{
		int32 BoneIndex;
		FVector RootSpacePos;
	};
	TArray<FWheelBoneCandidate> Candidates;
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FString BoneNameStr = BodyMesh->GetBoneName(BoneIndex).ToString();
		if (BoneNameStr.Contains(WheelBoneFilter, ESearchCase::IgnoreCase)
			&& !BoneNameStr.Contains(TEXT("steering"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: wheel-bone candidate '%s' at (%.0f, %.0f, %.0f)"),
				*BoneNameStr, ComponentBones[BoneIndex].GetTranslation().X,
				ComponentBones[BoneIndex].GetTranslation().Y, ComponentBones[BoneIndex].GetTranslation().Z);
			Candidates.Add({ BoneIndex, BonePositionToRootSpace(ComponentBones[BoneIndex].GetTranslation()) });
		}
	}
	if (Candidates.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("QuarryVehicleMesh: no bones matching '%s' in %s; keeping existing wheel mounts"),
			*WheelBoneFilter, *BodyMeshPath);
		return;
	}
	Candidates.Sort([](const FWheelBoneCandidate& A, const FWheelBoneCandidate& B)
	{
		const bool bALeft = A.RootSpacePos.Y < 0.0f;
		const bool bBLeft = B.RootSpacePos.Y < 0.0f;
		if (bALeft != bBLeft)
		{
			return bALeft;
		}
		return A.RootSpacePos.X > B.RootSpacePos.X;
	});

	// Cluster wheel bones into physical wheels: Chaos skeletons model each
	// wheel as a start+end bone pair (axle-aligned, ~30 cm apart). Bones within
	// 40 cm on the same side of the car belong to one wheel; use the midpoint.
	TArray<FWheelBoneCandidate> Clustered;
	for (const FWheelBoneCandidate& Candidate : Candidates)
	{
		bool bMerged = false;
		for (FWheelBoneCandidate& Kept : Clustered)
		{
			if (FVector::Dist2D(Kept.RootSpacePos, Candidate.RootSpacePos) < 40.0f)
			{
				Kept.RootSpacePos = (Kept.RootSpacePos + Candidate.RootSpacePos) * 0.5f;
				bMerged = true;
				break;
			}
		}
		if (!bMerged)
		{
			Clustered.Add(Candidate);
		}
	}
	Candidates = MoveTemp(Clustered);
	UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: %d physical wheel mounts after bone clustering"), Candidates.Num());

	if (bHideBakedWheelBones)
	{
		TArray<int32> WheelBoneIndices;
		WheelBoneIndices.Reserve(Candidates.Num());
		for (const FWheelBoneCandidate& Candidate : Candidates)
		{
			WheelBoneIndices.Add(Candidate.BoneIndex);
		}
		HideBakedWheelBones(WheelBoneIndices);
	}

	if (!bRebuildWheelsFromBones)
	{
		return;
	}

	// Rebuild the movement's wheel mounts at the real bone positions and attach
	// real wheel meshes; the movement component drives location/spin/steer.
	UStaticMesh* WheelMesh = LoadObject<UStaticMesh>(nullptr, *WheelMeshPath);
	if (!WheelMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuarryVehicleMesh: wheel mesh '%s' not found; using fallback tire size"), *WheelMeshPath);
	}

	VehicleMovement->ClearWheels();
	const float MaxX = Candidates[0].RootSpacePos.X;
	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const FWheelBoneCandidate& Candidate = Candidates[CandidateIndex];
		const bool bFront = FMath::IsNearlyEqual(Candidate.RootSpacePos.X, MaxX, 25.0f);
		USceneComponent* WheelVisual = nullptr;
		if (WheelMesh)
		{
			UStaticMeshComponent* WheelComponent = NewObject<UStaticMeshComponent>(GetOwner(),
				*FString::Printf(TEXT("RealWheel_%d"), CandidateIndex));
			WheelComponent->SetupAttachment(AttachRootComponent);
			WheelComponent->SetRelativeLocation(Candidate.RootSpacePos);
			WheelComponent->SetRelativeRotation(WheelBaseRotation);
			WheelComponent->SetStaticMesh(WheelMesh);
			WheelComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			WheelComponent->RegisterComponent();
			WheelVisual = WheelComponent;
		}
		VehicleMovement->AddWheel(Candidate.RootSpacePos, RebuiltWheelRadius, true, bFront && bSteerFrontBones, WheelVisual);
	}
	UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: rebuilt %d wheel mounts from bone positions"), Candidates.Num());
}

void UQuarryVehicleMeshComponent::HideBakedWheelBones(const TArray<int32>& WheelBoneIndices)
{
	if (!BodyMesh || !BodyMesh->GetSkeletalMeshAsset())
	{
		return;
	}
	const FReferenceSkeleton& RefSkeleton = BodyMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const TArray<FTransform>& ComponentBones = BodyMesh->GetComponentSpaceTransforms();
	const int32 NumBones = ComponentBones.Num();

	TArray<uint8>& Visibility = BodyMesh->GetEditableBoneVisibilityStates();
	if (Visibility.Num() != NumBones)
	{
		UE_LOG(LogTemp, Verbose, TEXT("QuarryVehicleMesh: bone visibility array not ready (%d/%d)"), Visibility.Num(), NumBones);
		return;
	}
	Visibility.Init(1, NumBones); // 1 = BVS_Visible

	// Hide wheel bones and all their descendants.
	auto bIsDescendantOfWheelBone = [&RefSkeleton, &WheelBoneIndices](int32 BoneIndex)
	{
		for (int32 Parent = RefSkeleton.GetParentIndex(BoneIndex);
			Parent != INDEX_NONE; Parent = RefSkeleton.GetParentIndex(Parent))
		{
			if (WheelBoneIndices.Contains(Parent))
			{
				return true;
			}
		}
		return false;
	};

	int32 HiddenCount = 0;
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		if (WheelBoneIndices.Contains(BoneIndex) || bIsDescendantOfWheelBone(BoneIndex))
		{
			Visibility[BoneIndex] = 0; // 0 = BVS_Hidden
			++HiddenCount;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("QuarryVehicleMesh: hid %d baked wheel bone(s)"), HiddenCount);
}
