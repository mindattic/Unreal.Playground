using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealSoulslikeTarget : TargetRules
{
	public UnrealSoulslikeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSoulslike");
	}
}
