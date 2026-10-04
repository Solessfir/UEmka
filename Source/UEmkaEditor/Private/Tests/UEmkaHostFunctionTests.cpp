// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaHostFunctions.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UEmkaFunctionLibrary.h"

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

#endif
