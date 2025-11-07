// Copyright Solessfir. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

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

		string BasePath = Path.Combine(PluginDirectory, "Source", "ThirdParty");
		string IncludePath = Path.Combine(BasePath, "include");
		string LibPath = Path.Combine(BasePath, "lib", Target.Platform.ToString());

		PublicIncludePaths.Add(IncludePath);
		PublicDefinitions.AddRange(new string[] {"WITH_UMKA", "UMKA_STATIC"});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libumka_static.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libumka.a"));
		}
	}
}
