using System.IO;
using UnrealBuildTool;

public class MediaPipeDriver : ModuleRules
{
	public MediaPipeDriver(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory,
			Path.Combine(ModuleDirectory, "Avatar"),
			Path.Combine(ModuleDirectory, "BodyFusion"),
			Path.Combine(ModuleDirectory, "Core"),
			Path.Combine(ModuleDirectory, "Diagnostics"),
			Path.Combine(ModuleDirectory, "Embodiment"),
			Path.Combine(ModuleDirectory, "PoseDriven"),
			Path.Combine(ModuleDirectory, "PoseDriven", "Inline"),
			Path.Combine(ModuleDirectory, "Quest"),
			Path.Combine(ModuleDirectory, "Runtime"),
			Path.Combine(ModuleDirectory, "Tests"),
			Path.Combine(ModuleDirectory, "Tracking")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MediaAssets",
			"RenderCore",
			"RHI",
			"AnimGraphRuntime",
			"AnimationCore",
			"ControlRig",
			"HeadMountedDisplay",
			"InputCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper",
			"Json",
			"Media",
			"MediaUtils",
			"OpenXRHMD",
			"Slate",
			"SlateCore"
		});

		PrivateIncludePathModuleNames.Add("OpenXR");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.AddRange(new string[]
			{
				"mf.lib",
				"mfplat.lib",
				"mfreadwrite.lib",
				"mfuuid.lib"
			});
		}
	}
}
