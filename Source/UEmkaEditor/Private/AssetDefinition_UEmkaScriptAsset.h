// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"
#include "UEmkaScriptAsset.h"
#include "AssetDefinition_UEmkaScriptAsset.generated.h"

UCLASS()
class UAssetDefinition_UEmkaScriptAsset : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override { return NSLOCTEXT("UEmka", "ScriptAsset", "UEmka Script Asset"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(80, 180, 220)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UUEmkaScriptAsset::StaticClass(); }
};
