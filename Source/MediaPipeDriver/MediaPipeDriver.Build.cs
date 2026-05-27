using UnrealBuildTool;

public class MediaPipeDriver : ModuleRules
{
	public MediaPipeDriver(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		PublicIncludePaths.Add(ModuleDirectory);

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
			"OpenXRHMD"
		});

		PrivateIncludePathModuleNames.Add("OpenXR");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");
	}
}
