// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "UEmkaScriptAssetFactory.generated.h"

UCLASS()
class UUEmkaScriptAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UUEmkaScriptAssetFactory();
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
