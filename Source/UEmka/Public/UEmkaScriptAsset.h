// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "UEmkaScriptAsset.generated.h"

struct UEMKA_API FUEmkaModuleSource
{
	FString FileName;
	FString Source;
};

UCLASS(BlueprintType)
class UEMKA_API UUEmkaScriptAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Umka", Meta = (MultiLine = true))
	FString Source;

	// Virtual import filename, such as lib/helpers.um.
	// Use a relative .um path with forward slashes, at most 255 UTF-8 bytes, and no empty, '.' or '..' segments.
	// Leave empty to use the asset name plus .um.
	// The linked source file is shown in Import Settings.
	UPROPERTY(EditAnywhere, Category = "Umka")
	FString ModulePath;

	// Strong references keep the complete module graph available in cooked builds.
	UPROPERTY(EditAnywhere, Category = "Umka")
	TArray<TObjectPtr<UUEmkaScriptAsset>> Imports;

	static bool IsValidModulePath(const FString& Path);
	static bool ResolveAsset(const UUEmkaScriptAsset* Asset, FString& OutSource, FString& OutFileName, TArray<FUEmkaModuleSource>& OutModules, FString& OutError);

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Import Settings")
	TObjectPtr<class UAssetImportData> AssetImportData;

	virtual void PostInitProperties() override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
#endif

#if WITH_EDITOR
	bool IsFileBacked() const;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnScriptAssetChanged, UUEmkaScriptAsset*);
	static FOnScriptAssetChanged OnScriptAssetChanged;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif
};
