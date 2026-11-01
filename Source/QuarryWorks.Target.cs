using UnrealBuildTool;

public class QuarryWorksTarget : TargetRules
{
	public QuarryWorksTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("QuarryWorks");
	}
}
