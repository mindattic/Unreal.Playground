using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealSoulslikeTarget : TargetRules
{
	public UnrealSoulslikeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSoulslike");
	}
}
