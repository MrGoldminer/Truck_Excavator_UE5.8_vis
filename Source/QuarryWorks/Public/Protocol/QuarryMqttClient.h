#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

#include <atomic>
#include <mutex>

class FRunnableThread;
class FSocket;
class UQuarryWorksiteSubsystem;

/**
 * Minimal MQTT 3.1.1 client over UE sockets (no third-party dependencies).
 * Owns its own worker thread: connects to the local management broker, keeps
 * the session alive with PINGREQ, publishes queued telemetry (QoS 0) and
 * delivers inbound command/task PUBLISH payloads to the game thread through a
 * MPSC queue. Reconnects with exponential backoff and survives broker loss.
 */
class QUARRYWORKS_API FQuarryMqttClient : public FRunnable
{
public:
	struct FMqttMessage
	{
		FString Topic;
		TArray<uint8> Payload;
		bool bRetain = false;
	};

	FQuarryMqttClient() = default;
	virtual ~FQuarryMqttClient();

	bool Start(const FString& InHost, uint16 InPort, const FString& InPrefix, const TWeakObjectPtr<UQuarryWorksiteSubsystem>& InWorksite);
	void RequestStop();

	bool IsConnected() const { return bConnected.load(); }

	/** Queues one message for publication (game thread; bounded, drops oldest). */
	void Publish(const FString& Topic, const FString& Payload, bool bRetain);

	/** Pops one inbound message (game thread). */
	bool PopInbound(FMqttMessage& OutMessage);

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { bStopping.store(true); }

private:
	enum class EState : uint8 { Idle, Connecting, Connected };

	bool ConnectOnce();
	void HandleConnected();
	void DisconnectAndBackoff();
	void PumpOutbound();
	void PumpIncoming();
	bool TryParsePacket();
	void EnsureKeepalive();

	void SendConnect();
	void SendSubscribe(const FString& TopicFilter, uint8 Qos);
	void SendPublish(const FString& Topic, const TArray<uint8>& Payload, bool bRetain);
	void SendPubAck(uint16 PacketId);
	void SendPingReq();
	void SendDisconnect();

	bool AppendPacket(const TArray<uint8>& VariableHeaderAndPayload, uint8 FixedHeaderByte);
	static void AppendMqttString(TArray<uint8>& Out, const TArray<uint8>& Utf8);
	static void AppendRemainingLength(TArray<uint8>& Out, uint32 Length);

	void LogState(const FString& Line) const;

	std::atomic<bool> bStopping{ false };
	std::atomic<bool> bConnected{ false };
	EState State = EState::Idle;

	// Outbound queue: game thread pushes, worker drains (bounded, drops oldest).
	std::mutex OutboundMutex;
	TArray<FMqttMessage> OutboundQueue;
	static constexpr int32 MaxOutbound = 128;

	// Inbound queue: worker pushes, game thread pops.
	TQueue<FMqttMessage, EQueueMode::Mpsc> InboundQueue;

	FString Host;
	uint16 Port = 1883;
	FString Prefix;
	FString ClientId;
	TWeakObjectPtr<UQuarryWorksiteSubsystem> Worksite;

	FSocket* Socket = nullptr;
	FRunnableThread* Thread = nullptr;
	TArray<uint8> RxBuffer;

	double LastSentTime = 0.0;
	double LastRecvTime = 0.0;
	bool bPingInFlight = false;
	float BackoffSeconds = 2.0f;
	uint16 NextPacketId = 1;
	static constexpr float KeepaliveSeconds = 30.0f;
};
