// Copyright Solessfir 2026. All Rights Reserved.

using UnrealBuildTool;

public class UEmkaEditor : ModuleRules
{
	public UEmkaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UEmka",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"BlueprintGraph",
			"GraphEditor",
			"Slate",
			"SlateCore",
			"KismetCompiler",
			"InputCore",
			"EditorStyle",
			"KismetWidgets",
		});
	}
}
