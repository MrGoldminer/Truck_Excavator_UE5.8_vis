#pragma once

#include "CoreMinimal.h"

class UQuarryWorksiteSubsystem;

class QUARRYWORKS_API FQuarryProtocol
{
public:
	static TArray<uint8> EncodeTelemetry(const UQuarryWorksiteSubsystem* Worksite);
	static bool ApplyVehicleCommand(const TArray<uint8>& CommandBytes, UQuarryWorksiteSubsystem* Worksite);
	static FString DecodeTelemetryForDebug(const TArray<uint8>& TelemetryBytes);
};
