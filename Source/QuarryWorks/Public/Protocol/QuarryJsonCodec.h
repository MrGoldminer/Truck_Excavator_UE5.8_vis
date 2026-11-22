#pragma once

#include "CoreMinimal.h"

class UQuarryWorksiteSubsystem;
struct FQuarryTaskCommand;

/**
 * CARLA-aligned JSON codec for the MQTT management plane. Telemetry field
 * names/units mirror the carla Python API (locations in meters, velocities in
 * m/s, IMU in m/s^2 and rad/s, compass in radians, GNSS in degrees). Control
 * messages use carla.VehicleControl semantics (throttle [0,1], steer [-1,1],
 * brake [0,1], hand_brake, reverse). See Docs/CARLA_ALIGNMENT.md.
 */
class QUARRYWORKS_API FQuarryJsonCodec
{
public:
	/** CARLA-style control parsed from an MQTT command payload. */
	struct FCarlaControl
	{
		FString VehicleId;
		float Throttle = 0.0f;   // [0, 1]
		float Steer = 0.0f;      // [-1, 1]
		float Brake = 0.0f;      // [0, 1]
		bool bHandBrake = false;
		bool bReverse = false;
		bool bValid = false;
	};

	static bool ParseControl(const FString& JsonText, FCarlaControl& OutControl);
	static bool ParseTask(const FString& JsonText, FQuarryTaskCommand& OutTask);

	/** Full-featured CARLA-named telemetry snapshot. */
	static FString EncodeTelemetry(const UQuarryWorksiteSubsystem* Worksite, double GnssOriginLat, double GnssOriginLon);
	static FString EncodeTaskResult(const FQuarryTaskCommand& Task, const FString& Result, const FString& Reason, double DeliveredMassKg, double ElapsedS);
	static FString EncodeTestCase(const FString& CaseName, bool bPass, const FString& MetricsJson, double DurationS);
	static FString EncodeTestSummary(const TArray<FString>& CaseResults, int32 NumPassed, int32 NumTotal);
	static FString EncodeStatus(bool bOnline);
};
