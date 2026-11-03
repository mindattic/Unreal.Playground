using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealSoulslikeCSharpTarget : TargetRules
{
	public UnrealSoulslikeCSharpTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSoulslikeCSharp");
	}
}
