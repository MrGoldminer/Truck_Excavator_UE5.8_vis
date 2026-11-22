#include "Protocol/QuarryTcpServer.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Worksite/QuarryWorksiteSubsystem.h"

namespace
{
	constexpr uint32 MaxFrameBytes = 1 << 20; // 1 MiB sanity limit per payload

	uint32 ReadFrameLength(const TArray<uint8>& Buffer)
	{
		return static_cast<uint32>(Buffer[0])
			| (static_cast<uint32>(Buffer[1]) << 8)
			| (static_cast<uint32>(Buffer[2]) << 16)
			| (static_cast<uint32>(Buffer[3]) << 24);
	}

	void WriteFrameLength(uint8 (&OutHeader)[4], uint32 Length)
	{
		OutHeader[0] = static_cast<uint8>(Length & 0xFF);
		OutHeader[1] = static_cast<uint8>((Length >> 8) & 0xFF);
		OutHeader[2] = static_cast<uint8>((Length >> 16) & 0xFF);
		OutHeader[3] = static_cast<uint8>((Length >> 24) & 0xFF);
	}
}

FQuarryTcpServer::~FQuarryTcpServer()
{
	RequestStop();
}

bool FQuarryTcpServer::Start(uint16 InPort, const TWeakObjectPtr<UQuarryWorksiteSubsystem>& InWorksite)
{
	if (bRunning.load())
	{
		return true;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return false;
	}

	Listener = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("QuarryTcpListener"), false);
	if (!Listener)
	{
		return false;
	}
	Listener->SetReuseAddr();

	TSharedRef<FInternetAddr> BindAddress = SocketSubsystem->CreateInternetAddr();
	BindAddress->SetLoopbackAddress();
	BindAddress->SetPort(InPort);
	if (!Listener->Bind(*BindAddress) || !Listener->Listen(1))
	{
		SocketSubsystem->DestroySocket(Listener);
		Listener = nullptr;
		return false;
	}

	Port = InPort;
	Worksite = InWorksite;
	bStopping.store(false);
	Thread = FRunnableThread::Create(this, TEXT("QuarryTcpServer"), 0, TPri_Normal);
	if (!Thread)
	{
		Listener->Close();
		SocketSubsystem->DestroySocket(Listener);
		Listener = nullptr;
		return false;
	}
	bRunning.store(true);
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks TCP server listening on 127.0.0.1:%u"), Port);
	return true;
}

void FQuarryTcpServer::RequestStop()
{
	bStopping.store(true);
	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}

	CloseClientSocket();
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (Listener)
	{
		Listener->Close();
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(Listener);
		}
		Listener = nullptr;
	}
	bRunning.store(false);
}

void FQuarryTcpServer::CloseClientSocket()
{
	// Take ownership of the socket under the lock, then close it unlocked so
	// callers that already hold the mutex can route through here safely.
	FSocket* Closed = nullptr;
	{
		std::lock_guard<std::mutex> Lock(ClientMutex);
		Closed = ClientSocket;
		ClientSocket = nullptr;
	}
	if (Closed)
	{
		Closed->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(Closed);
		}
		RecvBuffer.Reset();
		UE_LOG(LogTemp, Log, TEXT("QuarryWorks TCP client disconnected"));
	}
}

uint32 FQuarryTcpServer::Run()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	while (!bStopping.load() && Listener && SocketSubsystem)
	{
		FSocket* Client = nullptr;
		{
			std::lock_guard<std::mutex> Lock(ClientMutex);
			Client = ClientSocket;
		}
		if (!Client)
		{
			bool bHasPending = false;
			if (Listener->WaitForPendingConnection(bHasPending, FTimespan::FromMilliseconds(100)) && bHasPending)
			{
				TSharedRef<FInternetAddr> PeerAddress = SocketSubsystem->CreateInternetAddr();
				FSocket* Accepted = Listener->Accept(*PeerAddress, TEXT("QuarryTcpClient"));
				if (Accepted)
				{
					{
						std::lock_guard<std::mutex> Lock(ClientMutex);
						ClientSocket = Accepted;
					}
					RecvBuffer.Reset();
					UE_LOG(LogTemp, Log, TEXT("QuarryWorks TCP client connected on port %u"), Port);
				}
			}
			continue;
		}
		HandleConnectedClient();
	}
	return 0;
}

void FQuarryTcpServer::HandleConnectedClient()
{
	FSocket* Client = nullptr;
	{
		std::lock_guard<std::mutex> Lock(ClientMutex);
		Client = ClientSocket;
	}
	if (!Client)
	{
		return;
	}

	if (!Client->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50)))
	{
		return; // no inbound data this poll; telemetry keeps flowing via BroadcastTelemetry
	}

	uint8 Buffer[4096];
	int32 BytesRead = 0;
	if (!Client->Recv(Buffer, sizeof(Buffer), BytesRead) || BytesRead <= 0)
	{
		CloseClientSocket();
		return;
	}

	RecvBuffer.Append(Buffer, BytesRead);
	ParseFrames();
}

void FQuarryTcpServer::ParseFrames()
{
	while (RecvBuffer.Num() >= 4)
	{
		const uint32 Length = ReadFrameLength(RecvBuffer);
		if (Length == 0 || Length > MaxFrameBytes)
		{
			RecvBuffer.Reset();
			return;
		}
		if (RecvBuffer.Num() < static_cast<int32>(4 + Length))
		{
			return; // wait for the rest of the frame
		}

		TArray<uint8> Payload;
		Payload.Append(RecvBuffer.GetData() + 4, Length);
		if (UQuarryWorksiteSubsystem* Subsystem = Worksite.Get())
		{
			Subsystem->QueueVehicleCommand(Payload);
		}
		RecvBuffer.RemoveAt(0, static_cast<int32>(4 + Length), EAllowShrinking::No);
	}
}

void FQuarryTcpServer::BroadcastTelemetry(const TArray<uint8>& Bytes)
{
	if (!bRunning.load() || Bytes.Num() == 0)
	{
		return;
	}

	// Extract the socket under the lock, send unlocked. On failure just bail:
	// only the worker thread closes/destroys the socket (see CloseClientSocket)
	// so a destroy can never race a send or recv still in flight.
	FSocket* Client = nullptr;
	{
		std::lock_guard<std::mutex> Lock(ClientMutex);
		Client = ClientSocket;
	}
	if (!Client)
	{
		return;
	}

	uint8 Header[4];
	WriteFrameLength(Header, Bytes.Num());
	int32 BytesSent = 0;
	const bool bHeaderOk = Client->Send(Header, sizeof(Header), BytesSent) && BytesSent == sizeof(Header);
	int32 BodySent = 0;
	const bool bBodyOk = bHeaderOk && Client->Send(reinterpret_cast<const uint8*>(Bytes.GetData()), Bytes.Num(), BodySent)
		&& BodySent == Bytes.Num();
	if (!bBodyOk)
	{
		UE_LOG(LogTemp, Verbose, TEXT("QuarryWorks TCP telemetry send failed; worker will reclaim the socket"));
	}
}
