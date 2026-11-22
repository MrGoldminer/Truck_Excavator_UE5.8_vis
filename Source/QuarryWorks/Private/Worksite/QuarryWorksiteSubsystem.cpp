#include "Worksite/QuarryWorksiteSubsystem.h"

#include "Components/BoxComponent.h"
#include "Materials/QuarryMaterialPile.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/PlayerController.h"
#include "Protocol/QuarryJsonCodec.h"
#include "Protocol/QuarryMqttClient.h"
#include "Protocol/QuarryProtocol.h"
#include "Protocol/QuarryTcpServer.h"
#include "Vehicles/QuarryExcavator.h"
#include "Vehicles/QuarryMiningTruck.h"
#include "Vehicles/QuarryVehiclePawn.h"

UQuarryWorksiteSubsystem::UQuarryWorksiteSubsystem() = default;

UQuarryWorksiteSubsystem::~UQuarryWorksiteSubsystem()
{
	if (MqttClient)
	{
		MqttClient->RequestStop();
		delete MqttClient;
		MqttClient = nullptr;
	}
	if (TcpServer)
	{
		TcpServer->RequestStop();
		delete TcpServer;
		TcpServer = nullptr;
	}
}

void UQuarryWorksiteSubsystem::RegisterExcavator(AQuarryExcavator* InExcavator)
{
	Excavator = InExcavator;
	if (InExcavator)
	{
		ExcavatorSpawnPose = InExcavator->GetActorTransform();
	}
}

void UQuarryWorksiteSubsystem::RegisterMiningTruck(AQuarryMiningTruck* Truck)
{
	MiningTruck = Truck;
	if (Truck)
	{
		TruckSpawnPose = Truck->GetActorTransform();
	}
}

void UQuarryWorksiteSubsystem::TickOutOfBoundRespawn()
{
	// Fall protection: the site ground plane is finite, and a management-plane
	// command can drive a vehicle past its edge. Reset to the registration
	// pose instead of letting it free-fall forever.
	if (RespawnCooldownS > 0.0f)
	{
		RespawnCooldownS -= GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
		return;
	}

	auto RespawnIfNeeded = [this](AQuarryVehiclePawn* Vehicle, const FTransform& SpawnPose, const TCHAR* Label) -> bool
	{
		if (Vehicle && Vehicle->GetActorLocation().Z < RespawnZCm)
		{
			Vehicle->SetActorTransform(SpawnPose, false, nullptr, ETeleportType::TeleportPhysics);
			if (Vehicle->Chassis)
			{
				Vehicle->Chassis->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Vehicle->Chassis->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			}
			Vehicle->SetParkingBrake(true);
			RespawnCooldownS = 2.0f;
			UE_LOG(LogTemp, Warning, TEXT("QuarryWorks: %s left the world; respawned at the registration pose"), Label);
			Notify(FString::Printf(TEXT("%s RESPAWNED (out of bounds)"), Label), 4.0f);
			return true;
		}
		return false;
	};

	RespawnIfNeeded(GetMiningTruck(), TruckSpawnPose, TEXT("MINING TRUCK"));
	RespawnIfNeeded(GetExcavator(), ExcavatorSpawnPose, TEXT("EXCAVATOR"));
}

void UQuarryWorksiteSubsystem::RegisterDigSite(AQuarryDigSite* InDigSite)
{
	DigSite = InDigSite;
}

void UQuarryWorksiteSubsystem::RegisterDeliveryPile(AQuarryMaterialPile* Pile)
{
	if (DeliveryPile)
	{
		DeliveryPile->OnPileChanged.RemoveDynamic(this, &UQuarryWorksiteSubsystem::HandlePileChanged);
	}
	DeliveryPile = Pile;
	if (DeliveryPile)
	{
		DeliveryPile->OnPileChanged.AddDynamic(this, &UQuarryWorksiteSubsystem::HandlePileChanged);
	}
}

