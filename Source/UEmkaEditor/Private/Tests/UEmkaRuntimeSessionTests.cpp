// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaFunctionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UEmkaHostFunctions.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
FUEmkaExecutionOptions SessionOptions()
{
	FUEmkaExecutionOptions Options;
	Options.bUseSession = true;
	Options.MaxInstructions = 100000;
	return Options;
}

const FString Counter = TEXT("var Count: int\nfn Tick*(): int { Count++; return Count }\nfn tick*(): int { Count++; return Count }");

bool Tick(FAutomationTestBase& Test, UObject* Caller, const FGuid& Id, const FString& Script, const int64 Expected,
	const FString& Function = TEXT("Tick"), const FUEmkaExecutionOptions& Options = SessionOptions())
{
	FUEmkaScriptParam Result;
	FString Error;
	if (!Test.TestTrue(TEXT("Configured counter executes"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller, Script, Function, {}, EUEmkaValueType::Int, false, false, Result, Error, Id, Options)))
	{
		Test.AddError(Error);
		return false;
	}
	return Test.TestEqual(TEXT("Configured counter preserves expected state"), Result.IntValue, Expected);
}

FEvent* CancellationReady = nullptr;
void SessionCancellationReady(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	CancellationReady->Trigger();
	umkaGetResult(Params, Result)->intVal = 1;
}

struct FResetFixture
{
	UObject* Caller = nullptr;
	FGuid SessionId;
	int32 Mode = 0;
	bool bRequestReset = true;
	bool bAccepted = false;
	int32 ResetCount = 0;
};

FResetFixture* ActiveResetFixture = nullptr;

