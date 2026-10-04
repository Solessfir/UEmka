// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "Commandlets/Commandlet.h"
#include "UEmkaCookFixtureCommandlet.generated.h"

UCLASS()
class UUEmkaCookFixtureCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UUEmkaCookFixtureCommandlet();
	virtual int32 Main(const FString& Params) override;
};
