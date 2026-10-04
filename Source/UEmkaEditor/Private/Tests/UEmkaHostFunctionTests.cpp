// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaHostFunctions.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UEmkaFunctionLibrary.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
void HostDouble(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	umkaGetResult(Params, Result)->intVal = umkaGetParam(Params, 0)->intVal * 2;
}

bool bMutationRejected = false;
FString MutationError;

void HostMutation(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	bMutationRejected = !UEmkaHostFunctions::UnregisterModule(TEXT("tests/mutation.um"), MutationError);
	umkaGetResult(Params, Result)->intVal = bMutationRejected ? 1 : 0;
}

TWeakObjectPtr<UObject> ExpectedCaller;
TWeakObjectPtr<UWorld> ExpectedWorld;
FGuid ExpectedSessionId;
TWeakObjectPtr<UObject> HandleTarget;
UEmkaHostFunctions::FObjectHandle FinalizerOuterHandle = 0;
int32 FinalizerCount = 0;
bool bFinalizerContextSafe = false;

void HostFinalizer(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	const UEmkaHostFunctions::FCallContext* Context = UEmkaHostFunctions::GetCurrentContext();
	++FinalizerCount;
	bFinalizerContextSafe = Context && !Context->GetCaller() && !Context->GetWorld()
		&& !Context->GetSessionId().IsValid() && !Context->ResolveObjectHandle(FinalizerOuterHandle)
		&& Context->CreateObjectHandle(HandleTarget.Get()) == 0;
}

void HostAllocateFinalizer(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	void* Data = umkaAllocData(umkaGetInstance(Result), sizeof(int32), &HostFinalizer);
	umkaGetResult(Params, Result)->intVal = Data ? 0 : 1;
}

void HostContext(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	const UEmkaHostFunctions::FCallContext* Context = UEmkaHostFunctions::GetCurrentContext();
	umkaGetResult(Params, Result)->intVal = Context && Context->GetCaller() == ExpectedCaller.Get()
		&& Context->GetWorld() == ExpectedWorld.Get() && Context->GetSessionId() == ExpectedSessionId ? 1 : 0;
}

void HostCreateHandle(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	const UEmkaHostFunctions::FCallContext* Context = UEmkaHostFunctions::GetCurrentContext();
	umkaGetResult(Params, Result)->uintVal = Context ? Context->CreateObjectHandle(HandleTarget.Get()) : 0;
}

void HostResolveHandle(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	const UEmkaHostFunctions::FCallContext* Context = UEmkaHostFunctions::GetCurrentContext();
	UObject* Object = Context ? Context->ResolveObjectHandle(umkaGetParam(Params, 0)->uintVal) : nullptr;
	umkaGetResult(Params, Result)->intVal = Object && Object == HandleTarget.Get() ? 1 : 0;
}

void HostNestedContext(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	const UEmkaHostFunctions::FCallContext* Outer = UEmkaHostFunctions::GetCurrentContext();
	if (!Outer)
	{
		umkaGetResult(Params, Result)->intVal = 0;
		return;
	}
	UObject* Caller = Outer->GetCaller();
	UWorld* World = Outer->GetWorld();
	const FGuid SessionId = Outer->GetSessionId();
	const UEmkaHostFunctions::FObjectHandle Handle = Outer->CreateObjectHandle(HandleTarget.Get());
	const TWeakObjectPtr<UObject> SavedCaller = ExpectedCaller;
	const TWeakObjectPtr<UWorld> SavedWorld = ExpectedWorld;
	const FGuid SavedSessionId = ExpectedSessionId;
	ExpectedCaller = nullptr;
	ExpectedWorld = nullptr;
	ExpectedSessionId = {};
	FinalizerOuterHandle = Handle;
	const int32 PreviousFinalizerCount = FinalizerCount;
	FUEmkaScriptParam NestedResult;
	FString Error;
	const bool bSuccess = UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		TEXT("import \"tests/context.um\"\nfn Nested*(Handle: uint): int { return context::UEmkaHostContext() - context::UEmkaHostResolveHandle(Handle) + context::UEmkaHostAllocateFinalizer() }"),
		TEXT("Nested"), {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::UInt, static_cast<int64>(Handle))},
		EUEmkaValueType::Int, false, false, NestedResult, Error);
	ExpectedCaller = SavedCaller;
	ExpectedWorld = SavedWorld;
	ExpectedSessionId = SavedSessionId;
	umkaGetResult(Params, Result)->intVal = bSuccess && NestedResult.IntValue == 1
		&& FinalizerCount == PreviousFinalizerCount + 1 && bFinalizerContextSafe
		&& UEmkaHostFunctions::GetCurrentContext() == Outer && Outer->GetCaller() == Caller
		&& Outer->GetWorld() == World && Outer->GetSessionId() == SessionId
		&& Outer->ResolveObjectHandle(Handle) == HandleTarget.Get() ? 1 : 0;
}

