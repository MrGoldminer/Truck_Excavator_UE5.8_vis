#include "Terrain/QuarryDigSite.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "QuarryCollisionChannels.h"
#include "Vehicles/QuarryBlockoutComponent.h"

// ============================================================================
// AQuarryDigSite —— 可变形沙堆料场
// 实现：ProceduralMeshComponent 高度场网格（33×33 顶点）
// 挖掘：ExtractAt 扣料 → ApplyHeightDelta 让地面同步下陷
// 堆料：DepositAt 反向隆起（卸料区堆积可见）
// ============================================================================
AQuarryDigSite::AQuarryDigSite()
{
	PrimaryActorTick.bCanEverTick = true;
	RemainingMassKg = 500000.0f;
	if (PileVisual)
	{
		PileVisual->SetVisibility(false);
	}

	TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	TerrainMesh->SetupAttachment(GetRootComponent());
	TerrainMesh->bUseAsyncCooking = true;   // 网格重建异步化（挖掘不掉帧）
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		TerrainMesh->SetCollisionProfileName(TEXT("BlockAll"));  // 可碰撞：车轮 raycast 目标
		TerrainMesh->SetCollisionObjectType(QuarryCollision::Terrain);
	}
	TerrainMesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_QuarryBlockout.M_QuarryBlockout")));
}

void AQuarryDigSite::BeginPlay()
{
	Super::BeginPlay();
	BuildMesh();
}

void AQuarryDigSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	MeshUpdateCooldown -= DeltaSeconds;
	if (bMeshDirty && MeshUpdateCooldown <= 0.0f)
	{
		RefreshMesh();
		MeshUpdateCooldown = 0.12f;
		bMeshDirty = false;
	}
}

float AQuarryDigSite::ExtractAt(const FVector& WorldLocation, float RequestedMassKg)
{
	const float ExtractedMassKg = Super::ExtractAt(WorldLocation, RequestedMassKg);
	if (ExtractedMassKg > 0.0f)
	{
		ApplyHeightDelta(WorldLocation, -ExtractedMassKg / BulkDensityKgPerM3);
	}
	return ExtractedMassKg;
}

float AQuarryDigSite::DepositAt(const FVector& WorldLocation, EQuarryMaterialType InMaterialType, float OfferedMassKg)
{
	const float AcceptedMassKg = Super::DepositAt(WorldLocation, InMaterialType, OfferedMassKg);
	if (AcceptedMassKg > 0.0f)
	{
		ApplyHeightDelta(WorldLocation, AcceptedMassKg / BulkDensityKgPerM3);
	}
	return AcceptedMassKg;
}

void AQuarryDigSite::UpdateVisual()
{
	if (PileVisual)
	{
		PileVisual->SetVisibility(false);
	}
}

float AQuarryDigSite::GetDistanceToWorkSurface(const FVector& WorldLocation) const
{
	const float Extent = GridResolution * CellSize * 0.5f;
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	if (FMath::Abs(Local.X) > Extent || FMath::Abs(Local.Y) > Extent)
	{
		return TNumericLimits<float>::Max();
	}
	return FMath::Abs(Local.Z - GetHeightAtWorld(WorldLocation));
}

float AQuarryDigSite::GetHeightAtWorld(const FVector& WorldLocation) const
{
	if (Heights.IsEmpty())
	{
		return GetActorLocation().Z;
	}

	const int32 Stride = GridResolution + 1;
	const float Extent = GridResolution * CellSize * 0.5f;
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float GridX = FMath::Clamp((Local.X + Extent) / CellSize, 0.0f, GridResolution - 0.001f);
	const float GridY = FMath::Clamp((Local.Y + Extent) / CellSize, 0.0f, GridResolution - 0.001f);
	const int32 X = FMath::FloorToInt(GridX);
	const int32 Y = FMath::FloorToInt(GridY);
	const float AlphaX = GridX - X;
	const float AlphaY = GridY - Y;
	const float H00 = Heights[Y * Stride + X];
	const float H10 = Heights[Y * Stride + X + 1];
	const float H01 = Heights[(Y + 1) * Stride + X];
	const float H11 = Heights[(Y + 1) * Stride + X + 1];
	return FMath::Lerp(FMath::Lerp(H00, H10, AlphaX), FMath::Lerp(H01, H11, AlphaX), AlphaY) + GetActorLocation().Z;
}

