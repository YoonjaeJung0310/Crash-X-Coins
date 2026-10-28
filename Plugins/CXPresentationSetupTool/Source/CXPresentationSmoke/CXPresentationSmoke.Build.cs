using UnrealBuildTool;

public class CXPresentationSmoke : ModuleRules
{
    public CXPresentationSmoke(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Json", "CX", "ChaosVehicles", "PhysicsCore", "EnhancedInput", "InputCore", "UMG" });
    }
}
