// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "umka_api.h"

class UWorld;

namespace UEmkaHostFunctions
{
struct FObjectHandles;
class FScopedCallContext;

using FObjectHandle = uint64;

// Available only during game-thread execution; never retain the context pointer after a callback.
class UEMKA_API FCallContext
{
public:
	UObject* GetCaller() const;
	UWorld* GetWorld() const;
	FGuid GetSessionId() const;
	FObjectHandle CreateObjectHandle(UObject* Object) const;
	UObject* ResolveObjectHandle(FObjectHandle Handle) const;

	FCallContext(const FCallContext&) = delete;
	FCallContext& operator=(const FCallContext&) = delete;

private:
	friend class FScopedCallContext;
	FCallContext() = default;
	TWeakObjectPtr<UObject> Caller;
	TWeakObjectPtr<UWorld> World;
	FGuid SessionId;
	FObjectHandles* Handles = nullptr;
};

UEMKA_API const FCallContext* GetCurrentContext();

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
