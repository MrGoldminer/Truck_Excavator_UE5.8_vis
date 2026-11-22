#include "Protocol/QuarryMqttClient.h"

#include "Interfaces/IPv4/IPv4Address.h"
#include "Misc/Guid.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Worksite/QuarryWorksiteSubsystem.h"

namespace
{
	// MQTT 3.1.1 control packet types (fixed header, high nibble).
	constexpr uint8 PacketConnect = 0x10;
	constexpr uint8 PacketConnack = 0x20;
	constexpr uint8 PacketPublish = 0x30;
	constexpr uint8 PacketPuback = 0x40;
	constexpr uint8 PacketSubscribe = 0x82; // flags 0010
	constexpr uint8 PacketSuback = 0x90;
	constexpr uint8 PacketPingreq = 0xC0;
	constexpr uint8 PacketPingresp = 0xD0;
	constexpr uint8 PacketDisconnect = 0xE0;

	void TextToUtf8(const FString& Text, TArray<uint8>& OutUtf8)
	{
		const FTCHARToUTF8 Converted(*Text);
		OutUtf8.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	}
}

FQuarryMqttClient::~FQuarryMqttClient()
{
	RequestStop();
}

bool FQuarryMqttClient::Start(const FString& InHost, uint16 InPort, const FString& InPrefix, const TWeakObjectPtr<UQuarryWorksiteSubsystem>& InWorksite)
{
	if (Thread)
	{
		return true;
	}
	Host = InHost;
	Port = InPort;
	Prefix = InPrefix;
	Worksite = InWorksite;
	ClientId = FString::Printf(TEXT("quarryworks-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
	RxBuffer.Reset();
	bStopping.store(false);
	BackoffSeconds = 2.0f;

	Thread = FRunnableThread::Create(this, TEXT("QuarryMqttClient"), 0, TPri_Normal);
	if (!Thread)
	{
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks MQTT client starting: %s:%u prefix=%s"), *Host, Port, *Prefix);
	return true;
}

void FQuarryMqttClient::RequestStop()
{
	bStopping.store(true);
	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}
	// The socket belongs to the worker thread; after the join above it is
		// already closed and destroyed (Run cleans up on every exit path).
	Socket = nullptr;
	bConnected.store(false);
}

void FQuarryMqttClient::Publish(const FString& Topic, const FString& Payload, bool bRetain)
{
	TArray<uint8> PayloadUtf8;
	TextToUtf8(Payload, PayloadUtf8);

	std::lock_guard<std::mutex> Lock(OutboundMutex);
	if (OutboundQueue.Num() >= MaxOutbound)
	{
		OutboundQueue.RemoveAt(0, OutboundQueue.Num() - MaxOutbound + 1, EAllowShrinking::No);
	}
	OutboundQueue.Add(FMqttMessage{ Topic, MoveTemp(PayloadUtf8), bRetain });
}

bool FQuarryMqttClient::PopInbound(FMqttMessage& OutMessage)
{
	return InboundQueue.Dequeue(OutMessage);
}

uint32 FQuarryMqttClient::Run()
{
	while (!bStopping.load())
	{
		if (State == EState::Connected)
		{
			PumpOutbound();
			PumpIncoming();
			EnsureKeepalive();
			if (!bStopping.load() && State == EState::Connected)
			{
				FPlatformProcess::Sleep(0.01f);
			}
		}
		else
		{
			// Idle/failed: wait out the backoff, then try to connect.
			for (float Waited = 0.0f; Waited < BackoffSeconds && !bStopping.load(); Waited += 0.25f)
			{
				FPlatformProcess::Sleep(0.25f);
			}
			if (bStopping.load())
			{
				break;
			}
			if (ConnectOnce())
			{
				HandleConnected();
			}
			else
			{
				BackoffSeconds = FMath::Min(10.0f, BackoffSeconds * 2.0f);
				UE_LOG(LogTemp, Warning, TEXT("QuarryWorks MQTT connect to %s:%u failed; next attempt in %.0fs"),
					*Host, Port, BackoffSeconds);
			}
		}
	}
	// Worker-owned socket: close and destroy here on every exit path so the
	// game thread never races a destroy against a live worker call.
	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			Subsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	bConnected.store(false);
	return 0;
}

bool FQuarryMqttClient::ConnectOnce()
{
	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Subsystem)
	{
		return false;
	}

	Socket = Subsystem->CreateSocket(NAME_Stream, TEXT("QuarryMqtt"), false);
	if (!Socket)
	{
		return false;
	}
	Socket->SetNonBlocking();

	TSharedRef<FInternetAddr> Address = Subsystem->CreateInternetAddr();
	bool bValid = false;
	if (Host == TEXT("localhost") || Host == TEXT("127.0.0.1"))
	{
		Address->SetLoopbackAddress();
		bValid = true;
	}
	else
	{
		FIPv4Address ParsedIp;
		bValid = FIPv4Address::Parse(Host, ParsedIp);
		if (bValid)
		{
			Address->SetIp(*ParsedIp.ToString(), bValid);
		}
	}
	if (!bValid)
	{
		Subsystem->DestroySocket(Socket);
		Socket = nullptr;
		UE_LOG(LogTemp, Warning, TEXT("QuarryWorks MQTT host '%s' is not a resolvable IPv4 address"), *Host);
		return false;
	}
	Address->SetPort(Port);

	// Non-blocking connect; success is decided by WaitForWrite below (an
	// immediate refusal shows up as a non-writable, erroring socket there).
	Socket->Connect(*Address);
	if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromSeconds(4.0)))
	{
		Subsystem->DestroySocket(Socket);
		Socket = nullptr;
		return false;
	}

	SendConnect();
	// Block until CONNACK (or timeout) before subscribing: strict brokers
	// reject packets that arrive before the connection is acknowledged.
	const double Deadline = FPlatformTime::Seconds() + 4.0;
	while (FPlatformTime::Seconds() < Deadline && !bStopping.load() && State != EState::Connected)
	{
		if (!Socket)
		{
			// TryParsePacket tore the connection down (e.g. CONNACK refusal).
			break;
		}
		uint32 PendingBytes = 0;
		if (Socket->HasPendingData(PendingBytes))
		{
			uint8 Buffer[8192];
			int32 BytesRead = 0;
			if (!Socket->Recv(Buffer, sizeof(Buffer), BytesRead) || BytesRead <= 0)
			{
				break;
			}
			RxBuffer.Append(Buffer, BytesRead);
			LastRecvTime = FPlatformTime::Seconds();
			while (State != EState::Connected && TryParsePacket())
			{
			}
		}
		else
		{
			FPlatformProcess::Sleep(0.01f);
		}
	}
	return State == EState::Connected;
}

