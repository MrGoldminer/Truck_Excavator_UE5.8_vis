#include "Worksite/QuarryWorksiteGameMode.h"

#include "EngineUtils.h"
#include "Worksite/QuarryHUD.h"
#include "Worksite/QuarryPlayerController.h"
#include "Worksite/QuarryWorksite.h"
#include "Worksite/QuarryWorksiteSubsystem.h"
#include "Vehicles/QuarryExcavator.h"

AQuarryWorksiteGameMode::AQuarryWorksiteGameMode()
{
	DefaultPawnClass = AQuarryExcavator::StaticClass();
	PlayerControllerClass = AQuarryPlayerController::StaticClass();
	HUDClass = AQuarryHUD::StaticClass();
}

void AQuarryWorksiteGameMode::StartPlay()
{
	Super::StartPlay();

	AQuarryExcavator* PlayerExcavator = nullptr;
	for (TActorIterator<AQuarryExcavator> It(GetWorld()); It; ++It)
	{
		PlayerExcavator = *It;
		break;
	}

	AQuarryWorksite* Worksite = nullptr;
	for (TActorIterator<AQuarryWorksite> It(GetWorld()); It; ++It)
	{
		Worksite = *It;
		break;
	}
	if (!Worksite)
	{
		Worksite = GetWorld()->SpawnActor<AQuarryWorksite>();
	}
	Worksite->InitializeWorksite(PlayerExcavator);
}
