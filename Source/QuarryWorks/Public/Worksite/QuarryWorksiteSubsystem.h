#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Worksite/QuarryTaskExecutor.h"
#include "QuarryWorksiteSubsystem.generated.h"

class AQuarryDigSite;
class AQuarryExcavator;
class AQuarryMaterialPile;
class AQuarryMiningTruck;
class FQuarryMqttClient;
class FQuarryTcpServer;

UCLASS()
class QUARRYWORKS_API UQuarryWorksiteSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Constructor and destructor live in the cpp: TcpServer/MqttClient are
	// only forward-declared here, so inline specials would need full types.
	UQuarryWorksiteSubsystem();
	~UQuarryWorksiteSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterExcavator(AQuarryExcavator* Excavator);
	void RegisterMiningTruck(AQuarryMiningTruck* Truck);
	void RegisterDigSite(AQuarryDigSite* DigSite);
	void RegisterDeliveryPile(AQuarryMaterialPile* Pile);

private:
	/** Resets a vehicle to its registration pose when it leaves the world (fall protection). */
	void TickOutOfBoundRespawn();

public:

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

	// --- CARLA-style synchronous stepping ---

	/** Simulation clock in seconds, monotonically advancing every tick. */
	UFUNCTION(BlueprintPure, Category="Quarry|Sync")
	float GetSimulationTimeSeconds() const { return SimulationTimeSeconds; }

	/** Enables fixed-cadence telemetry snapshots; commands always apply at tick boundaries. */
	UFUNCTION(BlueprintCallable, Category="Quarry|Sync")
	void SetSynchronousMode(bool bEnabled) { bSynchronousMode = bEnabled; }

	UFUNCTION(BlueprintPure, Category="Quarry|Sync")
	bool IsSynchronousMode() const { return bSynchronousMode; }

	/** Fixed telemetry snapshot interval in seconds (sync mode). */
	UFUNCTION(BlueprintCallable, Category="Quarry|Sync")
	void SetFixedDeltaTime(float InFixedDeltaTime) { FixedDeltaTime = FMath::Max(0.005f, InFixedDeltaTime); }

	/**
	 * Thread-safe command entry point (used by the TCP server worker): the
	 * command is queued and applied on the game thread at the next tick.
	 */
	bool QueueVehicleCommand(const TArray<uint8>& CommandBytes);

	/** Pops the oldest fixed-rate telemetry snapshot, if any (game thread only). */
	bool PopTelemetrySnapshot(TArray<uint8>& OutBytes);

	// --- CARLA-style TCP bridge ---

	/** Starts the TCP telemetry/control server on 127.0.0.1 (single client). */
	UFUNCTION(BlueprintCallable, Category="Quarry|Server")
	bool StartTcpServer(int32 Port = 17000);

	UFUNCTION(BlueprintCallable, Category="Quarry|Server")
	void StopTcpServer();

	UFUNCTION(BlueprintPure, Category="Quarry|Server")
	bool IsTcpServerRunning() const;

	// --- MQTT management plane ---

	/** Connects the MQTT client to the configured local broker. */
	UFUNCTION(BlueprintCallable, Category="Quarry|Server")
	bool StartMqtt();

	UFUNCTION(BlueprintCallable, Category="Quarry|Server")
	void StopMqtt();

	UFUNCTION(BlueprintPure, Category="Quarry|Server")
	bool IsMqttConnected() const;

	/** Publishes a UTF-8 JSON payload to <prefix>/<TopicSuffix> (QoS 0). */
	void PublishMqtt(const FString& TopicSuffix, const FString& Payload, bool bRetain = false);

	/** Enqueues a management-plane task (drive_to/dig/haul/test_suite). */
	bool EnqueueTask(const FQuarryTaskCommand& Task);

private:
	/** CARLA VehicleControl JSON adapter: maps throttle/steer/brake/hand_brake/reverse onto a pawn. */
	void ApplyCarlaControlJson(const FString& JsonText);

public:

private:
	UFUNCTION()
	void HandlePileChanged(AQuarryMaterialPile* Pile, float DeltaMassKg, bool bDeposited);

	UPROPERTY(Transient)
	TObjectPtr<AQuarryExcavator> Excavator = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMiningTruck> MiningTruck = nullptr;

	// Registration poses, kept for out-of-bounds respawn.
	FTransform ExcavatorSpawnPose;
	FTransform TruckSpawnPose;
	float RespawnZCm = -3000.0f;
	float RespawnCooldownS = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryDigSite> DigSite = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AQuarryMaterialPile> DeliveryPile = nullptr;

	float DeliveredMassKg = 0.0f;
	FString Notification;
	float NotificationExpiresAt = 0.0f;

	bool bSynchronousMode = false;
	float FixedDeltaTime = 1.0f / 60.0f;
	float SimulationTimeSeconds = 0.0f;
	float SnapshotAccumulator = 0.0f;

	// Commands cross from the TCP worker thread to the game thread.
	TQueue<TArray<uint8>, EQueueMode::Mpsc> PendingCommands;
	TArray<TArray<uint8>> SnapshotQueue;
	// Raw pointer managed in the cpp: UHT-generated vtable helpers instantiate
	// member constructors with only the forward declaration available, which
	// TUniquePtr does not support.
	FQuarryTcpServer* TcpServer = nullptr;
	FQuarryMqttClient* MqttClient = nullptr;
	FQuarryTaskExecutor TaskExecutor;
	float TelemetryBroadcastAccumulator = 0.0f;
	float MqttTelemetryAccumulator = 0.0f;
	float MqttTelemetryPeriodS = 1.0f / 20.0f;
	bool bMqttOnlineStatusSent = false;

	// Configuration loaded from [QuarryWorks] in DefaultGame.ini.
	FString MqttHost = TEXT("127.0.0.1");
	uint16 MqttPort = 1883;
	FString MqttPrefix = TEXT("quarryworks/v1");
	double GeoOriginLat = 31.2304;
	double GeoOriginLon = 121.4737;
	bool bMqttAutoStart = true;
};
