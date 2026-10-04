// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaScriptAsset.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(UEmkaScriptAsset)

namespace
{
bool IsValidModulePath(const FString& Path)
{
	if (Path.IsEmpty() || FTCHARToUTF8(*Path).Length() > 255 || !Path.EndsWith(TEXT(".um"), ESearchCase::CaseSensitive) || Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("\\"))) return false;
	for (const TCHAR Ch : Path)
	{
		if (Ch < TEXT(' ') || Ch == TEXT(':') || Ch == TEXT('*') || Ch == TEXT('?') || Ch == TEXT('"') || Ch == TEXT('<') || Ch == TEXT('>') || Ch == TEXT('|')) return false;
	}
	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("/"), false);
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..")) return false;
	}
	return true;
}

bool VisitAsset(const UUEmkaScriptAsset* Asset, const UUEmkaScriptAsset* Root, TMap<FString, const UUEmkaScriptAsset*>& Paths,
	TSet<const UUEmkaScriptAsset*>& Visiting, TSet<const UUEmkaScriptAsset*>& Visited, TArray<FUEmkaModuleSource>& Modules, FString& Error)
{
	if (!Asset)
	{
		Error = TEXT("Missing Umka script asset reference");
		return false;
	}
	// Hard references can exist before their serialized source and imports have been loaded.
	const_cast<UUEmkaScriptAsset*>(Asset)->ConditionalPreload();
	if (Visiting.Contains(Asset))
	{
		Error = FString::Printf(TEXT("Cyclic Umka asset import at '%s'"), *Asset->ModulePath);
		return false;
	}
	if (Visited.Contains(Asset)) return true;
	const FString ModulePath = Asset->ModulePath.IsEmpty() ? Asset->GetName() + TEXT(".um") : Asset->ModulePath;
	if (!IsValidModulePath(ModulePath))
	{
		Error = FString::Printf(TEXT("Invalid Umka module path '%s': use a relative .um filename of at most 255 UTF-8 bytes without empty, '.' or '..' segments"), *ModulePath);
		return false;
	}
	if (const UUEmkaScriptAsset* const* Existing = Paths.Find(ModulePath))
	{
		if (*Existing != Asset)
		{
			Error = FString::Printf(TEXT("Conflicting Umka module path '%s'"), *ModulePath);
			return false;
		}
	}
	Paths.Add(ModulePath, Asset);
	Visiting.Add(Asset);
	for (const UUEmkaScriptAsset* Import : Asset->Imports)
	{
		if (!VisitAsset(Import, Root, Paths, Visiting, Visited, Modules, Error)) return false;
	}
	Visiting.Remove(Asset);
	Visited.Add(Asset);
	if (Asset != Root) Modules.Add({ModulePath, Asset->Source});
	return true;
}
}

bool UUEmkaScriptAsset::ResolveAsset(const UUEmkaScriptAsset* Asset, FString& OutSource, FString& OutFileName, TArray<FUEmkaModuleSource>& OutModules, FString& OutError)
{
	OutSource.Reset();
	OutFileName.Reset();
	OutModules.Reset();
	OutError.Reset();
	TMap<FString, const UUEmkaScriptAsset*> Paths;
	TSet<const UUEmkaScriptAsset*> Visiting;
	TSet<const UUEmkaScriptAsset*> Visited;
	if (!VisitAsset(Asset, Asset, Paths, Visiting, Visited, OutModules, OutError))
	{
		OutModules.Reset();
		return false;
	}
	OutSource = Asset->Source;
	OutFileName = Asset->ModulePath.IsEmpty() ? Asset->GetName() + TEXT(".um") : Asset->ModulePath;
	return true;
}

#if WITH_EDITOR
UUEmkaScriptAsset::FOnScriptAssetChanged UUEmkaScriptAsset::OnScriptAssetChanged;

void UUEmkaScriptAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnScriptAssetChanged.Broadcast(this);
}

void UUEmkaScriptAsset::PostEditUndo()
{
	Super::PostEditUndo();
	OnScriptAssetChanged.Broadcast(this);
}
#endif
