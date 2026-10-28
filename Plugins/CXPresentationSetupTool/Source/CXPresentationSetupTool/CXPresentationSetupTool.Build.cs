using UnrealBuildTool;

public class CXPresentationSetupTool : ModuleRules
{
    public CXPresentationSetupTool(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "CX", "ChaosVehicles", "UnrealEd", "AssetRegistry", "AssetTools", "Json", "RenderCore", "RHI",
            "ImageWrapper", "KismetCompiler", "BlueprintGraph"
        });
    }
}
