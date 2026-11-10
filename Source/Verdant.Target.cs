using UnrealBuildTool;
public class VerdantTarget : TargetRules {
 public VerdantTarget(TargetInfo Target) : base(Target) { Type = TargetType.Game; DefaultBuildSettings = BuildSettingsVersion.V7; IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("Verdant"); }
}
