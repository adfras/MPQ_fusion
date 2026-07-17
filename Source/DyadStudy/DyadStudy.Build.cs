using System.IO;
using UnrealBuildTool;

public class DyadStudy : ModuleRules
{
	public DyadStudy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory,
			Path.Combine(ModuleDirectory, "Tests")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"InputCore",
			"EnhancedInput",
			"HeadMountedDisplay",
			"UMG",
			"Slate",
			"SlateCore",
			"MediaPipeDriver",
			"DyadLink"
		});

		if (Target.bBuildEditor)
		{
			// Editor-only: mp.DyadAuthorMenuWidget rebuilds the menu WidgetBlueprint's
			// designer tree (DyadMenuWidgetAssetAuthor.cpp).
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UMGEditor",
				"UnrealEd"
			});
		}
	}
}
