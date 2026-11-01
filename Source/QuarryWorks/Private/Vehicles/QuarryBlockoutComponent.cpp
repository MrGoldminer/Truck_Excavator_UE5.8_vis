#include "Vehicles/QuarryBlockoutComponent.h"

#include "Materials/MaterialInstanceDynamic.h"

void UQuarryBlockoutComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInstanceDynamic* Material = CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
}
