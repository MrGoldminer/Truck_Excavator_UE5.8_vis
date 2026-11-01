#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "QuarryHUD.generated.h"

UCLASS(Blueprintable)
class QUARRYWORKS_API AQuarryHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawPanelLine(const FString& Text, float Y, const FLinearColor& Color = FLinearColor::White);
	void DrawWorldMarker(const FString& Label, const FVector& Location, const FLinearColor& Color);
};