void AQuarryDigSite::BuildMesh()
{
	const int32 Stride = GridResolution + 1;
	const float Extent = GridResolution * CellSize * 0.5f;
	const int32 VertexCount = Stride * Stride;
	Heights.SetNum(VertexCount);
	Vertices.SetNum(VertexCount);
	Normals.SetNum(VertexCount);
	UV0.SetNum(VertexCount);
	Triangles.Reserve(GridResolution * GridResolution * 6);

	for (int32 Y = 0; Y < Stride; ++Y)
	{
		for (int32 X = 0; X < Stride; ++X)
		{
			const int32 Index = Y * Stride + X;
			const float LocalX = X * CellSize - Extent;
			const float LocalY = Y * CellSize - Extent;
			const float NormalizedRadius = FVector2D(LocalX, LocalY).Size() / Extent;
			// 初始 mound 高度场：中心 MoundHeight，向外二次衰减到 0
		Heights[Index] = MoundHeight * FMath::Max(0.0f, 1.0f - NormalizedRadius * NormalizedRadius);
			Vertices[Index] = FVector(LocalX, LocalY, Heights[Index]);
			Normals[Index] = FVector::UpVector;
			UV0[Index] = FVector2D(static_cast<float>(X) / GridResolution, static_cast<float>(Y) / GridResolution);
			if (X < GridResolution && Y < GridResolution)
			{
				Triangles.Add(Index);
				Triangles.Add(Index + 1);
				Triangles.Add(Index + Stride);
				Triangles.Add(Index + 1);
				Triangles.Add(Index + Stride + 1);
				Triangles.Add(Index + Stride);
			}
		}
	}

	TerrainMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0,
		TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);
	if (UMaterialInstanceDynamic* Material = TerrainMesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.38f, 0.16f));
	}
}

void AQuarryDigSite::ApplyHeightDelta(const FVector& WorldLocation, float VolumeM3)
{
	const int32 Stride = GridResolution + 1;
	const float Extent = GridResolution * CellSize * 0.5f;
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	float WeightSum = 0.0f;
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		const float DistanceSquared = FVector2D(Vertices[Index].X - Local.X, Vertices[Index].Y - Local.Y).SizeSquared();
		if (DistanceSquared < FMath::Square(EditRadius))
		{
			WeightSum += 1.0f - DistanceSquared / FMath::Square(EditRadius);
		}
	}
	if (WeightSum <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 高度变化量 = 挖掘体积(cm³) ÷ 权重和×格子面积——体积守恒的地形形变
	const float HeightDelta = VolumeM3 * 1000000.0f / (WeightSum * CellSize * CellSize);
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		const float DistanceSquared = FVector2D(Vertices[Index].X - Local.X, Vertices[Index].Y - Local.Y).SizeSquared();
		if (DistanceSquared < FMath::Square(EditRadius))
		{
			const float Weight = 1.0f - DistanceSquared / FMath::Square(EditRadius);
			Heights[Index] = FMath::Clamp(Heights[Index] + HeightDelta * Weight, -300.0f, 500.0f);
			Vertices[Index].Z = Heights[Index];
		}
	}
	bMeshDirty = true;
}

void AQuarryDigSite::RefreshMesh()
{
	const int32 Stride = GridResolution + 1;
	for (int32 Y = 0; Y < Stride; ++Y)
	{
		for (int32 X = 0; X < Stride; ++X)
		{
			const int32 Index = Y * Stride + X;
			const int32 Left = Y * Stride + FMath::Max(0, X - 1);
			const int32 Right = Y * Stride + FMath::Min(GridResolution, X + 1);
			const int32 Down = FMath::Max(0, Y - 1) * Stride + X;
			const int32 Up = FMath::Min(GridResolution, Y + 1) * Stride + X;
			Normals[Index] = FVector(
				(Heights[Left] - Heights[Right]) / (2.0f * CellSize),
				(Heights[Down] - Heights[Up]) / (2.0f * CellSize), 1.0f).GetSafeNormal();
		}
	}
	TerrainMesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, UV0,
		TArray<FLinearColor>(), TArray<FProcMeshTangent>());
}
