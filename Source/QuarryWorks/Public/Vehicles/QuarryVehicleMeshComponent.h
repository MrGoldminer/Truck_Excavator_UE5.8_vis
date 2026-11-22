#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "QuarryVehicleMeshComponent.generated.h"

class UPrimitiveComponent;
class USkeletalMeshComponent;
class USceneComponent;
class UQuarryVehicleMovementComponent;

/**
 * Swaps the blockout car body for a real skeletal-mesh vehicle (engine-shipped
 * free content, e.g. the Chaos modular SportsCar). The mesh rides rigidly on
 * the chassis; its baked wheel bones are hidden and real wheel meshes are
 * attached at the bone positions, driven by the movement component's wheel
 * states (suspension drop, spin, steering). Wheel mounts are rebuilt from the
 * discovered wheel bone positions so the raycast suspension matches the real
 * mesh geometry.
 */
	/** One articulated part: parented to a pivot chain node with a rest-pose offset. */
	USTRUCT(BlueprintType)
struct FQuarryPartBinding
{
		GENERATED_BODY()

		/** Part asset name inside PartsFolder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString PartName;

		/** Parent: 0 = chassis root, 1 = boom pivot, 2 = stick pivot, 3 = bucket pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ParentPivot = 0;

		/** Rest-pose offset relative to the parent pivot (centers the part on its joint). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector RelativeLocation = FVector::ZeroVector;

		/** Rest-pose rotation relative to the parent pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FRotator RelativeRotation = FRotator::ZeroRotator;

		/** Spin this part with the wheel states (wheel parts). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bSpinWithWheels = false;
};


UCLASS(ClassGroup=(Quarry), meta=(BlueprintSpawnableComponent))
class QUARRYWORKS_API UQuarryVehicleMeshComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UQuarryVehicleMeshComponent();

	/** Object path of the skeletal mesh (path.objectname). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh")
	FString BodyMeshPath = TEXT("/ChaosModularVehicleExamples/Models/SportsCar/SKM_SportsCar.SKM_SportsCar");

	/** Yaw applied to the body mesh so its forward matches the pawn's +X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh")
	float BodyYawOffsetDeg = 0.0f;

	/** Vertical offset of the body mesh relative to the chassis root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh")
	float BodyZOffset = 0.0f;

	/** Longitudinal offset of the body mesh relative to the chassis root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh")
	float BodyOffsetX = 0.0f;

	/** Uniform scale for the wheel meshes attached at StaticBodyWheelMounts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	float StaticBodyWheelMeshScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh")
	float BodyScale = 1.0f;

	/** Real wheel mesh (path.objectname); falls back to blockout tires if missing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	FString WheelMeshPath = TEXT("/NetworkPredictionExtras/Art/Meshes/SM_Wheel_01.SM_Wheel_01");

	/** Base rotation applied to each wheel mesh. Measured: SM_Wheel_01 axle is already along Y, so no rotation is needed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	FRotator WheelBaseRotation = FRotator(0.0f, 0.0f, 0.0f);

	/** Tracked-vehicle track chain mesh (e.g. SM_ExcavatorTracks), attached under the chassis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Tracks")
	FString TracksMeshPath;

	/** Tracked-vehicle road wheel mesh (e.g. SM_ExcavatorWheels). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Tracks")
	FString WheelsMeshPath;

	/** Uniform scale for the track/wheel meshes (0 = auto-fit to TrackFitLength). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Tracks")
	float TracksScale = 0.0f;

	/** Auto-fit target length (bounds X) for the track mesh, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Tracks")
	float TrackFitLength = 450.0f;

	/** Attachment offset of the track/wheel meshes relative to the chassis root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Tracks")
	FVector TracksOffset = FVector(0.0f, 0.0f, -35.0f);

	/** Yaw applied to the track/wheel meshes so their length axis matches the chassis forward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Tracks")
	float TracksYawOffsetDeg = 0.0f;

	/** Pitch applied to the track/wheel meshes; pack assets are often authored lying flat (axle along Z). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Tracks")
	float TracksPitchOffsetDeg = 0.0f;

	/** Static body fallback: tried when BodyMeshPath has no skeletal mesh (e.g. SM_AmericanDumpTruck). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh")
	FString BodyStaticMeshPath;

	/** Auto-fit the static body so its bounds X equals this length, cm (0 = keep native scale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh")
	float BodyFitLength = 650.0f;

	/** Explicit wheel mounts for a static body with no wheel bones (empty = keep movement defaults). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	TArray<FVector> StaticBodyWheelMounts;

	/** Wheel radius used with StaticBodyWheelMounts, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	float StaticBodyWheelRadius = 55.0f;

	/** Attach the track/wheel meshes (tracked vehicles). */
	void AttachTracksMeshes(USceneComponent* AttachRoot);

	// --- Parts assembly (user-modeled split exports: one FBX split into per-part static meshes) ---

