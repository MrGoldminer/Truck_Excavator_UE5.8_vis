#pragma once

#include "CoreMinimal.h"
#include "QuarryMaterialTypes.generated.h"

UENUM(BlueprintType)
enum class EQuarryMaterialType : uint8
{
	None,
	Sand,
	Gravel,
	Ore
};
