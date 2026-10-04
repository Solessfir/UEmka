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

	// Virtual filename relative to the module root. Empty uses the asset name plus .um.
	UPROPERTY(EditAnywhere, Category = "Umka")
	FString ModulePath;

	// Strong references keep the complete module graph available in cooked builds.
	UPROPERTY(EditAnywhere, Category = "Umka")
	TArray<TObjectPtr<UUEmkaScriptAsset>> Imports;

	static bool ResolveAsset(const UUEmkaScriptAsset* Asset, FString& OutSource, FString& OutFileName, TArray<FUEmkaModuleSource>& OutModules, FString& OutError);

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnScriptAssetChanged, UUEmkaScriptAsset*);
	static FOnScriptAssetChanged OnScriptAssetChanged;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif
};
