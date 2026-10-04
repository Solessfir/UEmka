// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaScriptAssetFactory.h"
#include "UEmkaScriptAsset.h"

UUEmkaScriptAssetFactory::UUEmkaScriptAssetFactory()
{
	SupportedClass = UUEmkaScriptAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UUEmkaScriptAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UUEmkaScriptAsset>(InParent, Class, Name, Flags);
}
