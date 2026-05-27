using UnrealBuildTool;

public class CodexAgent : ModuleRules
{
	public CodexAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"LevelEditor",
			"HTTP",
			"Json",
			"JsonUtilities",
			"Projects",
			"Sockets"
		});
	}
}
