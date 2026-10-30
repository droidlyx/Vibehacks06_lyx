using UnrealBuildTool;
public class VerdantEditorTarget : TargetRules {
 public VerdantEditorTarget(TargetInfo Target) : base(Target) { Type = TargetType.Editor; DefaultBuildSettings = BuildSettingsVersion.V7; IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("Verdant"); }
}
