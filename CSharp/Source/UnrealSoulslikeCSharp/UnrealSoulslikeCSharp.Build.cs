using UnrealBuildTool;

// The stub native module UnrealSharp expects next to the C# game. Its only code is
// USoulslikeNativeLibrary: engine state the C++ game reads directly but that isn't reflected
// (and so isn't reachable from C#). Every entry there is a logged reflection gap.
public class UnrealSoulslikeCSharp : ModuleRules
{
	public UnrealSoulslikeCSharp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"RenderCore",
		});
	}
}
