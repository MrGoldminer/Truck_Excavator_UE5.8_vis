using UnrealBuildTool;
using System.IO;

public class QuarryWorks : ModuleRules
{
	public QuarryWorks(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicSystemIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/Abseil")));
		PublicSystemIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/Protobuf")));

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"EnhancedInput",
				"PhysicsCore",
				"ProceduralMeshComponent",
				"Protobuf"
			});
	}
}
