#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Protocol/QuarryProtobuf.h"
#include "QuarryConfigSubsystem.generated.h"

UCLASS()
class QUARRYWORKS_API UQuarryConfigSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const quarryworks::v1::VehicleConfig& GetExcavatorConfig() const { return Config.excavator(); }
	const quarryworks::v1::VehicleConfig& GetMiningTruckConfig() const { return Config.mining_truck(); }

private:
	quarryworks::v1::QuarryConfig Config;
};
