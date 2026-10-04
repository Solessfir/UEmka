// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "UEmkaHostFunctions.h"
#include "UEmkaScriptAsset.h"

namespace UEmkaHostFunctions
{
// A retained VM owns its handles. Replacing or destroying the VM invalidates every token.
struct FObjectHandles
{
	TMap<FObjectHandle, TWeakObjectPtr<UObject>> Objects;
};

class FScopedCallContext
{
public:
	FScopedCallContext(UObject* Caller, const FGuid& SessionId, FObjectHandles* Handles = nullptr);
	~FScopedCallContext();
	FScopedCallContext(const FScopedCallContext&) = delete;
	FScopedCallContext& operator=(const FScopedCallContext&) = delete;

private:
	FCallContext Context;
	const FCallContext* Previous = nullptr;
};

struct FSnapshot
{
	TArray<FUEmkaModuleSource> Modules;
	TArray<FFunction> Functions;
};

// Every VM retains its snapshot lease until after umkaFree, including failed initialization.
void AcquireSnapshot(FSnapshot& Snapshot);
void ReleaseSnapshot();
}

namespace UEmkaRuntime
{
void ResetIdleSessions();
}
