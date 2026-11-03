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
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Json",
			"RenderCore",
		});
		PublicIncludePaths.AddRange(new string[] {
			"UnrealSoulslike",
			"UnrealSoulslike/Souls",
			"UnrealSoulslike/ThirdPerson",
			"UnrealSoulslike/ThirdPerson/Variant_Platforming",
			"UnrealSoulslike/ThirdPerson/Variant_Platforming/Animation",
			"UnrealSoulslike/ThirdPerson/Variant_Combat",
			"UnrealSoulslike/ThirdPerson/Variant_Combat/AI",
			"UnrealSoulslike/ThirdPerson/Variant_Combat/Animation",
			"UnrealSoulslike/ThirdPerson/Variant_Combat/Gameplay",
			"UnrealSoulslike/ThirdPerson/Variant_Combat/Interfaces",
			"UnrealSoulslike/ThirdPerson/Variant_Combat/UI",
			"UnrealSoulslike/ThirdPerson/Variant_SideScrolling",
			"UnrealSoulslike/ThirdPerson/Variant_SideScrolling/AI",
			"UnrealSoulslike/ThirdPerson/Variant_SideScrolling/Gameplay",
			"UnrealSoulslike/ThirdPerson/Variant_SideScrolling/Interfaces",
			"UnrealSoulslike/ThirdPerson/Variant_SideScrolling/UI"
		});
	}
}
