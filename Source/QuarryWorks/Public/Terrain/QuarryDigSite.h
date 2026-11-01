#pragma once

#include "CoreMinimal.h"
#include "Materials/QuarryMaterialPile.h"
#include "QuarryDigSite.generated.h"

class UProceduralMeshComponent;

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryDigSite : public AQuarryMaterialPile
{
	GENERATED_BODY()

public:
	AQuarryDigSite();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Terrain")
	TObjectPtr<UProceduralMeshComponent> TerrainMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain", meta=(ClampMin="8", UIMax="64"))
	int32 GridResolution = 32;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain", meta=(ClampMin="25.0"))
	float CellSize = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain")
	float MoundHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Terrain")
	float EditRadius = 150.0f;

	virtual float ExtractAt(const FVector& WorldLocation, float RequestedMassKg) override;
	virtual float DepositAt(const FVector& WorldLocation, EQuarryMaterialType InMaterialType, float OfferedMassKg) override;
	virtual float GetDistanceToWorkSurface(const FVector& WorldLocation) const override;
	virtual void UpdateVisual() override;

	UFUNCTION(BlueprintPure, Category="Terrain")
	float GetHeightAtWorld(const FVector& WorldLocation) const;

private:
	void BuildMesh();
	void ApplyHeightDelta(const FVector& WorldLocation, float VolumeM3);
	void RefreshMesh();

	TArray<float> Heights;
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<int32> Triangles;
	TArray<FVector2D> UV0;
	bool bMeshDirty = false;
	float MeshUpdateCooldown = 0.0f;
};