void UQuarryWorksiteSubsystem::Tick(float DeltaTime)
{
	SimulationTimeSeconds += DeltaTime;

	// Fall protection runs before any task/controller logic.
	TickOutOfBoundRespawn();

	// Apply externally queued protobuf commands at the tick boundary.
	TArray<uint8> CommandBytes;
	while (PendingCommands.Dequeue(CommandBytes))
	{
		ApplyVehicleCommand(CommandBytes);
	}

	// MQTT management plane: route inbound control/task payloads, drive tasks,
	// and stream CARLA-named JSON telemetry at the configured rate.
	if (MqttClient && GetWorld() && GetWorld()->IsGameWorld())
	{
		FQuarryMqttClient::FMqttMessage Message;
		while (MqttClient->PopInbound(Message))
		{
			// UTF8_TO_TCHAR expects a null-terminated string; the raw payload
			// array has no terminator, so append one before converting.
			TArray<uint8> PayloadBytes = Message.Payload;
			PayloadBytes.Add(0);
			const FString Payload = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(PayloadBytes.GetData()));
			UE_LOG(LogTemp, Log, TEXT("QuarryWorks MQTT inbound: topic=%s payload=%.80s"), *Message.Topic, *Payload);
			if (Message.Topic.EndsWith(TEXT("/command")))
			{
				ApplyCarlaControlJson(Payload);
			}
			else if (Message.Topic.EndsWith(TEXT("/task")))
			{
				// Management-plane convenience: {"type":"screenshot"} captures the
				// game view (motion verification without a debugger attached).
				if (Payload.Contains(TEXT("\"screenshot\"")))
				{
					if (GEngine && GetWorld())
					{
						if (APlayerController* PC = GEngine->GetFirstLocalPlayerController(GetWorld()))
						{
							PC->ConsoleCommand(TEXT("HighResShot 1280x720"), true);
							UE_LOG(LogTemp, Log, TEXT("QuarryWorks: screenshot captured via MQTT"));
						}
					}
					continue;
				}
				FQuarryTaskCommand Task;
				if (FQuarryJsonCodec::ParseTask(Payload, Task))
				{
					const bool bQueued = TaskExecutor.EnqueueTask(Task);
					UE_LOG(LogTemp, Log, TEXT("QuarryWorks MQTT task '%s' parsed (type=%d) queued=%s"),
						*Task.TaskId, static_cast<int32>(Task.Type), bQueued ? TEXT("yes") : TEXT("NO"));
					if (bQueued)
					{
						Notify(FString::Printf(TEXT("TASK QUEUED: %s"), *Task.TaskId), 3.0f);
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("QuarryWorks MQTT task parse FAILED: %.80s"), *Payload);
				}
			}
		}

		// Publish the retained online status once per connection.
		if (MqttClient->IsConnected())
		{
			if (!bMqttOnlineStatusSent)
			{
				bMqttOnlineStatusSent = true;
				PublishMqtt(TEXT("/status"), FQuarryJsonCodec::EncodeStatus(true), true);
				Notify(TEXT("MQTT MANAGEMENT PLANE ONLINE"), 4.0f);
			}
			MqttTelemetryAccumulator += DeltaTime;
			if (MqttTelemetryAccumulator >= MqttTelemetryPeriodS)
			{
				MqttTelemetryAccumulator = 0.0f;
				PublishMqtt(TEXT("/telemetry"), FQuarryJsonCodec::EncodeTelemetry(this, GeoOriginLat, GeoOriginLon));
			}
		}
		else
		{
			bMqttOnlineStatusSent = false;
		}
	}

	// Drive the management-plane task executor (game thread).
	TaskExecutor.Tick(DeltaTime);

	if (bSynchronousMode)
	{
		// Emit telemetry snapshots at a fixed cadence, decoupled from the render rate.
		SnapshotAccumulator += DeltaTime;
		while (SnapshotAccumulator >= FixedDeltaTime)
		{
			SnapshotAccumulator -= FixedDeltaTime;
			SnapshotQueue.Add(GetTelemetryBytes());
			if (SnapshotQueue.Num() > 8)
			{
				SnapshotQueue.RemoveAt(0, SnapshotQueue.Num() - 8, EAllowShrinking::No);
			}
		}
	}

	// Stream telemetry to the TCP client: every snapshot in sync mode, throttled to 30 Hz otherwise.
	if (TcpServer && TcpServer->IsRunning())
	{
		TArray<uint8> Snapshot;
		if (bSynchronousMode)
		{
			while (PopTelemetrySnapshot(Snapshot))
			{
				TcpServer->BroadcastTelemetry(Snapshot);
			}
		}
		else
		{
			TelemetryBroadcastAccumulator += DeltaTime;
			if (TelemetryBroadcastAccumulator >= 1.0f / 30.0f)
			{
				TelemetryBroadcastAccumulator = 0.0f;
				TcpServer->BroadcastTelemetry(GetTelemetryBytes());
			}
		}
	}
}

