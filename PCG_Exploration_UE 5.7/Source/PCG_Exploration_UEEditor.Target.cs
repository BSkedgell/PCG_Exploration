using UnrealBuildTool;
using System.Collections.Generic;

public class PCG_Exploration_UEEditorTarget : TargetRules
{
    public PCG_Exploration_UEEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;

        // Use UE 5.7 defaults
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;

        // Do NOT override UndefinedIdentifierWarningLevel here in 5.7+
        // Do NOT modify shared build environment properties.

        ExtraModuleNames.Add("PCG_Exploration_UE");
    }
}
