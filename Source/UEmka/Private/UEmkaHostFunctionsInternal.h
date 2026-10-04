// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "UEmkaHostFunctions.h"
#include "UEmkaScriptAsset.h"

namespace UEmkaHostFunctions
{
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
