#pragma once

#include "CoreMinimal.h"

class AActor;
class UQuarryBlockoutComponent;
class USceneComponent;

class QUARRYWORKS_API FQuarryBlockoutVisuals
{
public:
	static UQuarryBlockoutComponent* AddCube(
		AActor* Owner, FName Name, USceneComponent* Parent, const FVector& Size,
		const FVector& Location, const FLinearColor& Color, bool bRuntime = false, const FRotator& Rotation = FRotator::ZeroRotator);

	static UQuarryBlockoutComponent* AddCylinder(
		AActor* Owner, FName Name, USceneComponent* Parent, float Radius, float Length,
		const FVector& Location, const FRotator& Rotation, const FLinearColor& Color, bool bRuntime = false);

	static UQuarryBlockoutComponent* AddSphere(
		AActor* Owner, FName Name, USceneComponent* Parent, const FVector& Scale,
		const FVector& Location, const FLinearColor& Color, bool bRuntime = false);
};
