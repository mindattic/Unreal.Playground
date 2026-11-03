using UnrealBuildTool;
using System.Collections.Generic;

public class UnrealSoulslikeEditorTarget : TargetRules
{
	public UnrealSoulslikeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UnrealSoulslike");
	}
}
