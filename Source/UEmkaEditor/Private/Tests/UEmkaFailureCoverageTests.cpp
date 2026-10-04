// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaFunctionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaFailurePathsTest, "UEmka.Runtime.FailurePaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaFailurePathsTest::RunTest(const FString& Parameters)
{
	struct FFailureCase
	{
		FString Name;
		FString Script;
		TArray<FUEmkaScriptParam> Params;
		EUEmkaValueType ResultType = EUEmkaValueType::Int;
		bool bArray = false;
		bool bStatic = false;
		FString Expected;
	};
	const FString Identity = TEXT("fn Failure*(Value: int): int { return Value }");
	const FUEmkaScriptParam Int = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 1);
	const FUEmkaScriptParam Dynamic = UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {1, 2}, false);
	const FUEmkaScriptParam Fixed = UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {1, 2}, true);
	const TArray<FFailureCase> Cases = {
		{TEXT("Invalid script"), TEXT("fn Failure*(): int { return @ }"), {}, EUEmkaValueType::Int, false, false, TEXT("script.um:")},
		{TEXT("Missing function"), TEXT("fn Other*(): int { return 1 }"), {}, EUEmkaValueType::Int, false, false, TEXT("not found")},
		{TEXT("Missing argument"), Identity, {}, EUEmkaValueType::Int, false, false, TEXT("expects 1 parameters")},
		{TEXT("Extra argument"), Identity, {Int, Int}, EUEmkaValueType::Int, false, false, TEXT("expects 1 parameters")},
		{TEXT("Wrong scalar storage"), Identity, {UUEmkaFunctionLibrary::MakeStrParam(TEXT("bad"))}, EUEmkaValueType::Int, false, false, TEXT("metadata")},
		{TEXT("Array supplied to scalar"), Identity, {Dynamic}, EUEmkaValueType::Int, false, false, TEXT("metadata")},
		{TEXT("Scalar supplied to array"), TEXT("fn Failure*(Value: []int): int { return len(Value) }"), {Int}, EUEmkaValueType::Int, false, false, TEXT("metadata")},
		{TEXT("Fixed supplied to dynamic"), TEXT("fn Failure*(Value: []int): int { return len(Value) }"), {Fixed}, EUEmkaValueType::Int, false, false, TEXT("array-kind")},
		{TEXT("Dynamic supplied to fixed"), TEXT("fn Failure*(Value: [2]int): int { return Value[0] }"), {Dynamic}, EUEmkaValueType::Int, false, false, TEXT("array-kind")},
		{TEXT("Wrong array storage"), TEXT("fn Failure*(Value: []real): real { return Value[0] }"), {Dynamic}, EUEmkaValueType::Real, false, false, TEXT("metadata")},
		{TEXT("Wrong scalar result storage"), Identity, {Int}, EUEmkaValueType::Str, false, false, TEXT("metadata")},
		{TEXT("Wrong result shape"), TEXT("fn Failure*(): []int { return []int{1} }"), {}, EUEmkaValueType::Int, false, false, TEXT("array-kind")},
		{TEXT("Wrong result array kind"), TEXT("fn Failure*(): [2]int { return [2]int{1, 2} }"), {}, EUEmkaValueType::Int, true, false, TEXT("array-kind")},
		{TEXT("Division by zero"), TEXT("fn Failure*(Value: int): int { return 7 / Value }"), {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 0)}, EUEmkaValueType::Int, false, false, TEXT("zero")},
		{TEXT("Out of bounds"), TEXT("fn Failure*(Value: []int): int { return Value[3] }"), {Dynamic}, EUEmkaValueType::Int, false, false, TEXT("range")},
	};
	AddExpectedError(TEXT("Failure:"), EAutomationExpectedErrorFlags::Contains, Cases.Num());
	for (const FFailureCase& Case : Cases)
	{
		FUEmkaScriptParam Result = UUEmkaFunctionLibrary::MakeStrParam(TEXT("stale"));
		Result.IntValue = 99;
		Result.RealValue = 99.5;
		Result.Real32Value = 99.5f;
		Result.IntArrayValue = {99};
		Result.RealArrayValue = {99.5};
		Result.Real32ArrayValue = {99.5f};
		Result.StringArrayValue = {TEXT("stale")};
		FString Error = TEXT("stale");
		TestFalse(Case.Name, UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Case.Script, TEXT("Failure"), Case.Params,
			Case.ResultType, Case.bArray, Case.bStatic, Result, Error));
		TestTrue(Case.Name + TEXT(" diagnostic"), Error.Contains(Case.Expected, ESearchCase::IgnoreCase));
		TestTrue(Case.Name + TEXT(" clears every output"), Result.IntValue == 0 && Result.RealValue == 0 && Result.Real32Value == 0
			&& Result.StringValue.IsEmpty() && Result.IntArrayValue.IsEmpty() && Result.RealArrayValue.IsEmpty()
			&& Result.Real32ArrayValue.IsEmpty() && Result.StringArrayValue.IsEmpty());
		TestTrue(Case.Name + TEXT(" allows subsequent success"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Identity, TEXT("Failure"), {Int},
			EUEmkaValueType::Int, false, false, Result, Error));
		TestTrue(Case.Name + TEXT(" clears the prior error"), Error.IsEmpty() && Result.IntValue == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaMultiFailurePathsTest, "UEmka.Runtime.MultiFailurePaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaMultiFailurePathsTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("fn MultiFailure*(Value: int): (int, str) { return 10 / Value, \"ok\" }");
	const FString Types = FString::Printf(TEXT("%d:0:8,%d:0:8"), static_cast<int32>(EUEmkaValueType::Int), static_cast<int32>(EUEmkaValueType::Str));
	const FUEmkaScriptParam Zero = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 0);
	const FUEmkaScriptParam One = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 1);
	struct FFailureCase { FString Script; FString Types; TArray<FUEmkaScriptParam> Params; FString Expected; };
	const TArray<FFailureCase> Cases = {
		{Script, TEXT(""), {One}, TEXT("No result types")},
		{TEXT("fn MultiFailure*(): (int, str) { return @ }"), Types, {}, TEXT("script.um:")},
		{TEXT("fn Other*(): (int, str) { return 1, \"ok\" }"), Types, {}, TEXT("not found")},
		{Script, Types, {}, TEXT("expects 1 parameters")},
		{Script, TEXT("0:0:8"), {One}, TEXT("multi-return signature")},
		{Script, TEXT("10:0:8,12:0:8"), {One}, TEXT("metadata")},
		{Script, TEXT("0:1:8,12:0:8"), {One}, TEXT("array-kind")},
		{Script, Types, {Zero}, TEXT("zero")},
	};
	AddExpectedError(TEXT("MultiFailure:"), EAutomationExpectedErrorFlags::Contains, Cases.Num());
	for (const FFailureCase& Case : Cases)
	{
		TArray<FUEmkaScriptParam> Results = {UUEmkaFunctionLibrary::MakeStrParam(TEXT("stale"))};
		FString Error = TEXT("stale");
		TestFalse(TEXT("Multi-return failure"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Case.Script, TEXT("MultiFailure"), Case.Params, Case.Types, Results, Error));
		TestTrue(TEXT("Multi-return diagnostic"), Error.Contains(Case.Expected, ESearchCase::IgnoreCase));
		TestTrue(TEXT("Multi-return failure clears every output"), Results.IsEmpty());
		TestTrue(TEXT("Multi-return subsequent success"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Script, TEXT("MultiFailure"), {One}, Types, Results, Error));
		TestTrue(TEXT("Successful multi-return clears prior error"), Error.IsEmpty() && Results.Num() == 2);
	}
	TestEqual(TEXT("Missing multi-result uses the default container"), UUEmkaFunctionLibrary::GetMultiResultAt({}, -1).IntValue, int64{0});
	TestEqual(TEXT("Past-end multi-result uses the default container"), UUEmkaFunctionLibrary::GetMultiResultAt({}, 1).StringValue, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaCompileInspectionTest, "UEmka.Runtime.CompileInspection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaCompileInspectionTest::RunTest(const FString& Parameters)
{
	FString Error = TEXT("stale");
	int32 Line = 99;
	TestFalse(TEXT("Compile-only check reports invalid source"), UUEmkaFunctionLibrary::CompileCheckScript(TEXT("fn Test*(): int {\n return Missing\n}"), Error, Line));
	TestEqual(TEXT("Compile diagnostic reports the source line"), Line, 2);
	TestTrue(TEXT("Compile diagnostic names the unresolved symbol"), Error.Contains(TEXT("Missing")));
	const FString NeverRun = TEXT("fn Test*() { for true { printf(\"never execute during inspection\") } }");
	TestTrue(TEXT("Compile-only check does not execute the function"), UUEmkaFunctionLibrary::CompileCheckScript(NeverRun, Error, Line));
	TestTrue(TEXT("Successful check clears stale diagnostics"), Error.IsEmpty() && Line == -1);
	FUEmkaCompiledSignature Signature;
	TestTrue(TEXT("Inspection does not execute the function"), UUEmkaFunctionLibrary::InspectScriptFunction(NeverRun, TEXT("Test"), Signature));
	TestTrue(TEXT("Void metadata"), Signature.Result.bSupported && Signature.Result.Type == EUEmkaValueType::Void && Signature.Params.IsEmpty());
	Signature.Params.AddDefaulted();
	TestFalse(TEXT("Missing function inspection fails"), UUEmkaFunctionLibrary::InspectScriptFunction(TEXT("fn Other*() {}"), TEXT("Missing"), Signature));
	TestTrue(TEXT("Failed inspection clears stale metadata"), Signature.Params.IsEmpty() && !Signature.Result.bSupported);
	return true;
}

#endif
