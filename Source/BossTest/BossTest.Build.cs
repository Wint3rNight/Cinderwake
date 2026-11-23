// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BossTest : ModuleRules
{
	public BossTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory); // lets files include "Combat/..." from subfolders
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AIModule", "AssetRegistry" });

		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" }); // frame timings in Tests/PerfTests.cpp

		// The PIE smoke test (Tests/SmokeTests.cpp) uses the editor's automation helpers.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("PhysicsUtilities"); // Tools/BossTestEditorTools: generating the boss's ragdoll physics asset
			PrivateDependencyModuleNames.AddRange(new string[] { "AnimationModifiers", "AnimationBlueprintLibrary" }); // Tools/BossTestEditorTools: mirroring borrowed clips
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
