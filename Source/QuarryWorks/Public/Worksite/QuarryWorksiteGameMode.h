#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "QuarryWorksiteGameMode.generated.h"

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryWorksiteGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AQuarryWorksiteGameMode();
	virtual void StartPlay() override;
};
