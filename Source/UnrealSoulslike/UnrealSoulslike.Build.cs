using UnrealBuildTool;

public class UnrealSoulslike : ModuleRules
{
	public UnrealSoulslike(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"ProceduralMeshComponent",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Json",
		});
	}
}
