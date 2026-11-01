#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "QuarryTireComponent.generated.h"

UCLASS(ClassGroup=(Quarry))
class QUARRYWORKS_API UQuarryTireComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire")
	float Radius = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire")
	float Width = 48.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tire")
	FLinearColor Color = FLinearColor(0.025f, 0.03f, 0.035f);

private:
	void BuildMesh();
};