void FQuarryMqttClient::HandleConnected()
{
	BackoffSeconds = 2.0f;
	SendSubscribe(Prefix + TEXT("/command"), 1);
	SendSubscribe(Prefix + TEXT("/task"), 1);
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks MQTT connected to %s:%u as %s (subscriptions: %s/command, %s/task)"),
		*Host, Port, *ClientId, *Prefix, *Prefix);
}

void FQuarryMqttClient::DisconnectAndBackoff()
{
	if (bConnected.load())
	{
		UE_LOG(LogTemp, Warning, TEXT("QuarryWorks MQTT connection lost; reconnecting with %.0fs backoff"), BackoffSeconds);
	}
	bConnected.store(false);
	State = EState::Idle;
	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			Subsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	RxBuffer.Reset();
}

void FQuarryMqttClient::PumpOutbound()
{
	TArray<FMqttMessage> Batch;
	{
		std::lock_guard<std::mutex> Lock(OutboundMutex);
		Batch = MoveTemp(OutboundQueue);
		OutboundQueue.Reset();
	}
	for (const FMqttMessage& Message : Batch)
	{
		SendPublish(Message.Topic, Message.Payload, Message.bRetain);
	}
}

void FQuarryMqttClient::PumpIncoming()
{
	FSocket* Current = Socket;
	if (!Current)
	{
		DisconnectAndBackoff();
		return;
	}
	uint32 PendingBytes = 0;
	if (!Current->HasPendingData(PendingBytes))
	{
		// Nothing inbound this poll; EnsureKeepalive owns liveness probing.
		return;
	}

	uint8 Buffer[8192];
	int32 BytesRead = 0;
	while (Current->Recv(Buffer, sizeof(Buffer), BytesRead) && BytesRead > 0)
	{
		RxBuffer.Append(Buffer, BytesRead);
		LastRecvTime = FPlatformTime::Seconds();
		if (RxBuffer.Num() >= sizeof(Buffer))
		{
			break;
		}
		if (!Current->HasPendingData(PendingBytes))
		{
			break;
		}
	}

	while (State == EState::Connected && TryParsePacket())
	{
		// drain all complete packets
	}
}