	/** Folder containing the split parts, e.g. /Game/MyExcavator/Static1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FString PartsFolder;

	/** Part asset names to attach (all placed at the same origin, reconstructing the machine). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	TArray<FString> PartNames;

	/** Part whose bounds approximate the whole machine, used for the auto-fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FString PartsFitProxy = TEXT("Body1");

	/** Auto-fit target length (longest horizontal extent of the proxy), cm (0 = native scale). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	float PartsFitLength = 700.0f;

	/** Common yaw for the whole assembly (model authoring axis alignment). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	float PartsYawOffsetDeg = 0.0f;

	/** Common pitch for the whole assembly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	float PartsPitchOffsetDeg = 0.0f;

	/** Vertical offset of the whole assembly relative to the chassis root. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	float PartsZOffset = 0.0f;

	/** Local spin axis of the wheel parts: 0 = X, 1 = Y, 2 = Z. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	int32 PartsSpinAxis = 1;

	/** Spin direction multiplier; measured forward rolling = negative rotation about the lateral axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	float PartsSpinSign = -1.0f;

	/** Parts that spin with the wheels (suffix match, e.g. WheelFL). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	TArray<FString> PartsSpinNames;

	/** Articulated bindings replacing PartsFolder+PartNames when set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	TArray<FQuarryPartBinding> PartBindings;

	/** Real pivot chain nodes (chassis-root space): boom / stick / bucket. Passed in by the owning pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FVector BoomJointLocal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FVector StickJointOffset = FVector::ZeroVector; // relative to the boom joint

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FVector BucketJointOffset = FVector::ZeroVector; // relative to the stick joint

	/** Parts that spin with the wheels (suffix match, e.g. WheelFL). */

	/** Body color override for parts without a wheel suffix (missing-material fallback). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FLinearColor PartsBodyColor = FLinearColor(0.78f, 0.40f, 0.035f);

	/** Wheel color override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FLinearColor PartsWheelColor = FLinearColor(0.025f, 0.03f, 0.035f);

	/** Attach the split parts assembly (user-modeled machines). */
	void AttachPartsAssembly(USceneComponent* AttachRoot);

	/** Dig-sample offset relative to the real bucket joint (model space, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Parts")
	FVector BucketTipLocal = FVector(80.0f, 0.0f, -35.0f);

	/** World-space position of the REAL bucket tip (forward kinematics of the joint chain). */
	FVector GetRealBucketTip() const;

	/** Drives the articulated parts from the pawn's arm angles (called every tick). */
	void DriveArticulatedParts(float BoomAngleDeg, float StickAngleDeg, float BucketAngleDeg);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Rebuild movement wheel mounts from the discovered wheel bone positions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	bool bRebuildWheelsFromBones = true;

	/** Tire radius used when rebuilding wheel mounts from bones, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels", meta=(ClampMin="10.0"))
	float RebuiltWheelRadius = 34.0f;

	/** Front wheels (largest +X) become steered when rebuilding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	bool bSteerFrontBones = true;

	/** Hide the body mesh's baked wheel bones (replaced by attached real wheels). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	bool bHideBakedWheelBones = true;

	/** Substring (case-insensitive) selecting wheel bones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle Mesh|Wheels")
	FString WheelBoneFilter = TEXT("wheel");

	/**
	 * Loads the mesh, attaches it, discovers wheel bones, optionally rebuilds
	 * the movement's wheel mounts from the bones with real wheel meshes, and
	 * hides the blockout parts the real mesh replaces.
	 */
	void Initialize(UQuarryVehicleMovementComponent* InMovement, USceneComponent* AttachRoot,
		const TArray<UPrimitiveComponent*>& PartsToHide);

	UFUNCTION(BlueprintPure, Category="Quarry|Vehicle Mesh")
	bool IsBodyMeshActive() const { return BodyMesh && bInitialized; }

private:
	void DiscoverAndBindWheels();
	FVector BonePositionToRootSpace(const FVector& BoneComponentPos) const;
	void HideBakedWheelBones(const TArray<int32>& WheelBoneIndices);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BodyMeshStatic;

	/** Per-wheel visual state for parts-assembly wheels (suspension + spin). */
	struct FWheelPartVisual
	{
		TObjectPtr<UStaticMeshComponent> Component;
		FRotator BaseRotation;
		FVector RestLocation;
		int32 WheelIndex = 0; // index into the movement wheel states
		float SpinRad = 0.0f;
	};
	TArray<FWheelPartVisual> WheelPartVisuals;

	/** Real joint chain (rotated by DriveArticulatedParts; parts inherit). */
	TWeakObjectPtr<USceneComponent> RealBoomJoint;
	TWeakObjectPtr<USceneComponent> RealStickJoint;
	TWeakObjectPtr<USceneComponent> RealBucketJoint;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> AttachRootComponent;

	TWeakObjectPtr<UQuarryVehicleMovementComponent> Movement;
	bool bInitialized = false;
};
