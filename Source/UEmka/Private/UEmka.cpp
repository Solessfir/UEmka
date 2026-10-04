// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmka.h"
#include "UEmkaFunctionLibrary.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CoreDelegates.h"

void FUEmkaModule::StartupModule()
{
	GarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddStatic(&UUEmkaFunctionLibrary::CleanupInvalidRuntimeSessions);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld*, bool, bool) { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); });
	PreExitHandle = FCoreDelegates::OnPreExit.AddStatic(&UUEmkaFunctionLibrary::ResetAllRuntimeSessions);
}

void FUEmkaModule::ShutdownModule()
{
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(GarbageCollectHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	FCoreDelegates::OnPreExit.Remove(PreExitHandle);
	UUEmkaFunctionLibrary::ResetAllRuntimeSessions();
}

IMPLEMENT_MODULE(FUEmkaModule, UEmka)