TStatId UQuarryWorksiteSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UQuarryWorksiteSubsystem, STATGROUP_Tickables);
}

bool UQuarryWorksiteSubsystem::QueueVehicleCommand(const TArray<uint8>& CommandBytes)
{
	if (CommandBytes.Num() == 0)
	{
		return false;
	}
	PendingCommands.Enqueue(CommandBytes);
	return true;
}

bool UQuarryWorksiteSubsystem::PopTelemetrySnapshot(TArray<uint8>& OutBytes)
{
	if (SnapshotQueue.Num() == 0)
	{
		return false;
	}
	OutBytes = SnapshotQueue[0];
	SnapshotQueue.RemoveAt(0, 1, EAllowShrinking::No);
	return true;
}

FString UQuarryWorksiteSubsystem::GetNotification() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() <= NotificationExpiresAt ? Notification : FString();
}

void UQuarryWorksiteSubsystem::Notify(const FString& Message, float Duration)
{
	Notification = Message;
	NotificationExpiresAt = GetWorld() ? GetWorld()->GetTimeSeconds() + Duration : 0.0f;
}

void UQuarryWorksiteSubsystem::HandlePileChanged(AQuarryMaterialPile* Pile, float DeltaMassKg, bool bDeposited)
{
	if (Pile == DeliveryPile && bDeposited && DeltaMassKg > 0.0f)
	{
		DeliveredMassKg += DeltaMassKg;
	}
}

TArray<uint8> UQuarryWorksiteSubsystem::GetTelemetryBytes() const
{
	return FQuarryProtocol::EncodeTelemetry(this);
}

bool UQuarryWorksiteSubsystem::ApplyVehicleCommand(const TArray<uint8>& CommandBytes)
{
	return FQuarryProtocol::ApplyVehicleCommand(CommandBytes, this);
}

FString UQuarryWorksiteSubsystem::GetTelemetryDebugString() const
{
	return FQuarryProtocol::DecodeTelemetryForDebug(GetTelemetryBytes());
}

void UQuarryWorksiteSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Load the management-plane configuration from [QuarryWorks] in DefaultGame.ini.
	FString Host;
	if (GConfig->GetString(TEXT("QuarryWorks"), TEXT("MqttHost"), Host, GGameIni) && !Host.IsEmpty())
	{
		MqttHost = Host;
	}
	int32 Port = 0;
	if (GConfig->GetInt(TEXT("QuarryWorks"), TEXT("MqttPort"), Port, GGameIni) && Port > 0)
	{
		MqttPort = static_cast<uint16>(FMath::Clamp(Port, 1, 65535));
	}
	FString Prefix;
	if (GConfig->GetString(TEXT("QuarryWorks"), TEXT("MqttPrefix"), Prefix, GGameIni) && !Prefix.IsEmpty())
	{
		MqttPrefix = Prefix;
	}
	double RateHz = 0.0;
	if (GConfig->GetDouble(TEXT("QuarryWorks"), TEXT("MqttTelemetryRateHz"), RateHz, GGameIni) && RateHz > 0.0)
	{
		MqttTelemetryPeriodS = static_cast<float>(1.0 / FMath::Clamp(RateHz, 1.0, 60.0));
	}
	GConfig->GetDouble(TEXT("QuarryWorks"), TEXT("GeoOriginLat"), GeoOriginLat, GGameIni);
	GConfig->GetDouble(TEXT("QuarryWorks"), TEXT("GeoOriginLon"), GeoOriginLon, GGameIni);
	GConfig->GetBool(TEXT("QuarryWorks"), TEXT("MqttAutoStart"), bMqttAutoStart, GGameIni);
	float RespawnZ = 0.0f;
	if (GConfig->GetFloat(TEXT("QuarryWorks"), TEXT("RespawnZCm"), RespawnZ, GGameIni) && RespawnZ < 0.0f)
	{
		RespawnZCm = RespawnZ;
	}

	TaskExecutor.Initialize(this);
	// Bootstrap worlds also run Initialize; only the real game world owns the
	// management-plane client, or a ghost world steals inbound routing.
	if (bMqttAutoStart && GetWorld() && GetWorld()->IsGameWorld())
	{
		StartMqtt();
	}
}

bool UQuarryWorksiteSubsystem::StartMqtt()
{
	if (MqttClient && MqttClient->IsConnected())
	{
		return true;
	}
	delete MqttClient;
	MqttClient = new FQuarryMqttClient();
	if (!MqttClient->Start(MqttHost, MqttPort, MqttPrefix, TWeakObjectPtr<UQuarryWorksiteSubsystem>(this)))
	{
		delete MqttClient;
		MqttClient = nullptr;
		UE_LOG(LogTemp, Error, TEXT("QuarryWorks MQTT client failed to start"));
		return false;
	}
	bMqttOnlineStatusSent = false;
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks MQTT client launched for %s:%u (prefix %s)"), *MqttHost, MqttPort, *MqttPrefix);
	return true;
}

void UQuarryWorksiteSubsystem::StopMqtt()
{
	if (MqttClient)
	{
		MqttClient->RequestStop();
		delete MqttClient;
		MqttClient = nullptr;
		bMqttOnlineStatusSent = false;
		Notify(TEXT("MQTT DISCONNECTED"), 3.0f);
	}
}

bool UQuarryWorksiteSubsystem::IsMqttConnected() const
{
	return MqttClient && MqttClient->IsConnected();
}

void UQuarryWorksiteSubsystem::PublishMqtt(const FString& TopicSuffix, const FString& Payload, bool bRetain)
{
	if (MqttClient && MqttClient->IsConnected())
	{
		MqttClient->Publish(MqttPrefix + TopicSuffix, Payload, bRetain);
	}
}

bool UQuarryWorksiteSubsystem::EnqueueTask(const FQuarryTaskCommand& Task)
{
	return TaskExecutor.EnqueueTask(Task);
}

void UQuarryWorksiteSubsystem::ApplyCarlaControlJson(const FString& JsonText)
{
	// CARLA VehicleControl semantics: throttle [0,1] forward, reverse selects
	// the reverse gear, brake [0,1] is the service brake, hand_brake parks.
	FQuarryJsonCodec::FCarlaControl Control;
	if (!FQuarryJsonCodec::ParseControl(JsonText, Control))
	{
		UE_LOG(LogTemp, Warning, TEXT("QuarryWorks MQTT control parse failed: %.80s"), *JsonText);
		return;
	}

	AQuarryVehiclePawn* Vehicle = nullptr;
	if (Control.VehicleId == TEXT("excavator")) Vehicle = GetExcavator();
	if (Control.VehicleId == TEXT("mining_truck")) Vehicle = GetMiningTruck();
	if (!Vehicle)
	{
		UE_LOG(LogTemp, Warning, TEXT("QuarryWorks MQTT control for unknown vehicle '%s'"), *Control.VehicleId);
		return;
	}

	const float SignedThrottle = Control.bReverse ? -Control.Throttle : Control.Throttle;
	Vehicle->SetDriveInput(SignedThrottle, Control.Steer);
	Vehicle->SetServiceBrake(Control.Brake);
	Vehicle->SetParkingBrake(Control.bHandBrake);
}

