#include "Worksite/QuarryConfigSubsystem.h"

#include "google/protobuf/text_format.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	void SetDefaults(quarryworks::v1::VehicleConfig& Config, double Mass, double Engine, double Brake,
		double ForwardSpeed, double ReverseSpeed, double Capacity, double WorkRate)
	{
		Config.set_base_mass_kg(Mass);
		Config.set_engine_force_n(Engine);
		Config.set_brake_force_n(Brake);
		Config.set_max_forward_speed_cm_s(ForwardSpeed);
		Config.set_max_reverse_speed_cm_s(ReverseSpeed);
		Config.set_cargo_capacity_kg(Capacity);
		Config.set_work_rate_kg_s(WorkRate);
	}
}

void UQuarryConfigSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString ConfigPath = FPaths::ProjectConfigDir() / TEXT("QuarryWorks.pbtxt");
	FString ConfigText;
	const bool bLoaded = FFileHelper::LoadFileToString(ConfigText, *ConfigPath);
	const bool bParsed = bLoaded && google::protobuf::TextFormat::ParseFromString(TCHAR_TO_UTF8(*ConfigText), &Config);
	if (!bParsed)
	{
		Config.Clear();
		SetDefaults(*Config.mutable_excavator(), 24000.0, 100000.0, 180000.0, 500.0, 350.0, 5000.0, 2500.0);
		SetDefaults(*Config.mutable_mining_truck(), 30000.0, 140000.0, 240000.0, 1300.0, 450.0, 60000.0, 6000.0);
	}
	Config.set_schema_version(1);
	UE_LOG(LogTemp, Log, TEXT("QuarryWorks configuration loaded from %s"), *ConfigPath);
}
