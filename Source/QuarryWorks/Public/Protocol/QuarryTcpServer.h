#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

#include <atomic>
#include <mutex>

class FInternetAddr;
class FRunnableThread;
class FSocket;
class UQuarryWorksiteSubsystem;

/**
 * CARLA-style TCP control bridge. The UE process acts as the server: clients
 * send length-prefixed VehicleCommand frames, the server queues them onto the
 * worksite subsystem (applied on the game thread at tick boundaries) and the
 * game thread broadcasts QuarryTelemetry frames back every tick.
 *
 * Wire format: [uint32 little-endian payload length][protobuf payload].
 */
class FQuarryTcpServer : public FRunnable
{
public:
	FQuarryTcpServer() = default;
	virtual ~FQuarryTcpServer();

	bool Start(uint16 InPort, const TWeakObjectPtr<UQuarryWorksiteSubsystem>& InWorksite);
	void RequestStop();

	bool IsRunning() const { return bRunning.load(); }

	/** Sends one framed payload to the connected client (any thread). */
	void BroadcastTelemetry(const TArray<uint8>& Bytes);

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { bStopping.store(true); }

private:
	void HandleConnectedClient();
	void ParseFrames();
	void CloseClientSocket();

	std::atomic<bool> bRunning{ false };
	std::atomic<bool> bStopping{ false };
	// Guards the client socket between the worker (recv/disconnect) and the
	// game thread (telemetry send) so a disconnect never races a send.
	std::mutex ClientMutex;
	FSocket* ClientSocket = nullptr;
	TWeakObjectPtr<UQuarryWorksiteSubsystem> Worksite;
	FSocket* Listener = nullptr;
	FRunnableThread* Thread = nullptr;
	uint16 Port = 17000;

	TArray<uint8> RecvBuffer;
};
