// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/AutomationTest.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
UBlueprint* MakeTestBlueprint()
{
	return FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("UEmkaTest")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
}

UK2Node_UEmka* MakeScriptNode(UEdGraph* Graph, const FString& Script)
{
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = Script;
	Node->AllocateDefaultPins();
	return Node;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaTupleParsingTest, "UEmka.Editor.TupleParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaTupleParsingTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Script : {
		TEXT("fn Test*(): (^int, int) {}"),
		TEXT("fn Test*(): (int, ^int) {}"),
		TEXT("fn Test*(): ([]^int, int) {}"),
		TEXT("fn Test*(): (int; str) {}"),
		TEXT("fn Test*(): (int, @) {}")})
	{
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Script);
		TestTrue(Script, !Signature.bValid || !Signature.UnsupportedReason.IsEmpty());
		TestTrue(TEXT("Unsupported tuple exposes no return pins"),
			Signature.ReturnParams.IsEmpty() && !Signature.ReturnType.IsSet());
	}

	const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(
		TEXT("type Kind = enum(uint8) { First, Second }\nfn Test*(): (int, []bool, [2]bool, Kind) {}"));
	TestTrue(TEXT("Supported tuple is valid"), Signature.bValid && Signature.UnsupportedReason.IsEmpty());
	if (TestEqual(TEXT("All tuple elements remain ordered"), Signature.ReturnParams.Num(), 4))
	{
		TestEqual(TEXT("Scalar type"), Signature.ReturnParams[0].Type, EUEmkaValueType::Int);
		TestEqual(TEXT("Dynamic Boolean type"), Signature.ReturnParams[1].Type, EUEmkaValueType::Bool);
		TestTrue(TEXT("Dynamic Boolean array"), Signature.ReturnParams[1].bIsArray && !Signature.ReturnParams[1].bIsStaticArray);
		TestTrue(TEXT("Fixed Boolean array"), Signature.ReturnParams[2].bIsArray && Signature.ReturnParams[2].bIsStaticArray);
		TestEqual(TEXT("Declared enum base type"), Signature.ReturnParams[3].Type, EUEmkaValueType::UInt8);
		TestEqual(TEXT("Enum storage width"), Signature.ReturnParams[3].EnumByteSize, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBodyEditTest, "UEmka.Editor.BodyEditMarksBlueprintDirty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBodyEditTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = MakeTestBlueprint();
	if (!TestNotNull(TEXT("Blueprint created"), Blueprint)) return false;
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Event graph exists"), Graph)) return false;
	UK2Node_UEmka* Node = MakeScriptNode(Graph, TEXT("fn Test*(Value: int): int { return Value + 1 }"));
	UEdGraphNode* SourceNode = NewObject<UEdGraphNode>(Graph);
	Graph->AddNode(SourceNode);
	UEdGraphPin* SourcePin = SourceNode->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Int64, TEXT("Value"));
	UEdGraphPin* InputPin = Node->FindPinChecked(TEXT("Value"));
	SourcePin->MakeLinkTo(InputPin);
	const TArray<UEdGraphPin*> OriginalPins = Node->Pins;
	const FUEmkaSignature OriginalSignature = UK2Node_UEmka::ParseScript(Node->Script);
	Blueprint->Status = BS_UpToDate;
	const FString NewScript = TEXT("fn Test*(Value: int): int { return Value + 2 }");

	Node->OnScriptChanged(NewScript);

	TestEqual(TEXT("Body edit marks compiled Blueprint dirty"), Blueprint->Status, BS_Dirty);
	TestEqual(TEXT("Committed script stored"), Node->Script, NewScript);
	TestTrue(TEXT("Signature unchanged"), OriginalSignature == UK2Node_UEmka::ParseScript(Node->Script));
	TestTrue(TEXT("Existing pins retained without reconstruction"), OriginalPins == Node->Pins);
	TestTrue(TEXT("Existing input connection retained"), InputPin->LinkedTo.Contains(SourcePin));
	TestTrue(TEXT("Edited body compiles"), Node->LastErrorMessage.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBoolArrayBlueprintTest, "UEmka.Editor.BoolArrayBlueprintCompilation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBoolArrayBlueprintTest::RunTest(const FString& Parameters)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	FEdGraphPinType BoolArrayType;
	BoolArrayType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	BoolArrayType.ContainerType = EPinContainerType::Array;
	for (const bool bStatic : {false, true})
	{
		for (const bool bMulti : {false, true})
		{
			const FString ArrayType = bStatic ? TEXT("[2]bool") : TEXT("[]bool");
			const FString Script = bMulti
				? FString::Printf(TEXT("fn Test*(Values: %s): (%s, int) { return Values, 7 }"), *ArrayType, *ArrayType)
				: FString::Printf(TEXT("fn Test*(Values: %s): %s { return Values }"), *ArrayType, *ArrayType);
			UBlueprint* Blueprint = MakeTestBlueprint();
			if (!TestNotNull(TEXT("Blueprint created"), Blueprint)) return false;
			UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("TestArrays"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
			TArray<UK2Node_FunctionEntry*> Entries;
			Graph->GetNodesOfClass(Entries);
			if (!TestEqual(TEXT("One function entry"), Entries.Num(), 1)) return false;
			UK2Node_FunctionEntry* Entry = Entries[0];
			UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
			if (!TestNotNull(TEXT("Function result created"), Exit)) return false;
			UEdGraphPin* Input = Entry->CreateUserDefinedPin(TEXT("Values"), BoolArrayType, EGPD_Output);
			UEdGraphPin* Output = Exit->CreateUserDefinedPin(TEXT("OutValues"), BoolArrayType, EGPD_Input);
			UK2Node_UEmka* Node = MakeScriptNode(Graph, Script);
			TestTrue(TEXT("Function execution reaches script"), Schema->TryCreateConnection(Entry->GetThenPin(), Node->GetExecPin()));
			TestTrue(TEXT("Script execution reaches result"), Schema->TryCreateConnection(Node->GetThenPin(), Exit->GetExecPin()));
			TestTrue(TEXT("Boolean array input connected"), Schema->TryCreateConnection(Input, Node->FindPinChecked(TEXT("Values"))));
			TestTrue(TEXT("Boolean array output connected"), Schema->TryCreateConnection(Node->FindPinChecked(bMulti ? TEXT("ReturnValue1") : TEXT("ReturnValue")), Output));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FCompilerResultsLog CompileLog;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &CompileLog);
			TestEqual(FString::Printf(TEXT("%s Boolean array %s return compiles"), bStatic ? TEXT("Fixed") : TEXT("Dynamic"), bMulti ? TEXT("multi") : TEXT("single")), CompileLog.NumErrors, 0);
			TestTrue(TEXT("Blueprint generated executable bytecode"), Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);

			UFunction* Function = Blueprint->GeneratedClass->FindFunctionByName(Graph->GetFName());
			if (!TestNotNull(TEXT("Compiled array function exists"), Function)) return false;
			FArrayProperty* InputProperty = FindFProperty<FArrayProperty>(Function, TEXT("Values"));
			FArrayProperty* OutputProperty = FindFProperty<FArrayProperty>(Function, TEXT("OutValues"));
			if (!TestNotNull(TEXT("Compiled array input exists"), InputProperty)
				|| !TestNotNull(TEXT("Compiled array output exists"), OutputProperty)) return false;
			FStructOnScope FunctionParams(Function);
			const TArray<bool> Values = {true, false};
			*InputProperty->ContainerPtrToValuePtr<TArray<bool>>(FunctionParams.GetStructMemory()) = Values;
			UObject* Instance = NewObject<UObject>(GetTransientPackage(), Blueprint->GeneratedClass);
			Instance->ProcessEvent(Function, FunctionParams.GetStructMemory());
			TestTrue(TEXT("Compiled Blueprint preserves Boolean array values"),
				*OutputProperty->ContainerPtrToValuePtr<TArray<bool>>(FunctionParams.GetStructMemory()) == Values);
		}

		const TArray<bool> Values = {true, false};
		const FUEmkaScriptParam Param = UUEmkaFunctionLibrary::MakeBoolArrayParam(Values, bStatic);
		FUEmkaScriptParam Result;
		FString Error;
		const FString ArrayType = bStatic ? TEXT("[2]bool") : TEXT("[]bool");
		TestTrue(TEXT("Boolean array runtime round trip"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
			FString::Printf(TEXT("fn Test*(Values: %s): %s { return Values }"), *ArrayType, *ArrayType), TEXT("Test"), {Param},
			EUEmkaValueType::Bool, true, bStatic, Result, Error));
		TestTrue(TEXT("Boolean values preserved"), UUEmkaFunctionLibrary::GetBoolArrayResult(Result) == Values);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRuntimeFailureTest, "UEmka.Runtime.FixedArrayFailureClearsResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRuntimeFailureTest::RunTest(const FString& Parameters)
{
	const FString ScalarScript = TEXT("fn Test*(Values: [2]int): int { return Values[0] + Values[1] }");
	const FString MultiScript = TEXT("fn Test*(Values: [2]int): (int, str) { return Values[0] + Values[1], \"ok\" }");
	const FString ResultTypes = FString::Printf(TEXT("%d:0:8,%d:0:8"), static_cast<int32>(EUEmkaValueType::Int), static_cast<int32>(EUEmkaValueType::Str));
	const FUEmkaScriptParam WrongParam = UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {3}, true);
	const FUEmkaScriptParam CorrectParam = UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {3, 4}, true);
	FUEmkaScriptParam Result = UUEmkaFunctionLibrary::MakeStrParam(TEXT("stale"));
	Result.IntValue = 99;
	Result.IntArrayValue = {99};
	FString Error = TEXT("stale error");
	AddExpectedError(TEXT("Test: Parameter 1 expects exactly 2 array elements, but Blueprint supplied 1"), EAutomationExpectedErrorFlags::Contains, 2);

	TestFalse(TEXT("Wrong fixed length fails for scalar return"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		ScalarScript, TEXT("Test"), {WrongParam}, EUEmkaValueType::Int, false, false, Result, Error));
	TestTrue(TEXT("Failure replaces stale error"), Error.Contains(TEXT("expects exactly 2 array elements")));
	TestTrue(TEXT("Failure clears stale scalar result"), Result.StringValue.IsEmpty() && Result.IntValue == 0 && Result.IntArrayValue.IsEmpty());
	TestTrue(TEXT("Subsequent scalar call succeeds"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		ScalarScript, TEXT("Test"), {CorrectParam}, EUEmkaValueType::Int, false, false, Result, Error));
	TestTrue(TEXT("Success clears previous scalar error"), Error.IsEmpty());
	TestEqual(TEXT("Scalar result decoded"), Result.IntValue, int64{7});

	TArray<FUEmkaScriptParam> Results = {UUEmkaFunctionLibrary::MakeStrParam(TEXT("stale"))};
	Error = TEXT("stale error");
	TestFalse(TEXT("Wrong fixed length fails for multi return"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr,
		MultiScript, TEXT("Test"), {WrongParam}, ResultTypes, Results, Error));
	TestTrue(TEXT("Multi failure replaces stale error"), Error.Contains(TEXT("expects exactly 2 array elements")));
	TestTrue(TEXT("Multi failure clears stale results"), Results.IsEmpty());
	TestTrue(TEXT("Subsequent multi call succeeds"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr,
		MultiScript, TEXT("Test"), {CorrectParam}, ResultTypes, Results, Error));
	TestTrue(TEXT("Success clears previous multi error"), Error.IsEmpty());
	if (TestEqual(TEXT("Multi results decoded"), Results.Num(), 2))
	{
		TestEqual(TEXT("Multi integer result"), Results[0].IntValue, int64{7});
		TestEqual(TEXT("Multi string result"), Results[1].StringValue, FString(TEXT("ok")));
	}
	return true;
}

#endif
