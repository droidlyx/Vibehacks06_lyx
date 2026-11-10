using UnrealBuildTool;
public class Verdant : ModuleRules {
 public Verdant(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new [] {"Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent", "UMG", "Slate", "SlateCore", "HTTP", "Json", "JsonUtilities"});
 }
}
