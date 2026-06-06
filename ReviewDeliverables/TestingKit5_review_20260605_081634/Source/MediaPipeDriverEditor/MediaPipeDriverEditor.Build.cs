using UnrealBuildTool;

public class MediaPipeDriverEditor : ModuleRules
{
	public MediaPipeDriverEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		OptimizeCode = CodeOptimization.Never;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MediaAssets",
			"MediaPipeDriver"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Media",
			"MediaUtils",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"ToolMenus"
		});
	}
}
