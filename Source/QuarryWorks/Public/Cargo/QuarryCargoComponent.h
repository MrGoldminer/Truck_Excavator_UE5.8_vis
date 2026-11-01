#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Cargo/QuarryMaterialTypes.h"
#include "QuarryCargoComponent.generated.h"

class UQuarryCargoComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FQuarryCargoChanged, EQuarryMaterialType, MaterialType, float, CurrentMassKg, float, CapacityKg);

UCLASS(ClassGroup=(Quarry), meta=(BlueprintSpawnableComponent))
class QUARRYWORKS_API UQuarryCargoComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UQuarryCargoComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo", meta=(ClampMin="0.0"))
	float CapacityKg = 5000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	EQuarryMaterialType MaterialType = EQuarryMaterialType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo")
	float CurrentMassKg = 0.0f;

	UPROPERTY(BlueprintAssignable, Category="Cargo")
	FQuarryCargoChanged OnCargoChanged;

	UFUNCTION(BlueprintPure, Category="Cargo")
	float GetFreeCapacityKg() const;

	UFUNCTION(BlueprintPure, Category="Cargo")
	float GetFillRatio() const;

	UFUNCTION(BlueprintPure, Category="Cargo")
	bool CanAcceptMaterial(EQuarryMaterialType InMaterialType) const;

	UFUNCTION(BlueprintCallable, Category="Cargo")
	float Load(EQuarryMaterialType InMaterialType, float RequestedMassKg);

	UFUNCTION(BlueprintCallable, Category="Cargo")
	float Unload(float RequestedMassKg, EQuarryMaterialType& OutMaterialType);

	UFUNCTION(BlueprintCallable, Category="Cargo")
	float TransferTo(UQuarryCargoComponent* Destination, float RequestedMassKg);

private:
	void NotifyCargoChanged();
};
