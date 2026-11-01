#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "QuarryBlockoutComponent.generated.h"

UCLASS(ClassGroup=(Quarry))
class QUARRYWORKS_API UQuarryBlockoutComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blockout")
	FLinearColor Color = FLinearColor::White;
};
