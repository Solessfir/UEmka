// Copyright Solessfir 2026. All Rights Reserved.

using UnrealBuildTool;

public class UEmka : ModuleRules
{
	public UEmka(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange
		(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		// Umka interpreter, compiled from vendored sources - see Source/UmkaLib
		PrivateDependencyModuleNames.Add("UmkaLib");
	}
}
