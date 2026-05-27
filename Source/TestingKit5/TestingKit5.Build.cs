// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TestingKit5 : ModuleRules
{
	public TestingKit5(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"TestingKit5",
			"TestingKit5/Variant_Platforming",
			"TestingKit5/Variant_Platforming/Animation",
			"TestingKit5/Variant_Combat",
			"TestingKit5/Variant_Combat/AI",
			"TestingKit5/Variant_Combat/Animation",
			"TestingKit5/Variant_Combat/Gameplay",
			"TestingKit5/Variant_Combat/Interfaces",
			"TestingKit5/Variant_Combat/UI",
			"TestingKit5/Variant_SideScrolling",
			"TestingKit5/Variant_SideScrolling/AI",
			"TestingKit5/Variant_SideScrolling/Gameplay",
			"TestingKit5/Variant_SideScrolling/Interfaces",
			"TestingKit5/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
