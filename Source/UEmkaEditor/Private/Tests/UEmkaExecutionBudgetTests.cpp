// Copyright Solessfir 2026. All Rights Reserved.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "umka_api.h"

namespace
{
Umka* CompileBudgetFixture(FAutomationTestBase& Test, const char* Source, UmkaExternFunc Host = nullptr)
{
	Umka* Vm = umkaAlloc();
	if (!Test.TestNotNull(TEXT("Native VM allocated"), Vm)) return nullptr;
	if (!Test.TestTrue(TEXT("Native VM initialized"), umkaInit(Vm, "execution-budget.um", Source, 100000, nullptr, 0, nullptr, false, false, nullptr))
		|| (Host && !Test.TestTrue(TEXT("Native callback registered"), umkaAddFunc(Vm, "Host", Host)))
		|| !Test.TestTrue(TEXT("Execution budget fixture compiles"), umkaCompile(Vm)))
	{
		Test.AddError(UTF8_TO_TCHAR(umkaGetError(Vm)->msg));
		umkaFree(Vm);
		return nullptr;
	}
	return Vm;
}

bool CancelImmediately(void* UserData)
{
	++*static_cast<int32*>(UserData);
	return true;
}

bool CancelAfterThreePolls(void* UserData)
{
	return ++*static_cast<int32*>(UserData) == 3;
}

struct FNativeBudgetCallbackState
{
	int32 Calls = 0;
	bool bRecursive = false;
	bool bNestedCallFailed = false;
};

void NestedBudgetCallback(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	Umka* Vm = static_cast<Umka*>(Result->ptrVal);
	auto& State = *static_cast<FNativeBudgetCallbackState*>(umkaGetMetadata(Vm));
	UmkaFuncContext Context = {};
	if (!umkaGetFunc(Vm, nullptr, State.bRecursive ? "Outer" : "Inner", &Context))
	{
		State.bNestedCallFailed = true;
		return;
	}
	for (int32 Index = 0; Index < (State.bRecursive ? 1 : 100); ++Index)
	{
		++State.Calls;
		if (umkaCall(Vm, &Context) != 0) State.bNestedCallFailed = true;
	}
	Result->intVal = 12;
}

void CountNativeDeallocation(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	++**static_cast<int32**>(umkaGetParam(Params, 0)->ptrVal);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeExecutionBudgetTest, "UEmka.Runtime.NativeExecutionBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeExecutionBudgetTest::RunTest(const FString& Parameters)
{
	Umka* Vm = CompileBudgetFixture(*this,
		"fn Count*(N: int): int { Total := 0; for I := 0; I < N; I++ { Total += I }; return Total }\nfn main() {}\n");
	if (!Vm) return false;
	UmkaFuncContext Context = {};
	if (TestTrue(TEXT("Finite function resolved"), umkaGetFunc(Vm, nullptr, "Count", &Context)))
	{
		umkaGetParam(Context.params, 0)->intVal = 10000;
		TestEqual(TEXT("Instruction limit is disabled by default"), umkaCall(Vm, &Context), 0);
		umkaSetExecutionBudget(Vm, 1000000, nullptr, nullptr);
		for (int32 Index = 0; Index < 20; ++Index)
		{
			if (!TestEqual(TEXT("Each top-level call gets its full budget"), umkaCall(Vm, &Context), 0)) break;
			TestEqual(TEXT("Large finite loop result"), static_cast<int64>(Context.result->intVal), int64{49995000});
		}
		TestFalse(TEXT("A completed call has no active native call stack"), umkaGetCallStack(Vm, 0, 0, nullptr, nullptr, nullptr, nullptr));
	}
	int32 Polls = 0;
	int32 Deallocations = 0;
	int32** DeallocationCounter = static_cast<int32**>(umkaAllocData(Vm, sizeof(int32*), CountNativeDeallocation));
	*DeallocationCounter = &Deallocations;
	umkaSetExecutionBudget(Vm, 1, CancelImmediately, &Polls);
	umkaFree(Vm);
	TestEqual(TEXT("Free disables cancellation before cleanup"), Polls, 0);
	TestEqual(TEXT("Free still runs native deallocators"), Deallocations, 1);

	for (const bool bRun : {false, true})
	{
		Vm = CompileBudgetFixture(*this, "fn Loop*() { for true {} }\nfn main() { Loop() }\n");
		if (!Vm) return false;
		umkaGetFunc(Vm, nullptr, "Loop", &Context);
		umkaSetExecutionBudget(Vm, 1000, nullptr, nullptr);
		TestTrue(TEXT("Loop without calls exceeds the instruction budget"), (bRun ? umkaRun(Vm) : umkaCall(Vm, &Context)) != 0);
		const UmkaError* Error = umkaGetError(Vm);
		TestEqual(TEXT("Budget failure uses the normal runtime report"), FString(UTF8_TO_TCHAR(Error->msg)), FString(TEXT("Instruction budget exceeded")));
		TestTrue(TEXT("Budget failure retains source location"), Error->line > 0 && FString(UTF8_TO_TCHAR(Error->fileName)).EndsWith(TEXT("execution-budget.um")));
		TestFalse(TEXT("Budget failure leaves the VM dead"), umkaAlive(Vm));
		TestTrue(TEXT("A later call safely reports the dead VM"), umkaCall(Vm, &Context) != 0);
		TestTrue(TEXT("A later call retains a valid error target"), FString(UTF8_TO_TCHAR(umkaGetError(Vm)->msg)).Contains(TEXT("dead fiber")));
		umkaFree(Vm);
	}

	Vm = CompileBudgetFixture(*this, "fn Child() { for true {} }\nfn Fib*() { F := make(fiber, Child); resume(F) }\nfn main() {}\n");
	if (!Vm) return false;
	umkaGetFunc(Vm, nullptr, "Fib", &Context);
	umkaSetExecutionBudget(Vm, 1000, nullptr, nullptr);
	TestTrue(TEXT("Child fibers share the VM instruction budget"), umkaCall(Vm, &Context) != 0);
	TestEqual(TEXT("Child fiber failure reports its source function"), FString(UTF8_TO_TCHAR(umkaGetError(Vm)->fnName)), FString(TEXT("Child")));
	umkaFree(Vm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeExecutionCancellationTest, "UEmka.Runtime.NativeExecutionCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeExecutionCancellationTest::RunTest(const FString& Parameters)
{
	for (const bool bImmediate : {false, true})
	{
		Umka* Vm = CompileBudgetFixture(*this, bImmediate
			? "fn Loop*() { for true {} }\nfn main() {}\n"
			: "fn Child() { for true {} }\nfn Loop*() { F := make(fiber, Child); resume(F) }\nfn main() {}\n");
		if (!Vm) return false;
		UmkaFuncContext Context = {};
		umkaGetFunc(Vm, nullptr, "Loop", &Context);
		int32 Polls = 0;
		umkaSetExecutionBudget(Vm, 0, bImmediate ? CancelImmediately : CancelAfterThreePolls, &Polls);
		TestTrue(TEXT("Cancellation stops a loop without function calls"), umkaCall(Vm, &Context) != 0);
		TestEqual(TEXT("Cancellation callback is polled at the first instruction and periodically"), Polls, bImmediate ? 1 : 3);
		TestEqual(TEXT("Cancellation failure uses the normal runtime report"), FString(UTF8_TO_TCHAR(umkaGetError(Vm)->msg)), FString(TEXT("Execution cancelled")));
		if (!bImmediate) TestEqual(TEXT("Cancellation follows child fibers"), FString(UTF8_TO_TCHAR(umkaGetError(Vm)->fnName)), FString(TEXT("Child")));
		umkaFree(Vm);
	}

	Umka* Vm = umkaAlloc();
	if (!TestNotNull(TEXT("Uncompiled VM allocated"), Vm)) return false;
	TestTrue(TEXT("Uncompiled VM initialized"), umkaInit(Vm, "uncompiled.um", "fn main() {}", 10000, nullptr, 0, nullptr, false, false, nullptr));
	int32 Polls = 0;
	umkaSetExecutionBudget(Vm, 1, CancelImmediately, &Polls);
	umkaFree(Vm);
	TestEqual(TEXT("Free before compilation does not execute cancellation callbacks"), Polls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeNestedExecutionBudgetTest, "UEmka.Runtime.NativeNestedExecutionBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeNestedExecutionBudgetTest::RunTest(const FString& Parameters)
{
	for (const bool bRecursive : {false, true})
	{
		Umka* Vm = CompileBudgetFixture(*this,
			"fn Host(): int;\nfn Inner*(): int { return 1 }\nfn Outer*(): int { return Host() }\nfn main() {}\n", NestedBudgetCallback);
		if (!Vm) return false;
		FNativeBudgetCallbackState State;
		State.bRecursive = bRecursive;
		umkaSetMetadata(Vm, &State);
		UmkaFuncContext Context = {};
		umkaGetFunc(Vm, nullptr, "Outer", &Context);
		umkaSetExecutionBudget(Vm, 200, nullptr, nullptr);
		TestTrue(TEXT("Nested and recursive native callbacks share the outer budget"), umkaCall(Vm, &Context) != 0);
		TestTrue(TEXT("Budget expires before all nested callbacks complete"), State.Calls > 1 && State.Calls < 100);
		TestFalse(TEXT("Fatal failure jumps to the live outer call"), State.bNestedCallFailed);
		TestEqual(TEXT("Nested callback fails from the budget"), FString(UTF8_TO_TCHAR(umkaGetError(Vm)->msg)), FString(TEXT("Instruction budget exceeded")));
		char File[128] = {}, Function[128] = {};
		int32 Line = 0;
		TestTrue(TEXT("The failing nested stack remains inspectable"), umkaGetCallStack(Vm, 0, UE_ARRAY_COUNT(File), nullptr, File, Function, &Line));
		TestEqual(TEXT("The failure retains the innermost function"), FString(UTF8_TO_TCHAR(Function)), FString(bRecursive ? TEXT("Outer") : TEXT("Inner")));
		TestTrue(TEXT("The outer error jumper is restored after unwind"), umkaCall(Vm, &Context) != 0);
		umkaFree(Vm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeTypeModuleIdentityTest, "UEmka.Runtime.NativeTypeModuleIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeTypeModuleIdentityTest::RunTest(const FString& Parameters)
{
	Umka* Vm = umkaAlloc();
	if (!TestNotNull(TEXT("Type module VM allocated"), Vm)) return false;
	if (TestTrue(TEXT("Type module VM initialized"), umkaInit(Vm, "identity-main.um",
		"import \"ue.um\"\ntype Alias* = ue::Vector\ntype Unrelated* = struct { X: real; Y: real; Z: real }\n"
		"fn Take*(A: Alias, B: ue::Vector, C: struct { X: real }, D: Unrelated) {}\nfn main() {}\n",
		10000, nullptr, 0, nullptr, false, false, nullptr))
		&& TestTrue(TEXT("Canonical native type module registered"), umkaAddModule(Vm, "ue.um", "type Vector* = struct { X: real; Y: real; Z: real }"))
		&& TestTrue(TEXT("Type module fixture compiles"), umkaCompile(Vm)))
	{
		const UmkaType* FunctionType = umkaGetFuncType(Vm, nullptr, "Take");
		const char* AliasPath = umkaGetTypeModulePath(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 0));
		const char* VectorPath = umkaGetTypeModulePath(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 1));
		if (TestNotNull(TEXT("Alias module path exists"), AliasPath)) TestTrue(TEXT("Alias retains its declaring module"), FString(UTF8_TO_TCHAR(AliasPath)).EndsWith(TEXT("identity-main.um")));
		if (TestNotNull(TEXT("Native module path exists"), VectorPath)) TestTrue(TEXT("Native type retains its canonical module"), FString(UTF8_TO_TCHAR(VectorPath)).EndsWith(TEXT("ue.um")));
		TestNull(TEXT("Anonymous type has no declaration module"), umkaGetTypeModulePath(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 2)));
		TestNull(TEXT("Null type has no declaration module"), umkaGetTypeModulePath(Vm, nullptr));
		TestTrue(TEXT("A declared alias shares the canonical native declaration"), umkaTypeSameDeclaration(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 0), "ue.um", "Vector"));
		TestTrue(TEXT("The canonical native type shares its own declaration"), umkaTypeSameDeclaration(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 1), "ue.um", "Vector"));
		TestFalse(TEXT("Anonymous types do not share a native declaration"), umkaTypeSameDeclaration(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 2), "ue.um", "Vector"));
		TestFalse(TEXT("An independent identical layout does not share a native declaration"), umkaTypeSameDeclaration(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 3), "ue.um", "Vector"));
		TestFalse(TEXT("Unknown modules do not share a declaration"), umkaTypeSameDeclaration(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 0), "missing.um", "Vector"));
		TestFalse(TEXT("Unknown type names do not share a declaration"), umkaTypeSameDeclaration(Vm, umkaGetFuncParamTypeByIndex(FunctionType, 0), "ue.um", "Missing"));
	}
	umkaFree(Vm);
	return true;
}

#endif