bool UQuarryWorksiteSubsystem::StartTcpServer(int32 Port)
{
	const uint16 SafePort = static_cast<uint16>(FMath::Clamp(Port, 1024, 65535));
	if (TcpServer && TcpServer->IsRunning())
	{
		return true;
	}
	delete TcpServer;
	TcpServer = new FQuarryTcpServer();
	if (!TcpServer->Start(SafePort, TWeakObjectPtr<UQuarryWorksiteSubsystem>(this)))
	{
		delete TcpServer;
		TcpServer = nullptr;
		return false;
	}
	Notify(FString::Printf(TEXT("TCP SERVER READY :%u (see Scripts/quarry_client.py)"), SafePort), 6.0f);
	return true;
}

void UQuarryWorksiteSubsystem::StopTcpServer()
{
	if (TcpServer)
	{
		TcpServer->RequestStop();
		delete TcpServer;
		TcpServer = nullptr;
		Notify(TEXT("TCP SERVER STOPPED"), 3.0f);
	}
}

bool UQuarryWorksiteSubsystem::IsTcpServerRunning() const
{
	return TcpServer && TcpServer->IsRunning();
}

void UQuarryWorksiteSubsystem::Deinitialize()
{
	StopMqtt();
	StopTcpServer();
	if (DeliveryPile)
	{
		DeliveryPile->OnPileChanged.RemoveDynamic(this, &UQuarryWorksiteSubsystem::HandlePileChanged);
	}
	Super::Deinitialize();
}

static FAutoConsoleCommand GQuarryServerStartCmd(
	TEXT("Quarry.Server.Start"),
	TEXT("Start the QuarryWorks TCP telemetry server: Quarry.Server.Start [port=17000]"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
		{
			if (UQuarryWorksiteSubsystem* Subsystem = World ? World->GetSubsystem<UQuarryWorksiteSubsystem>() : nullptr)
			{
				const int32 Port = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 17000;
				Subsystem->StartTcpServer(Port);
				Output.Logf(TEXT("QuarryWorks TCP server requested on port %d"), Port);
			}
		}));

static FAutoConsoleCommand GQuarryServerStopCmd(
	TEXT("Quarry.Server.Stop"),
	TEXT("Stop the QuarryWorks TCP telemetry server."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
		{
			if (UQuarryWorksiteSubsystem* Subsystem = World ? World->GetSubsystem<UQuarryWorksiteSubsystem>() : nullptr)
			{
				Subsystem->StopTcpServer();
				Output.Logf(TEXT("QuarryWorks TCP server stopped"));
			}
		}));

static FAutoConsoleCommand GQuarryMqttStartCmd(
	TEXT("Quarry.Mqtt.Start"),
	TEXT("Connect the QuarryWorks MQTT management-plane client (host/port from DefaultGame.ini)."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
		{
			if (UQuarryWorksiteSubsystem* Subsystem = World ? World->GetSubsystem<UQuarryWorksiteSubsystem>() : nullptr)
			{
				Output.Logf(TEXT("QuarryWorks MQTT start: %s"), Subsystem->StartMqtt() ? TEXT("ok") : TEXT("failed"));
			}
		}));

static FAutoConsoleCommand GQuarryMqttStopCmd(
	TEXT("Quarry.Mqtt.Stop"),
	TEXT("Disconnect the QuarryWorks MQTT management-plane client."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
		{
			if (UQuarryWorksiteSubsystem* Subsystem = World ? World->GetSubsystem<UQuarryWorksiteSubsystem>() : nullptr)
			{
				Subsystem->StopMqtt();
				Output.Logf(TEXT("QuarryWorks MQTT stopped"));
			}
		}));
