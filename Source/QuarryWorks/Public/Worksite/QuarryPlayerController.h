#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "QuarryPlayerController.generated.h"

class AQuarryVehiclePawn;

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AQuarryPlayerController();
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintCallable, Category="Worksite")
	void SwitchVehicle();

private:
	void SelectExcavator();
	void SelectMiningTruck();
	void PossessVehicle(AQuarryVehiclePawn* Target);
};