bool FQuarryMqttClient::TryParsePacket()
{
	if (RxBuffer.Num() < 2)
	{
		return false;
	}

	const uint8 FixedHeader = RxBuffer[0];
	uint32 RemainingLength = 0;
	uint32 Multiplier = 1;
	int32 VarintBytes = 0;
	for (int32 i = 1; i < RxBuffer.Num() && i <= 4; ++i)
	{
		RemainingLength += static_cast<uint32>(RxBuffer[i] & 0x7F) * Multiplier;
		Multiplier *= 128;
		++VarintBytes;
		if ((RxBuffer[i] & 0x80) == 0)
		{
			break;
		}
	}
	if (VarintBytes == 0 || (RxBuffer[VarintBytes] & 0x80) != 0)
	{
		// incomplete or malformed varint
		if (VarintBytes == 4)
		{
			UE_LOG(LogTemp, Warning, TEXT("QuarryWorks MQTT malformed packet header; dropping buffer"));
			RxBuffer.Reset();
		}
		return false;
	}
	const int32 HeaderSize = 1 + VarintBytes;
	if (RxBuffer.Num() < HeaderSize + static_cast<int32>(RemainingLength))
	{
		return false; // wait for the full payload
	}

	const uint8 Type = FixedHeader & 0xF0;
	const uint8 Flags = FixedHeader & 0x0F;
	const int32 BodyNum = static_cast<int32>(RemainingLength);
	// Snapshot the frame body BEFORE trimming the buffer: RemoveAt shifts the
	// remaining bytes over the region the body pointer refers to.
	TArray<uint8> FrameBody;
	FrameBody.Append(RxBuffer.GetData() + HeaderSize, BodyNum);
	RxBuffer.RemoveAt(0, HeaderSize + BodyNum, EAllowShrinking::No);
	const uint8* Body = FrameBody.GetData();

#if 1 // TEMP: inbound packet diagnostics
	static int32 InboundLogCount = 0;
	if (InboundLogCount < 30)
	{
		++InboundLogCount;
		UE_LOG(LogTemp, Log, TEXT("QuarryMqttRx: type=0x%02x flags=%d len=%d"), Type, Flags, BodyNum);
	}
#endif

	switch (Type)
	{
	case PacketConnack:
	{
		if (BodyNum < 2 || Body[1] != 0)
		{
			UE_LOG(LogTemp, Error, TEXT("QuarryWorks MQTT broker refused connection (return code %d, body %02x %02x %02x); rotating client id"),
				BodyNum >= 2 ? Body[1] : -1,
				BodyNum > 0 ? Body[0] : 0, BodyNum > 1 ? Body[1] : 0, BodyNum > 2 ? Body[2] : 0);
			// Rotate the client id: some brokers answer a rapid reconnect with
			// the previous session still half-open.
			ClientId = FString::Printf(TEXT("quarryworks-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
			DisconnectAndBackoff();
			return false;
		}
		State = EState::Connected;
		bConnected.store(true);
		LastSentTime = FPlatformTime::Seconds();
		HandleConnected();
		break;
	}
	case PacketPublish:
	{
		if (BodyNum < 2)
		{
			return false;
		}
		const int32 TopicLen = (Body[0] << 8) | Body[1];
		if (TopicLen <= 0 || BodyNum < 2 + TopicLen)
		{
			return false;
		}
		const FString Topic = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Body + 2));
		int32 PayloadOffset = 2 + TopicLen;
		if (Flags & 0x06) // QoS > 0: packet id present; acknowledge it
		{
			if (BodyNum < PayloadOffset + 2)
			{
				return false;
			}
			const uint16 PacketId = static_cast<uint16>((Body[PayloadOffset] << 8) | Body[PayloadOffset + 1]);
			SendPubAck(PacketId);
			PayloadOffset += 2;
		}
		FMqttMessage Message;
		Message.Topic = Topic;
		Message.Payload.Append(Body + PayloadOffset, BodyNum - PayloadOffset);
		InboundQueue.Enqueue(MoveTemp(Message));
		break;
	}
	case PacketPuback:
	case PacketPingresp:
		bPingInFlight = false;
		break;
	case PacketSuback:
		bPingInFlight = false;
		break;
	default:
		break;
	}
	return true;
}

void FQuarryMqttClient::EnsureKeepalive()
{
	const double Now = FPlatformTime::Seconds();
	// A publisher-only session legitimately receives nothing between pings:
	// outbound telemetry refreshes LastSentTime so the broker never drops us,
	// while inbound quiet only triggers a PINGREQ probe. Disconnect only when
	// the broker fails to answer the probe.
	if (Now - LastRecvTime > KeepaliveSeconds * 0.5 && !bPingInFlight)
	{
		SendPingReq();
		bPingInFlight = true;
	}
	if (bPingInFlight && Now - LastRecvTime > KeepaliveSeconds)
	{
		DisconnectAndBackoff();
	}
}

