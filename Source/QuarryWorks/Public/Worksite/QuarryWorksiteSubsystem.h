#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "QuarryWorksiteSubsystem.generated.h"

class AQuarryDigSite;
class AQuarryExcavator;
class AQuarryMaterialPile;
class AQuarryMiningTruck;

UCLASS()
class QUARRYWORKS_API UQuarryWorksiteSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	void RegisterExcavator(AQuarryExcavator* Excavator);
	void RegisterMiningTruck(AQuarryMiningTruck* Truck);
	void RegisterDigSite(AQuarryDigSite* DigSite);
	void RegisterDeliveryPile(AQuarryMaterialPile* Pile);

	UFUNCTION(BlueprintPure, Category="Worksite")
	AQuarryExcavator* GetExcavator() const { return Excavator; }

	UFUNCTION(BlueprintPure, Category="Worksite")
	AQuarryMiningTruck* GetMiningTruck() const { return MiningTruck; }

	UFUNCTION(BlueprintPure, Category="Worksite")
	AQuarryDigSite* GetDigSite() const { return DigSite; }

	UFUNCTION(BlueprintPure, Category="Worksite")
	AQuarryMaterialPile* GetDeliveryPile() const { return DeliveryPile; }

	UFUNCTION(BlueprintPure, Category="Worksite")
	float GetDeliveredMassKg() const { return DeliveredMassKg; }

	UFUNCTION(BlueprintPure, Category="Worksite")
	FString GetNotification() const;

	void Notify(const FString& Message, float Duration = 3.0f);

	UFUNCTION(BlueprintPure, Category="Protocol|Telemetry")
	TArray<uint8> GetTelemetryBytes() const;

	UFUNCTION(BlueprintCallable, Category="Protocol|Commands")
	bool ApplyVehicleCommand(const TArray<uint8>& CommandBytes);

	UFUNCTION(BlueprintPure, Category="Protocol|Telemetry")
	FString GetTelemetryDebugString() const;

private:
	UFUNCTION()
	void HandlePileChanged(AQuarryMaterialPile* Pile, float DeltaMassKg, bool bDeposited);

	UPROPERTY(Transient)
	TObjectPtr<AQuarryExcavator> Excavator = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMiningTruck> MiningTruck = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryDigSite> DigSite = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMaterialPile> DeliveryPile = nullptr;

	float DeliveredMassKg = 0.0f;
	FString Notification;
	float NotificationExpiresAt = 0.0f;
};
