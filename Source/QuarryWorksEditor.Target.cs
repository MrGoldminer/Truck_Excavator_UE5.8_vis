using UnrealBuildTool;

public class QuarryWorksEditorTarget : TargetRules
{
	public QuarryWorksEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("QuarryWorks");
	}
}