const FString ContextModule = TEXT("fn UEmkaHostContext*(): int\nfn UEmkaHostCreateHandle*(): uint\nfn UEmkaHostResolveHandle*(Handle: uint): int\nfn UEmkaHostNestedContext*(): int\nfn UEmkaHostAllocateFinalizer*(): int");

const TArray<UEmkaHostFunctions::FFunction> ContextFunctions = {
	{TEXT("UEmkaHostContext"), &HostContext}, {TEXT("UEmkaHostCreateHandle"), &HostCreateHandle},
	{TEXT("UEmkaHostResolveHandle"), &HostResolveHandle}, {TEXT("UEmkaHostNestedContext"), &HostNestedContext},
	{TEXT("UEmkaHostAllocateFinalizer"), &HostAllocateFinalizer}};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaHostModuleExecutionTest, "UEmka.HostFunctions.ModulesAndExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaHostModuleExecutionTest::RunTest(const FString& Parameters)
{
	FString Error;
	FString Source = TEXT("fn UEmkaHostDouble*(Value: int): int");
	TArray<UEmkaHostFunctions::FFunction> Functions = {{TEXT("UEmkaHostDouble"), &HostDouble}};
	if (!TestTrue(TEXT("Host module registers"), UEmkaHostFunctions::RegisterModule(TEXT("tests/host.um"), Source, Functions, Error))) return false;
	ON_SCOPE_EXIT
	{
		UEmkaHostFunctions::UnregisterModule(TEXT("tests/wrapper.um"), Error);
		UEmkaHostFunctions::UnregisterModule(TEXT("tests/host.um"), Error);
	};
	Source = TEXT("invalid source");
	Functions[0].Name = TEXT("ChangedName");
	Functions[0].Callback = nullptr;
	TestTrue(TEXT("Module source and callback list are copied"), UEmkaHostFunctions::RegisterModule(TEXT("tests/wrapper.um"),
		TEXT("import \"host.um\"\nfn Twice*(Value: int): int { return host::UEmkaHostDouble(Value) }"), {}, Error));
	const FString Script = TEXT("import \"tests/wrapper.um\"\nfn Test*(Value: int): int { return wrapper::Twice(Value) }");
	int32 Line = -1;
	TestTrue(TEXT("Registered modules compile in preview without disk imports"), UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, Line, {}, TEXT("script.um"), false));
	FUEmkaCompiledSignature Signature;
	TestTrue(TEXT("Registered native declarations participate in signature inspection"), UUEmkaFunctionLibrary::InspectScriptFunction(Script, TEXT("Test"), Signature, {}, TEXT("script.um"), false));
	FUEmkaScriptParam Result;
	TestTrue(TEXT("Registered host callback executes"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 21)}, EUEmkaValueType::Int, false, false, Result, Error));
	TestEqual(TEXT("Native callback reads parameters and returns value"), Result.IntValue, int64(42));
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>();
	Asset->Source = Script;
	TestTrue(TEXT("Asset execution imports registered host modules"), UUEmkaFunctionLibrary::RunUmkaAsset(nullptr, Asset, Script, TEXT("Test"),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 7)}, EUEmkaValueType::Int, false, false, Result, Error));
	TestEqual(TEXT("Asset host call result"), Result.IntValue, int64(14));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaHostModuleValidationTest, "UEmka.HostFunctions.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaHostModuleValidationTest::RunTest(const FString& Parameters)
{
	FString Error;
	const TArray<UEmkaHostFunctions::FFunction> Functions = {{TEXT("UEmkaHostValidation"), &HostDouble}};
	if (!TestTrue(TEXT("Validation host module registers"), UEmkaHostFunctions::RegisterModule(TEXT("tests/validation.um"),
		TEXT("fn UEmkaHostValidation*(Value: int): int"), Functions, Error))) return false;
	ON_SCOPE_EXIT { UEmkaHostFunctions::UnregisterModule(TEXT("tests/validation.um"), Error); };
	TestFalse(TEXT("Duplicate module rejected"), UEmkaHostFunctions::RegisterModule(TEXT("tests/validation.um"), TEXT(""), {}, Error));
	TestTrue(TEXT("Duplicate module diagnostic"), Error.Contains(TEXT("already registered")));
	TestFalse(TEXT("Native names are unique across modules"), UEmkaHostFunctions::RegisterModule(TEXT("tests/conflict.um"), TEXT(""), Functions, Error));
	TestTrue(TEXT("Duplicate callback diagnostic names function"), Error.Contains(TEXT("UEmkaHostValidation")));
	const TArray<UEmkaHostFunctions::FFunction> Duplicate = {{TEXT("UEmkaHostDuplicate"), &HostDouble}, {TEXT("UEmkaHostDuplicate"), &HostDouble}};
	TestFalse(TEXT("Duplicate names in one registration rejected atomically"), UEmkaHostFunctions::RegisterModule(TEXT("tests/conflict.um"), TEXT(""), Duplicate, Error));
	TestFalse(TEXT("Failed registration adds no module"), UEmkaHostFunctions::UnregisterModule(TEXT("tests/conflict.um"), Error));
	for (const FString& Path : {TEXT("ue.um"), TEXT("std.um"), TEXT("fnc.um"), TEXT("mat.um"), TEXT("utf8.um"),
		TEXT("/absolute.um"), TEXT("../outside.um"), TEXT("lib/./host.um"), TEXT("lib//host.um"), TEXT("C:\\host.um"), TEXT("host.txt")})
	{
		TestFalse(TEXT("Invalid or reserved path rejected: ") + Path, UEmkaHostFunctions::RegisterModule(Path, TEXT(""), {}, Error));
		TestFalse(TEXT("Invalid path produces diagnostic"), Error.IsEmpty());
	}
	for (const UEmkaHostFunctions::FFunction& Function : {UEmkaHostFunctions::FFunction{TEXT("rtltime"), &HostDouble},
		UEmkaHostFunctions::FFunction{TEXT("InvalidName"), nullptr}, UEmkaHostFunctions::FFunction{TEXT("not-an-identifier"), &HostDouble}})
	{
		const TArray<UEmkaHostFunctions::FFunction> Invalid = {Function};
		TestFalse(TEXT("Invalid or reserved callback rejected"), UEmkaHostFunctions::RegisterModule(TEXT("tests/conflict.um"), TEXT(""), Invalid, Error));
	}
	const bool bWorkerRejected = Async(EAsyncExecution::Thread, []
	{
		FString WorkerError;
		return !UEmkaHostFunctions::RegisterModule(TEXT("tests/worker.um"), TEXT(""), {}, WorkerError)
			&& WorkerError.Contains(TEXT("game thread"));
	}).Get();
	TestTrue(TEXT("Registration is restricted to the game thread"), bWorkerRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaHostModuleLifetimeTest, "UEmka.HostFunctions.LifetimeAndSessionInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaHostModuleLifetimeTest::RunTest(const FString& Parameters)
{
	FString Error;
	const TArray<UEmkaHostFunctions::FFunction> Functions = {{TEXT("UEmkaHostMutation"), &HostMutation}};
	if (!TestTrue(TEXT("Mutation host module registers"), UEmkaHostFunctions::RegisterModule(TEXT("tests/mutation.um"),
		TEXT("fn UEmkaHostMutation*(): int"), Functions, Error))) return false;
	ON_SCOPE_EXIT { UEmkaHostFunctions::UnregisterModule(TEXT("tests/mutation.um"), Error); };
	FUEmkaExecutionOptions Options;
	Options.bUseSession = true;
	const FGuid SessionId = FGuid::NewGuid();
	UObject* Caller = NewObject<UUEmkaScriptAsset>();
	const FString Script = TEXT("import \"tests/mutation.um\"\nvar Count: int\nfn Test*(): int { Count++; return mutation::UEmkaHostMutation() * Count }");
	FUEmkaScriptParam Result;
	bMutationRejected = false;
	MutationError.Reset();
	TestTrue(TEXT("Native callback executes with session lease"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller,
		Script, TEXT("Test"), {}, EUEmkaValueType::Int, false, false, Result, Error, SessionId, Options));
	TestTrue(TEXT("Unregistration inside active callback is rejected"), bMutationRejected);
	TestTrue(TEXT("Active VM diagnostic"), MutationError.Contains(TEXT("VM is active")));
	TestEqual(TEXT("Callback remains valid until execution finishes"), Result.IntValue, int64(1));
	TestTrue(TEXT("Rejected registry mutation retains active session"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller,
		Script, TEXT("Test"), {}, EUEmkaValueType::Int, false, false, Result, Error, SessionId, Options));
	TestEqual(TEXT("Rejected registry mutation preserves session globals"), Result.IntValue, int64(2));
	const FString CachedScript = TEXT("import \"tests/mutation.um\"\nfn Test*(): int { return 9 }");
	TestTrue(TEXT("Imported host module is retained by cached session"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller,
		CachedScript, TEXT("Test"), {}, EUEmkaValueType::Int, false, false, Result, Error, SessionId, Options));
	TestTrue(TEXT("Module unregisters after execution and resets cached sessions"), UEmkaHostFunctions::UnregisterModule(TEXT("tests/mutation.um"), Error));
	AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Previous session cannot invoke removed callback"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller,
		CachedScript, TEXT("Test"), {}, EUEmkaValueType::Int, false, false, Result, Error, SessionId, Options));
	TestFalse(TEXT("Removed module call reports error"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaHostCallContextTest, "UEmka.HostFunctions.CallerWorldAndNestedContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaHostCallContextTest::RunTest(const FString& Parameters)
{
	FString Error;
	if (!TestTrue(TEXT("Context host module registers"), UEmkaHostFunctions::RegisterModule(TEXT("tests/context.um"), ContextModule, ContextFunctions, Error))) return false;
	ON_SCOPE_EXIT
	{
		UEmkaHostFunctions::UnregisterModule(TEXT("tests/context.um"), Error);
		ExpectedCaller.Reset();
		ExpectedWorld.Reset();
		ExpectedSessionId = {};
		HandleTarget.Reset();
	};
	TStrongObjectPtr<UWorld> World(NewObject<UWorld>());
	ExpectedCaller = World.Get();
	ExpectedWorld = World.Get();
	ExpectedSessionId = FGuid::NewGuid();
	HandleTarget = World.Get();
	FUEmkaExecutionOptions Options;
	Options.bUseSession = true;
	FUEmkaScriptParam Result;
	TestNull(TEXT("No context outside execution"), UEmkaHostFunctions::GetCurrentContext());
	TestTrue(TEXT("Caller, world and session are available in callback"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(World.Get(),
		TEXT("import \"tests/context.um\"\nfn Test*(): int { return context::UEmkaHostContext() * context::UEmkaHostNestedContext() }"),
		TEXT("Test"), {}, EUEmkaValueType::Int, false, false, Result, Error, ExpectedSessionId, Options));
	TestEqual(TEXT("Nested execution isolates handles and restores caller/world/session"), Result.IntValue, int64(1));
	TestNull(TEXT("Context clears after execution"), UEmkaHostFunctions::GetCurrentContext());
	TArray<FUEmkaScriptParam> Results;
	TestTrue(TEXT("Multi-return callbacks receive the same context"), UUEmkaFunctionLibrary::RunUmkaInlineMultiConfigured(World.Get(),
		TEXT("import \"tests/context.um\"\nfn Test*(): (int, int) { return context::UEmkaHostContext(), context::UEmkaHostNestedContext() }"),
		TEXT("Test"), {}, TEXT("0:0:8,0:0:8"), Results, Error, ExpectedSessionId, Options));
	if (TestEqual(TEXT("Multi-return result count"), Results.Num(), 2))
	{
		TestEqual(TEXT("Multi-return caller context"), Results[0].IntValue, int64(1));
		TestEqual(TEXT("Multi-return nested context"), Results[1].IntValue, int64(1));
	}
	TestNull(TEXT("Multi-return context clears after execution"), UEmkaHostFunctions::GetCurrentContext());
	AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Runtime failure returns normally from host context"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(World.Get(),
		TEXT("import \"tests/context.um\"\nfn Test*(Divisor: int): int { return context::UEmkaHostContext() / Divisor }"),
		TEXT("Test"), {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 0)},
		EUEmkaValueType::Int, false, false, Result, Error, ExpectedSessionId, Options));
	TestNull(TEXT("Failed execution clears caller context"), UEmkaHostFunctions::GetCurrentContext());
	const int32 PreviousFinalizerCount = FinalizerCount;
	TestTrue(TEXT("Cached VM retains a native finalizer"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(World.Get(),
		TEXT("import \"tests/context.um\"\nfn Test*(): int { return context::UEmkaHostAllocateFinalizer() }"),
		TEXT("Test"), {}, EUEmkaValueType::Int, false, false, Result, Error, ExpectedSessionId, Options));
	TestEqual(TEXT("Finalizer waits for cached VM teardown"), FinalizerCount, PreviousFinalizerCount);
	TestTrue(TEXT("Reset runs native finalizer"), UUEmkaFunctionLibrary::ResetRuntimeSession(World.Get(), ExpectedSessionId));
	TestTrue(TEXT("Idle VM teardown isolates context and disables handles"), FinalizerCount == PreviousFinalizerCount + 1 && bFinalizerContextSafe);
	TestNull(TEXT("Idle teardown leaves no context"), UEmkaHostFunctions::GetCurrentContext());
	TestTrue(TEXT("Worker threads have no UObject call context"), Async(EAsyncExecution::Thread, []
	{
		return UEmkaHostFunctions::GetCurrentContext() == nullptr;
	}).Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaHostObjectHandlesTest, "UEmka.HostFunctions.SafeObjectHandles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaHostObjectHandlesTest::RunTest(const FString& Parameters)
{
	FString Error;
	if (!TestTrue(TEXT("Handle host module registers"), UEmkaHostFunctions::RegisterModule(TEXT("tests/context.um"), ContextModule, ContextFunctions, Error))) return false;
	ON_SCOPE_EXIT
	{
		UEmkaHostFunctions::UnregisterModule(TEXT("tests/context.um"), Error);
		HandleTarget.Reset();
	};
	TStrongObjectPtr<UUEmkaScriptAsset> Caller(NewObject<UUEmkaScriptAsset>());
	TStrongObjectPtr<UUEmkaScriptAsset> OtherCaller(NewObject<UUEmkaScriptAsset>());
	HandleTarget = Caller.Get();
	const FGuid SessionId = FGuid::NewGuid();
	FUEmkaExecutionOptions Options;
	Options.bUseSession = true;
	FString Script = TEXT("import \"tests/context.um\"\nfn Test*(Create: bool, Handle: uint): uint { if Create { return context::UEmkaHostCreateHandle() }; return uint(context::UEmkaHostResolveHandle(Handle)) }");
	FUEmkaScriptParam Result;
	const auto Run = [&](UObject* Owner, const FGuid& Id, const bool bCreate, const uint64 Handle)
	{
		return UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Owner, Script, TEXT("Test"),
			{UUEmkaFunctionLibrary::MakeBoolParam(bCreate), UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::UInt, static_cast<int64>(Handle))},
			EUEmkaValueType::UInt, false, false, Result, Error, Id, Options);
	};
	TestTrue(TEXT("Create object handle"), Run(Caller.Get(), SessionId, true, 0));
	const uint64 Handle = static_cast<uint64>(Result.IntValue);
	TestTrue(TEXT("Handle is nonzero"), Handle != 0);
	TestTrue(TEXT("Same object reuses its VM handle"), Run(Caller.Get(), SessionId, true, 0));
	TestEqual(TEXT("Stable handle within a retained session"), static_cast<uint64>(Result.IntValue), Handle);
	TestTrue(TEXT("Resolve handle in its retained session"), Run(Caller.Get(), SessionId, false, Handle));
	TestEqual(TEXT("Original handle resolves"), Result.IntValue, int64(1));
	for (const uint64 Invalid : {uint64(0), MAX_uint64})
	{
		TestTrue(TEXT("Invalid token returns without dereferencing a pointer"), Run(Caller.Get(), SessionId, false, Invalid));
		TestEqual(TEXT("Zero and forged handles are rejected"), Result.IntValue, int64(0));
	}
	TestTrue(TEXT("Foreign session receives token"), Run(Caller.Get(), FGuid::NewGuid(), false, Handle));
	TestEqual(TEXT("Different session rejects handle"), Result.IntValue, int64(0));
	TestTrue(TEXT("Foreign caller receives token"), Run(OtherCaller.Get(), SessionId, false, Handle));
	TestEqual(TEXT("Different caller rejects handle"), Result.IntValue, int64(0));
	TestTrue(TEXT("Reset original session"), UUEmkaFunctionLibrary::ResetRuntimeSession(Caller.Get(), SessionId));
	TestTrue(TEXT("Reset session receives old token"), Run(Caller.Get(), SessionId, false, Handle));
	TestEqual(TEXT("Reset invalidates handle"), Result.IntValue, int64(0));
	TestTrue(TEXT("New VM creates fresh handle"), Run(Caller.Get(), SessionId, true, 0));
	const uint64 NewHandle = static_cast<uint64>(Result.IntValue);
	TestTrue(TEXT("Tokens are never recycled after reset"), NewHandle != 0 && NewHandle != Handle);
	Script += TEXT("\n// Updated source");
	TestTrue(TEXT("Recompiled session receives previous token"), Run(Caller.Get(), SessionId, false, NewHandle));
	TestEqual(TEXT("Recompilation invalidates handle"), Result.IntValue, int64(0));
	HandleTarget = NewObject<UUEmkaScriptAsset>();
	TestTrue(TEXT("Handle to an unrooted object is created"), Run(Caller.Get(), SessionId, true, 0));
	const uint64 CollectedHandle = static_cast<uint64>(Result.IntValue);
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Handle does not keep UObject alive"), HandleTarget.IsValid());
	TestTrue(TEXT("Collected object token returns without dereferencing freed memory"), Run(Caller.Get(), SessionId, false, CollectedHandle));
	TestEqual(TEXT("Collected UObject handle is rejected"), Result.IntValue, int64(0));
	TestTrue(TEXT("Null object produces no handle"), Run(Caller.Get(), SessionId, true, 0));
	TestEqual(TEXT("Null object has zero handle"), Result.IntValue, int64(0));
	HandleTarget = Caller.Get();
	Options.bUseSession = false;
	TestTrue(TEXT("One-shot execution creates token"), Run(Caller.Get(), SessionId, true, 0));
	const uint64 OneShotHandle = static_cast<uint64>(Result.IntValue);
	TestTrue(TEXT("Following one-shot execution receives token"), Run(Caller.Get(), SessionId, false, OneShotHandle));
	TestEqual(TEXT("One-shot tokens expire after execution"), Result.IntValue, int64(0));
	return true;
}

#endif
