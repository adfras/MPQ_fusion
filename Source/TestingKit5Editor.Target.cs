// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class TestingKit5EditorTarget : TargetRules
{
	public TestingKit5EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.AddRange(new string[] { "TestingKit5", "MediaPipeDriver", "MediaPipeDriverEditor", "DyadLink", "DyadStudy" });
	}
}