bool FQuarryMqttClient::AppendPacket(const TArray<uint8>& Body, uint8 FixedHeaderByte)
{
	FSocket* Current = Socket;
	if (!Current)
	{
		return false;
	}

	TArray<uint8> Packet;
	Packet.Reserve(1 + 4 + Body.Num());
	Packet.Add(FixedHeaderByte);
	AppendRemainingLength(Packet, Body.Num());
	Packet.Append(Body);

	int32 BytesSent = 0;
	if (!Current->Send(Packet.GetData(), Packet.Num(), BytesSent) || BytesSent != Packet.Num())
	{
		DisconnectAndBackoff();
		return false;
	}
	LastSentTime = FPlatformTime::Seconds();
	return true;
}

void FQuarryMqttClient::AppendMqttString(TArray<uint8>& Out, const TArray<uint8>& Utf8)
{
	const uint16 Length = static_cast<uint16>(Utf8.Num());
	Out.Add(static_cast<uint8>(Length >> 8));
	Out.Add(static_cast<uint8>(Length & 0xFF));
	Out.Append(Utf8);
}

void FQuarryMqttClient::AppendRemainingLength(TArray<uint8>& Out, uint32 Length)
{
	do
	{
		uint8 Digit = Length % 128;
		Length /= 128;
		if (Length > 0)
		{
			Digit |= 0x80;
		}
		Out.Add(Digit);
	} while (Length > 0);
}

void FQuarryMqttClient::SendConnect()
{
	TArray<uint8> Body;
	TArray<uint8> ProtocolName;
	TextToUtf8(TEXT("MQTT"), ProtocolName);
	AppendMqttString(Body, ProtocolName);
	Body.Add(0x04); // protocol level 4 (3.1.1)

	// Clean session + will (QoS0, retained): offline LWT on <prefix>/status.
	const FString WillTopic = Prefix + TEXT("/status");
	TArray<uint8> WillTopicUtf8;
	TextToUtf8(WillTopic, WillTopicUtf8);
	TArray<uint8> WillPayload;
	TextToUtf8(TEXT("{\"status\":\"offline\"}"), WillPayload);
	Body.Add(0x02 | 0x04 | 0x20); // clean session, will flag, will retain

	Body.Add(0x00);
	Body.Add(static_cast<uint8>(KeepaliveSeconds));

	TArray<uint8> ClientIdUtf8;
	TextToUtf8(ClientId, ClientIdUtf8);
	AppendMqttString(Body, ClientIdUtf8);
	AppendMqttString(Body, WillTopicUtf8);
	AppendMqttString(Body, WillPayload);

	AppendPacket(Body, PacketConnect);
}

void FQuarryMqttClient::SendSubscribe(const FString& TopicFilter, uint8 Qos)
{
	TArray<uint8> Body;
	const uint16 PacketId = NextPacketId++;
	Body.Add(static_cast<uint8>(PacketId >> 8));
	Body.Add(static_cast<uint8>(PacketId & 0xFF));
	TArray<uint8> TopicUtf8;
	TextToUtf8(TopicFilter, TopicUtf8);
	AppendMqttString(Body, TopicUtf8);
	Body.Add(Qos);
	UE_LOG(LogTemp, Log, TEXT("QuarryMqtt: SUBSCRIBE '%s' qos=%d pid=%u"), *TopicFilter, Qos, PacketId);
	AppendPacket(Body, PacketSubscribe);
}

void FQuarryMqttClient::SendPublish(const FString& Topic, const TArray<uint8>& Payload, bool bRetain)
{
	TArray<uint8> Body;
	TArray<uint8> TopicUtf8;
	TextToUtf8(Topic, TopicUtf8);
	AppendMqttString(Body, TopicUtf8);
	Body.Append(Payload); // QoS 0: no packet id
	AppendPacket(Body, PacketPublish | (bRetain ? 0x01 : 0x00));
}

void FQuarryMqttClient::SendPubAck(uint16 PacketId)
{
	TArray<uint8> Body;
	Body.Add(static_cast<uint8>(PacketId >> 8));
	Body.Add(static_cast<uint8>(PacketId & 0xFF));
	AppendPacket(Body, PacketPuback);
}

void FQuarryMqttClient::SendPingReq()
{
	TArray<uint8> Empty;
	AppendPacket(Empty, PacketPingreq);
}

void FQuarryMqttClient::SendDisconnect()
{
	TArray<uint8> Empty;
	AppendPacket(Empty, PacketDisconnect);
}

void FQuarryMqttClient::LogState(const FString& Line) const
{
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks MQTT: %s"), *Line);
}
