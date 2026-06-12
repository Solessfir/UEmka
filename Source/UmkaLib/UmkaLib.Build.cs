// Copyright Solessfir 2026. All Rights Reserved.

using UnrealBuildTool;

// Vendored Umka interpreter (https://github.com/vtereshkov/umka-lang, BSD-2-Clause - see LICENSE).
// Plain C compiled directly by UBT - no prebuilt static libraries, works on any target platform.
public class UmkaLib : ModuleRules
{
	public UmkaLib(ReadOnlyTargetRules Target) : base(Target)
	{
		// C sources - no PCH, no Unity blobs
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		CStandard = CStandardVersion.C11;

		// Only for the IMPLEMENT_MODULE stub in UmkaLibModule.cpp
		PrivateDependencyModuleNames.Add("Core");

		// Strict -std=c11 makes glibc hide POSIX declarations
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PrivateDefinitions.Add("_POSIX_C_SOURCE=200809L");
			PrivateDefinitions.Add("_DEFAULT_SOURCE=1");
		}

		// Third-party code is not held to Engine warning standards
		bWarningsAsErrors = false;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;

		if (Target.LinkType == TargetLinkType.Monolithic)
		{
			// Monolithic builds (Shipping): static linkage, UMKA_STATIC strips export/import
			PublicDefinitions.Add("UMKA_STATIC=1");
		}
		else
		{
			// Modular builds (Editor): each module is a DLL, UMKA_BUILD exports the API.
			PrivateDefinitions.Add("UMKA_BUILD=1");
		}
	}
}
