// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "umka_api.h"

namespace UEmkaHostFunctions
{
struct UEMKA_API FFunction
{
	FString Name;
	UmkaExternFunc Callback = nullptr;
};

// Register declarations and callbacks on the game thread before Blueprint compilation or script execution.
// Sources and names are copied; unregister before unloading the module that owns the callbacks.
UEMKA_API bool RegisterModule(const FString& ModulePath, const FString& Source, TConstArrayView<FFunction> Functions, FString& Error);
UEMKA_API bool UnregisterModule(const FString& ModulePath, FString& Error);
}