void ResetActiveSession(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	FResetFixture& Fixture = *ActiveResetFixture;
	if (Fixture.bRequestReset)
	{
		if (Fixture.Mode == 0) Fixture.bAccepted = UUEmkaFunctionLibrary::ResetRuntimeSession(Fixture.Caller, Fixture.SessionId);
		else if (Fixture.Mode == 1)
		{
			Fixture.ResetCount = UUEmkaFunctionLibrary::ResetRuntimeSessionsForCaller(Fixture.Caller);
			Fixture.bAccepted = Fixture.ResetCount > 0;
		}
		else
		{
			UUEmkaFunctionLibrary::ResetAllRuntimeSessions();
			Fixture.bAccepted = true;
		}
	}
	umkaGetResult(Params, Result)->intVal = 1;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionPersistenceTest, "UEmka.Runtime.Sessions.PersistenceIsolationAndReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionPersistenceTest::RunTest(const FString& Parameters)
{
	UUEmkaFunctionLibrary::ResetAllRuntimeSessions();
	ON_SCOPE_EXIT { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); };
	TStrongObjectPtr<UObject> First(NewObject<UUEmkaScriptAsset>());
	TStrongObjectPtr<UObject> Second(NewObject<UUEmkaScriptAsset>());
	const FGuid Id = FGuid::NewGuid();
	const FGuid OtherId = FGuid::NewGuid();
	Tick(*this, First.Get(), Id, Counter, 1);
	Tick(*this, First.Get(), Id, Counter, 2);
	Tick(*this, Second.Get(), Id, Counter, 1);
	Tick(*this, First.Get(), OtherId, Counter, 1);
	Tick(*this, First.Get(), Id, Counter, 3);
	TestTrue(TEXT("Idle session resets"), UUEmkaFunctionLibrary::ResetRuntimeSession(First.Get(), Id));
	TestFalse(TEXT("Missing session reset reports false"), UUEmkaFunctionLibrary::ResetRuntimeSession(First.Get(), Id));
	Tick(*this, First.Get(), Id, Counter, 1);
	Tick(*this, Second.Get(), Id, Counter, 2);
	TestEqual(TEXT("Caller reset discards both of its nodes"), UUEmkaFunctionLibrary::ResetRuntimeSessionsForCaller(First.Get()), 2);
	TestEqual(TEXT("Missing caller reset reports zero"), UUEmkaFunctionLibrary::ResetRuntimeSessionsForCaller(First.Get()), 0);
	Tick(*this, First.Get(), Id, Counter, 1);
	Tick(*this, First.Get(), OtherId, Counter, 1);
	Tick(*this, Second.Get(), Id, Counter, 3);
	FUEmkaExecutionOptions Reset = SessionOptions();
	Reset.bResetSession = true;
	Tick(*this, First.Get(), Id, Counter, 1, TEXT("Tick"), Reset);
	Tick(*this, First.Get(), Id, Counter, 1, TEXT("Tick"), Reset);
	Tick(*this, First.Get(), Id, Counter, 2);
	Tick(*this, First.Get(), OtherId, Counter, 2);
	Tick(*this, Second.Get(), Id, Counter, 4);
	UUEmkaFunctionLibrary::ResetAllRuntimeSessions();
	Tick(*this, First.Get(), Id, Counter, 1);
	Tick(*this, Second.Get(), Id, Counter, 1);
	const FUEmkaExecutionOptions Fresh;
	Tick(*this, First.Get(), OtherId, Counter, 1, TEXT("Tick"), Fresh);
	Tick(*this, First.Get(), OtherId, Counter, 1, TEXT("Tick"), Fresh);
	FUEmkaScriptParam Result;
	FString Error;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TestTrue(TEXT("Default runner remains fresh"), UUEmkaFunctionLibrary::RunUmkaInline(First.Get(), Counter, TEXT("Tick"), {}, EUEmkaValueType::Int, false, false, Result, Error));
		TestEqual(TEXT("Default runner starts globals fresh"), Result.IntValue, int64{1});
	}
	TestFalse(TEXT("Idle session cannot be cancelled"), UUEmkaFunctionLibrary::CancelExecution(First.Get(), Id));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionInvalidationTest, "UEmka.Runtime.Sessions.SourceFunctionAndModuleInvalidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionInvalidationTest::RunTest(const FString& Parameters)
{
	ON_SCOPE_EXIT { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); };
	TStrongObjectPtr<UObject> Caller(NewObject<UUEmkaScriptAsset>());
	const FGuid Id = FGuid::NewGuid();
	Tick(*this, Caller.Get(), Id, Counter, 1);
	Tick(*this, Caller.Get(), Id, Counter, 2);
	Tick(*this, Caller.Get(), Id, Counter, 1, TEXT("tick"));
	Tick(*this, Caller.Get(), Id, Counter, 2, TEXT("tick"));
	Tick(*this, Caller.Get(), Id, Counter + TEXT("\n// Changed"), 1, TEXT("tick"));
	Tick(*this, Caller.Get(), Id, Counter + TEXT("\n// changed"), 1, TEXT("tick"));
	FUEmkaScriptParam Result;
	FString Error;
	const FString ChangedSignature = TEXT("var Count: int\nfn Tick*(Add: int): int { Count += Add; return Count }");
	TestTrue(TEXT("Changed signature replaces session"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller.Get(), ChangedSignature, TEXT("Tick"),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 4)}, EUEmkaValueType::Int, false, false, Result, Error, Id, SessionOptions()));
	TestEqual(TEXT("New signature starts globals fresh"), Result.IntValue, int64{4});
	TStrongObjectPtr<UUEmkaScriptAsset> Dependency(NewObject<UUEmkaScriptAsset>());
	Dependency->ModulePath = TEXT("library.um");
	Dependency->Source = TEXT("const Amount* = 3");
	TStrongObjectPtr<UUEmkaScriptAsset> Asset(NewObject<UUEmkaScriptAsset>());
	Asset->ModulePath = TEXT("main.um");
	Asset->Imports.Add(Dependency.Get());
	Asset->Source = TEXT("import lib = \"library.um\"\nvar Count: int\nfn Tick*(): int { Count += lib::Amount; return Count }");
	const auto AssetCall = [this, &Caller, &Asset, &Id, &Result, &Error](const int64 Expected)
	{
		TestTrue(TEXT("Session asset executes with registered dependencies"), UUEmkaFunctionLibrary::RunUmkaAssetConfigured(Caller.Get(), Asset.Get(), Asset->Source, TEXT("Tick"),
			{}, EUEmkaValueType::Int, false, false, Result, Error, Id, SessionOptions()));
		TestEqual(TEXT("Asset session has expected global state"), Result.IntValue, Expected);
	};
	AssetCall(3);
	AssetCall(6);
	Dependency->Source = TEXT("const Amount* = 5");
	AssetCall(5);
	AssetCall(10);
	Asset->ModulePath = TEXT("changed-main.um");
	AssetCall(5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionManagedValuesTest, "UEmka.Runtime.Sessions.ManagedParametersResultsAndFailures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionManagedValuesTest::RunTest(const FString& Parameters)
{
	ON_SCOPE_EXIT { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); };
	TStrongObjectPtr<UObject> Caller(NewObject<UUEmkaScriptAsset>());
	const FGuid Id = FGuid::NewGuid();
	const FString Script = TEXT("type Row = struct { Number: int; Label: str }\nvar Count: int\n")
		TEXT("fn Managed*(Values: []str, Label: str = \"default\", Item: Row = Row{7, \"row\"}, Last: int8 = 0): (str, []str, Row, int) { Count++; return Label + \" copied\", Values, Item, Count }");
	FUEmkaCompiledSignature Signature;
	if (!TestTrue(TEXT("Managed session defaults inspect"), UUEmkaFunctionLibrary::InspectScriptFunction(Script, TEXT("Managed"), Signature))) return false;
	FUEmkaScriptParam Record;
	Record.Type = EUEmkaValueType::Composite;
	Record.CompositeValue = Signature.Params[2].DefaultCompositeValue;
	if (!TestFalse(TEXT("Compiler record default contains owned bytes"), Record.CompositeValue.IsEmpty())) return false;
	TArray<FUEmkaScriptParam> Params = {
		UUEmkaFunctionLibrary::MakeStrArrayParam({TEXT("first"), TEXT("second")}, false),
		UUEmkaFunctionLibrary::MakeStrParam(Signature.Params[1].DefaultValue), Record,
		UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int8, 0)
	};
	FString Error;
	TArray<FUEmkaScriptParam> Results;
	TArray<FUEmkaScriptParam> Saved;
	const FString Types = TEXT("12:0:8,12:1:8,15:0:8,0:0:8");
	for (int32 Iteration = 1; Iteration <= 16; ++Iteration)
	{
		if (!TestTrue(TEXT("Managed session tuple executes repeatedly"), UUEmkaFunctionLibrary::RunUmkaInlineMultiConfigured(Caller.Get(), Script, TEXT("Managed"), Params, Types, Results, Error, Id, SessionOptions()))) return false;
		if (!TestEqual(TEXT("Managed session tuple count"), Results.Num(), 4)) return false;
		TestEqual(TEXT("Managed string copied from compiler default"), Results[0].StringValue, FString(TEXT("default copied")));
		TestTrue(TEXT("Managed string array survives transfer"), Results[1].StringArrayValue == Params[0].StringArrayValue);
		TestTrue(TEXT("Managed record retains compiler default and owned string"), Results[2].CompositeValue == Record.CompositeValue);
		TestEqual(TEXT("Managed call retains only global state"), Results[3].IntValue, static_cast<int64>(Iteration));
		if (Iteration == 1) Saved = Results;
	}
	TArray<FUEmkaScriptParam> Bad = Params;
	Bad[3] = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int8, 128);
	AddExpectedError(TEXT("] Managed:"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Failed push after managed parameters is rejected"), UUEmkaFunctionLibrary::RunUmkaInlineMultiConfigured(Caller.Get(), Script, TEXT("Managed"), Bad, Types, Results, Error, Id, SessionOptions()));
	TestTrue(TEXT("Failed managed call clears results"), Results.IsEmpty());
	TestFalse(TEXT("Wrong result metadata after managed pushes is rejected"), UUEmkaFunctionLibrary::RunUmkaInlineMultiConfigured(Caller.Get(), Script, TEXT("Managed"), Params, TEXT("0:0:8,12:1:8,15:0:8,0:0:8"), Results, Error, Id, SessionOptions()));
	TestTrue(TEXT("Session recovers after push and metadata failures"), UUEmkaFunctionLibrary::RunUmkaInlineMultiConfigured(Caller.Get(), Script, TEXT("Managed"), Params, Types, Results, Error, Id, SessionOptions()));
	if (Results.Num() == 4) TestEqual(TEXT("Rejected calls leave globals untouched"), Results[3].IntValue, int64{17});
	UUEmkaFunctionLibrary::ResetRuntimeSession(Caller.Get(), Id);
	TestEqual(TEXT("Earlier managed string survives session destruction"), Saved[0].StringValue, FString(TEXT("default copied")));
	TestTrue(TEXT("Earlier managed array survives session destruction"), Saved[1].StringArrayValue == Params[0].StringArrayValue);
	TestTrue(TEXT("Earlier managed record survives session destruction"), Saved[2].CompositeValue == Record.CompositeValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionFatalRecoveryTest, "UEmka.Runtime.Sessions.FatalRecoveryAndInstructionBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionFatalRecoveryTest::RunTest(const FString& Parameters)
{
	ON_SCOPE_EXIT { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); };
	TStrongObjectPtr<UObject> Caller(NewObject<UUEmkaScriptAsset>());
	const FGuid Id = FGuid::NewGuid();
	const FString Script = TEXT("var Count: int\nfn Recover*(Fail: bool): int { Count++; if Fail { Zero := 0; return 1 / Zero }; return Count }");
	FUEmkaScriptParam Result;
	FString Error;
	const auto Run = [&](const bool bFail)
	{
		return UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller.Get(), Script, TEXT("Recover"), {UUEmkaFunctionLibrary::MakeBoolParam(bFail)}, EUEmkaValueType::Int, false, false, Result, Error, Id, SessionOptions());
	};
	TestTrue(TEXT("Stateful function first succeeds"), Run(false));
	TestTrue(TEXT("Stateful function second succeeds"), Run(false));
	TestEqual(TEXT("Globals persist before fatal error"), Result.IntValue, int64{2});
	AddExpectedError(TEXT("] Recover:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Runtime fatal call fails"), Run(true));
	TestEqual(TEXT("Fatal result is reset"), Result.Type, EUEmkaValueType::Int);
	TestEqual(TEXT("Fatal result value is cleared"), Result.IntValue, int64{0});
	TestTrue(TEXT("Next call recompiles dead session"), Run(false));
	TestEqual(TEXT("Dead session globals restart"), Result.IntValue, int64{1});
	FUEmkaExecutionOptions Budget = SessionOptions();
	Budget.MaxInstructions = 1000;
	AddExpectedError(TEXT("] Loop:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Infinite loop stops at instruction budget"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller.Get(), TEXT("fn Loop*(): int { for true {} ; return 1 }"), TEXT("Loop"), {}, EUEmkaValueType::Int, false, false, Result, Error, Id, Budget));
	TestTrue(TEXT("Budget diagnostic remains visible"), Error.Contains(TEXT("Instruction budget exceeded")));
	Tick(*this, Caller.Get(), Id, Counter, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionRuntimeStackTraceTest, "UEmka.Runtime.Sessions.RuntimeStackTrace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionRuntimeStackTraceTest::RunTest(const FString& Parameters)
{
	ON_SCOPE_EXIT { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); };
	TStrongObjectPtr<UObject> Caller(NewObject<UUEmkaScriptAsset>());
	const FString Script = TEXT("import \"std.um\"\n")
		TEXT("fn leaf() { std::assert(false, \"trace fixture\") }\n")
		TEXT("fn middle() { leaf() }\n")
		TEXT("fn Test*(): int { middle(); return 0 }");
	FUEmkaScriptParam Result;
	FString Error;
	AddExpectedError(TEXT("] Test:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Nested assertion fails"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller.Get(), Script, TEXT("Test"), {}, EUEmkaValueType::Int, false, false, Result, Error, FGuid::NewGuid(), SessionOptions()));
	TestTrue(TEXT("Runtime trace contains leaf frame"), Error.Contains(TEXT("leaf")));
	TestTrue(TEXT("Runtime trace contains middle frame"), Error.Contains(TEXT("middle")));
	TestTrue(TEXT("Runtime trace contains exported entry frame"), Error.Contains(TEXT("Test")));
	TestTrue(TEXT("Runtime trace contains source line"), Error.Contains(TEXT("script.um:2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionCancellationTest, "UEmka.Runtime.Sessions.CrossThreadCancellation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionCancellationTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UObject> Caller(NewObject<UUEmkaScriptAsset>());
	const FGuid Id = FGuid::NewGuid();
	FString Error;
	const TArray<UEmkaHostFunctions::FFunction> Functions = {{TEXT("UEmkaSessionCancellationReady"), &SessionCancellationReady}};
	if (!TestTrue(TEXT("Cancellation ready callback registers"), UEmkaHostFunctions::RegisterModule(TEXT("tests/session-cancel.um"), TEXT("fn UEmkaSessionCancellationReady*(): int"), Functions, Error))) return false;
	CancellationReady = FPlatformProcess::GetSynchEventFromPool(true);
	ON_SCOPE_EXIT
	{
		UUEmkaFunctionLibrary::ResetAllRuntimeSessions();
		UEmkaHostFunctions::UnregisterModule(TEXT("tests/session-cancel.um"), Error);
		FPlatformProcess::ReturnSynchEventToPool(CancellationReady);
		CancellationReady = nullptr;
	};
	TFuture<bool> Cancelled = Async(EAsyncExecution::Thread, [Owner = Caller.Get(), Id]()
	{
		return CancellationReady->Wait(5000) && UUEmkaFunctionLibrary::CancelExecution(Owner, Id);
	});
	FUEmkaExecutionOptions Options = SessionOptions();
	Options.MaxInstructions = 100000000;
	FUEmkaScriptParam Result;
	AddExpectedError(TEXT("] Cancel:"), EAutomationExpectedErrorFlags::Contains, 1);
	const bool bSucceeded = UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller.Get(),
		TEXT("import ready = \"tests/session-cancel.um\"\nfn Cancel*(): int { ready::UEmkaSessionCancellationReady(); for true {}; return 1 }"), TEXT("Cancel"),
		{}, EUEmkaValueType::Int, false, false, Result, Error, Id, Options);
	TestTrue(TEXT("Worker signals active session cancellation"), Cancelled.Get());
	TestFalse(TEXT("Cancelled infinite loop returns failure"), bSucceeded);
	TestTrue(TEXT("Cancellation diagnostic is preserved"), Error.Contains(TEXT("Execution cancelled")));
	TestEqual(TEXT("Cancellation clears result"), Result.IntValue, int64{0});
	Tick(*this, Caller.Get(), Id, Counter, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionActiveResetTest, "UEmka.Runtime.Sessions.DeferredActiveReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionActiveResetTest::RunTest(const FString& Parameters)
{
	FString Error;
	const TArray<UEmkaHostFunctions::FFunction> Functions = {{TEXT("UEmkaResetActive"), &ResetActiveSession}};
	if (!TestTrue(TEXT("Active reset callback registers"), UEmkaHostFunctions::RegisterModule(TEXT("tests/session-reset.um"), TEXT("fn UEmkaResetActive*(): int"), Functions, Error))) return false;
	ON_SCOPE_EXIT
	{
		ActiveResetFixture = nullptr;
		UUEmkaFunctionLibrary::ResetAllRuntimeSessions();
		UEmkaHostFunctions::UnregisterModule(TEXT("tests/session-reset.um"), Error);
	};
	const FString Script = TEXT("import reset = \"tests/session-reset.um\"\nvar Count: int\n")
		TEXT("fn Tick*(): int { Count++; reset::UEmkaResetActive(); n := 0; for n < 10000 { n++ }; return Count }");
	FUEmkaExecutionOptions Options = SessionOptions();
	Options.MaxInstructions = 1000000;
	AddExpectedError(TEXT("] Tick:"), EAutomationExpectedErrorFlags::Contains, 3);
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		UUEmkaFunctionLibrary::ResetAllRuntimeSessions();
		TStrongObjectPtr<UObject> First(NewObject<UUEmkaScriptAsset>());
		TStrongObjectPtr<UObject> Second(NewObject<UUEmkaScriptAsset>());
		FResetFixture Fixture;
		Fixture.Caller = First.Get();
		Fixture.SessionId = FGuid::NewGuid();
		Fixture.Mode = Mode;
		ActiveResetFixture = &Fixture;
		const FGuid OtherId = FGuid::NewGuid();
		Tick(*this, First.Get(), OtherId, Counter, 1);
		Tick(*this, Second.Get(), Fixture.SessionId, Counter, 1);
		FUEmkaScriptParam Result;
		TestFalse(TEXT("Reset from an active native callback cancels after it returns"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(First.Get(), Script,
			TEXT("Tick"), {}, EUEmkaValueType::Int, false, false, Result, Error, Fixture.SessionId, Options));
		TestTrue(TEXT("Busy reset is accepted without destroying the active VM"), Fixture.bAccepted);
		TestTrue(TEXT("Busy reset preserves cancellation diagnostic"), Error.Contains(TEXT("Execution cancelled")));
		TestEqual(TEXT("Busy reset clears the failed result"), Result.IntValue, int64{0});
		if (Mode == 1) TestEqual(TEXT("Caller reset includes both active and idle nodes"), Fixture.ResetCount, 2);
		Fixture.bRequestReset = false;
		Tick(*this, First.Get(), Fixture.SessionId, Script, 1, TEXT("Tick"), Options);
		Tick(*this, First.Get(), Fixture.SessionId, Script, 2, TEXT("Tick"), Options);
		Tick(*this, First.Get(), OtherId, Counter, Mode == 0 ? 2 : 1);
		Tick(*this, Second.Get(), Fixture.SessionId, Counter, Mode == 2 ? 1 : 2);
		ActiveResetFixture = nullptr;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionWeakOwnerTest, "UEmka.Runtime.Sessions.WeakOwnerAndImportIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionWeakOwnerTest::RunTest(const FString& Parameters)
{
	ON_SCOPE_EXIT { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); };
	const FGuid Id = FGuid::NewGuid();
	TWeakObjectPtr<UObject> Weak = NewObject<UUEmkaScriptAsset>();
	Tick(*this, Weak.Get(), Id, Counter, 1);
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Cached VM does not retain its caller"), Weak.IsValid());
	TStrongObjectPtr<UObject> Caller(NewObject<UUEmkaScriptAsset>());
	Tick(*this, Caller.Get(), Id, Counter, 1);
	TestTrue(TEXT("Surviving owner session remains resettable after stale cleanup"), UUEmkaFunctionLibrary::ResetRuntimeSession(Caller.Get(), Id));
	const FString Filename = FPaths::Combine(FPaths::ProjectSavedDir(), FString::Printf(TEXT("UEmkaSessionDisk_%s.um"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	if (!TestTrue(TEXT("Disk-only import fixture exists"), FFileHelper::SaveStringToFile(TEXT("const Value* = 9"), *Filename))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Filename); };
	const FString Script = FString::Printf(TEXT("import disk = \"%s\"\nfn Disk*(): int { return disk::Value }"), *Filename);
	FUEmkaScriptParam Result;
	FString Error;
	AddExpectedError(TEXT("] Disk:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Sessions reject filesystem-only imports"), UUEmkaFunctionLibrary::RunUmkaInlineConfigured(Caller.Get(), Script, TEXT("Disk"), {}, EUEmkaValueType::Int, false, false, Result, Error, Id, SessionOptions()));
	Tick(*this, Caller.Get(), Id, Counter, 1);
	return true;
}

#endif
