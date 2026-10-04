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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRawStringSignatureTest, "UEmka.Editor.RawStringSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRawStringSignatureTest::RunTest(const FString& Parameters)
{
	const FString Prefix = TEXT("const Note = `fn Fake*(Value: ^int): map[int]int { }\n")
		TEXT("type FakeEnum = enum { First; Second }\n")
		TEXT("type FakeStruct = struct { Value: ^int }\n")
		TEXT("// /* \" ' } ) \\\n")
		TEXT("trailing backslash\\`\n");
	for (const FString& Body : {FString(TEXT("{ return Value }")), FString(TEXT("{\n return Value"))})
	{
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Prefix + TEXT("fn Real*(Value: int): int ") + Body);
		TestTrue(TEXT("Real signature survives raw string and unfinished body"), Signature.bValid && Signature.UnsupportedReason.IsEmpty());
		TestEqual(TEXT("Raw string does not introduce exported functions"), Signature.FunctionName, FString(TEXT("Real")));
		if (TestEqual(TEXT("Real parameter retained"), Signature.Params.Num(), 1))
		{
			TestEqual(TEXT("Real parameter type"), Signature.Params[0].Type, EUEmkaValueType::Int);
		}
		TestTrue(TEXT("Real return type retained"), Signature.ReturnType.IsSet() && Signature.ReturnType.GetValue() == EUEmkaValueType::Int);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaAliasSignatureTest, "UEmka.Editor.CompiledAliasSignatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaAliasSignatureTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Count = int16\ntype Number = Count\n")
		TEXT("fn Test*(Left, Right: Number): Number { return Left + Right }");
	const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Script);
	TestTrue(TEXT("Primitive alias chain resolves"), Signature.bValid && Signature.UnsupportedReason.IsEmpty());
	if (TestEqual(TEXT("Grouped parameter names remain separate"), Signature.Params.Num(), 2))
	{
		TestEqual(TEXT("First grouped name"), Signature.Params[0].Name, FString(TEXT("Left")));
		TestEqual(TEXT("Second grouped name"), Signature.Params[1].Name, FString(TEXT("Right")));
		TestEqual(TEXT("Alias underlying type"), Signature.Params[0].Type, EUEmkaValueType::Int16);
	}
	FUEmkaScriptParam Result;
	FString Error;
	TestTrue(TEXT("Grouped primitive aliases execute"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int16, 20), UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int16, 22)},
		EUEmkaValueType::Int16, false, false, Result, Error));
	TestEqual(TEXT("Primitive alias result"), Result.IntValue, int64{42});

	for (const bool bStatic : {false, true})
	{
		const FString ArrayScript = FString::Printf(TEXT("const Length = 2\ntype Element = uint32\ntype Values = %sElement\ntype Alias = Values\nfn Test*(Input: Alias): Alias { return Input }"),
			bStatic ? TEXT("[Length]") : TEXT("[]"));
		const FUEmkaSignature ArraySignature = UK2Node_UEmka::ParseScript(ArrayScript);
		TestTrue(TEXT("Array alias chain resolves"), ArraySignature.bValid && ArraySignature.UnsupportedReason.IsEmpty());
		TestTrue(TEXT("Array alias result shape retained"), ArraySignature.bReturnIsArray && ArraySignature.bReturnIsStaticArray == bStatic);
		if (TestEqual(TEXT("Array alias parameter retained"), ArraySignature.Params.Num(), 1))
		{
			TestEqual(TEXT("Array element alias type"), ArraySignature.Params[0].Type, EUEmkaValueType::UInt32);
			TestTrue(TEXT("Array alias shape retained"), ArraySignature.Params[0].bIsArray && ArraySignature.Params[0].bIsStaticArray == bStatic);
		}
		const TArray<int64> Values = {0, 4294967295LL};
		TestTrue(TEXT("Array aliases execute"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, ArrayScript, TEXT("Test"),
			{UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::UInt32, Values, bStatic)}, EUEmkaValueType::UInt32, true, bStatic, Result, Error));
		TestTrue(TEXT("Array aliases preserve unsigned values"), Result.IntArrayValue == Values);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaStructAliasTest, "UEmka.Editor.FlatStructAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaStructAliasTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Count = int16\ntype Kind = enum(int8) { low = -7; high = 7 }\n")
		TEXT("type KindAlias = Kind\ntype Record = struct { Value: Count; Code: KindAlias }\n")
		TEXT("type Alias = Record\nfn Test*(Input: Alias): Alias { return Input }");
	const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Script);
	TestTrue(TEXT("Struct and field aliases resolve"), Signature.bValid && Signature.UnsupportedReason.IsEmpty() && Signature.bNeedsShim);
	if (!TestEqual(TEXT("Struct fields flattened to parameters"), Signature.Params.Num(), 2)
		|| !TestEqual(TEXT("Struct fields flattened to results"), Signature.ReturnParams.Num(), 2)) return false;
	TestEqual(TEXT("Integer field alias resolves"), Signature.Params[0].Type, EUEmkaValueType::Int16);
	TestEqual(TEXT("Enum field alias uses signed base"), Signature.Params[1].Type, EUEmkaValueType::Int8);
	const FString ResultTypes = FString::Printf(TEXT("%d:0:8,%d:0:1"), static_cast<int32>(EUEmkaValueType::Int16), static_cast<int32>(EUEmkaValueType::Int8));
	TArray<FUEmkaScriptParam> Results;
	FString Error;
	TestTrue(TEXT("Generated alias struct shim executes"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr,
		UK2Node_UEmka::GetEffectiveScript(Script, Signature), UK2Node_UEmka::GetEffectiveFunctionName(Signature),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int16, -123), UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int8, -7)},
		ResultTypes, Results, Error));
	if (TestEqual(TEXT("Shim returns both fields"), Results.Num(), 2))
	{
		TestEqual(TEXT("Signed integer field preserved"), Results[0].IntValue, int64{-123});
		TestEqual(TEXT("Signed enum field preserved"), Results[1].IntValue, int64{-7});
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaUnsupportedAliasTest, "UEmka.Editor.UnsupportedAliasesFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaUnsupportedAliasTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Declaration : {
		TEXT("type Hidden = ^int\ntype Alias = Hidden\n"),
		TEXT("type Hidden = map[int]int\ntype Alias = Hidden\n"),
		TEXT("type Hidden = struct { Value: enum { one } }\ntype Alias = Hidden\n"),
		TEXT("type Inner = struct { Value: int }\ntype Hidden = struct { Value: Inner }\ntype Alias = Hidden\n")})
	{
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(FString(Declaration) + TEXT("fn Test*(Value: Alias) {}"));
		TestTrue(TEXT("Unsupported alias rejected"), !Signature.bValid || !Signature.UnsupportedReason.IsEmpty());
		TestTrue(TEXT("Unsupported aliases expose no partial data signature"), Signature.Params.IsEmpty()
			&& Signature.ReturnParams.IsEmpty() && !Signature.ReturnType.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaIntegerRangeTest, "UEmka.Runtime.CompiledIntegerRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaIntegerRangeTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 36);
	struct FIntegerRange
	{
		const TCHAR* Name;
		EUEmkaValueType Type;
		int64 Min;
		int64 Max;
	};
	for (const FIntegerRange& Range : {
		FIntegerRange{TEXT("int8"), EUEmkaValueType::Int8, -128, 127},
		FIntegerRange{TEXT("int16"), EUEmkaValueType::Int16, -32768, 32767},
		FIntegerRange{TEXT("int32"), EUEmkaValueType::Int32, -2147483648LL, 2147483647LL},
		FIntegerRange{TEXT("uint8"), EUEmkaValueType::UInt8, 0, 255},
		FIntegerRange{TEXT("uint16"), EUEmkaValueType::UInt16, 0, 65535},
		FIntegerRange{TEXT("uint32"), EUEmkaValueType::UInt32, 0, 4294967295LL}})
	{
		const FString Script = FString::Printf(TEXT("type Number = %s\nfn Test*(Value: Number): Number { return Value }"), Range.Name);
		FUEmkaScriptParam Result;
		FString Error;
		for (const int64 Value : {Range.Min, Range.Max})
		{
			TestTrue(FString::Printf(TEXT("%s boundary accepted"), Range.Name), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
				Script, TEXT("Test"), {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, Value)}, Range.Type, false, false, Result, Error));
			TestEqual(TEXT("Boundary preserved"), Result.IntValue, Value);
		}
		for (const int64 Value : {Range.Min - 1, Range.Max + 1})
		{
			Result = UUEmkaFunctionLibrary::MakeStrParam(TEXT("stale"));
			Result.IntValue = 99;
			TestFalse(FString::Printf(TEXT("%s overflow rejected"), Range.Name), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
				Script, TEXT("Test"), {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, Value)}, Range.Type, false, false, Result, Error));
			TestTrue(TEXT("Range failure explained"), Error.Contains(TEXT("range"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("Range failure clears stale scalar"), Result.IntValue == 0 && Result.StringValue.IsEmpty());
		}
		for (const bool bStatic : {false, true})
		{
			const FString ArrayScript = FString::Printf(TEXT("type Number = %s\nfn Test*(Values: %sNumber): %sNumber { return Values }"),
				Range.Name, bStatic ? TEXT("[2]") : TEXT("[]"), bStatic ? TEXT("[2]") : TEXT("[]"));
			const TArray<int64> Values = {Range.Min, Range.Max};
			TestTrue(TEXT("Array integer boundaries accepted"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
				ArrayScript, TEXT("Test"), {UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, Values, bStatic)}, Range.Type, true, bStatic, Result, Error));
			TestTrue(TEXT("Array boundaries preserved"), Result.IntArrayValue == Values);
			for (const int64 Overflow : {Range.Min - 1, Range.Max + 1})
			{
				Result.IntArrayValue = {99};
				TestFalse(TEXT("Array overflow rejected"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, ArrayScript, TEXT("Test"),
					{UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {Range.Min, Overflow}, bStatic)}, Range.Type, true, bStatic, Result, Error));
				TestTrue(TEXT("Array range failure explained"), Error.Contains(TEXT("range"), ESearchCase::IgnoreCase));
				TestTrue(TEXT("Array failure clears stale result"), Result.IntArrayValue.IsEmpty());
			}
		}
	}
	FUEmkaScriptParam Result;
	FString Error;
	TestTrue(TEXT("uint preserves every bit"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		TEXT("fn Test*(Value: uint): uint { return Value }"), TEXT("Test"), {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::UInt, -1)},
		EUEmkaValueType::UInt, false, false, Result, Error));
	TestEqual(TEXT("uint maximum survives signed bit representation"), Result.IntValue, int64{-1});
	for (const bool bStatic : {false, true})
	{
		const FString Script = FString::Printf(TEXT("fn Test*(Values: %suint): %suint { return Values }"),
			bStatic ? TEXT("[2]") : TEXT("[]"), bStatic ? TEXT("[2]") : TEXT("[]"));
		const TArray<int64> Values = {-1, MIN_int64};
		TestTrue(TEXT("uint arrays preserve every bit"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"),
			{UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::UInt, Values, bStatic)}, EUEmkaValueType::UInt, true, bStatic, Result, Error));
		TestTrue(TEXT("uint arrays retain signed bit representations"), Result.IntArrayValue == Values);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaResultRangeMetadataTest, "UEmka.Runtime.ResultRangeMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaResultRangeMetadataTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 2);
	FUEmkaScriptParam Result = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::UInt8, 99);
	FString Error;
	TestFalse(TEXT("Narrow result metadata cannot truncate a wider scalar"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		TEXT("fn Test*(): uint32 { return 4294967295 }"), TEXT("Test"), {}, EUEmkaValueType::UInt8, false, false, Result, Error));
	TestTrue(TEXT("Scalar mismatch clears stale result and explains failure"), Result.IntValue == 0 && Error.Contains(TEXT("metadata")));
	TArray<FUEmkaScriptParam> Results = {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::UInt8, 99)};
	const FString ResultTypes = FString::Printf(TEXT("%d:0:8,%d:0:8"), static_cast<int32>(EUEmkaValueType::UInt8), static_cast<int32>(EUEmkaValueType::Int));
	TestFalse(TEXT("Narrow result metadata cannot truncate a wider tuple field"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr,
		TEXT("fn Test*(): (uint32, int) { return 4294967295, 7 }"), TEXT("Test"), {}, ResultTypes, Results, Error));
	TestTrue(TEXT("Tuple mismatch clears stale results and explains failure"), Results.IsEmpty() && Error.Contains(TEXT("metadata")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaEnumStorageTest, "UEmka.Runtime.EnumIntegerStorage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaEnumStorageTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 2);
	const FString DefaultScript = TEXT("type Kind = enum { low = -7; wide = 4294967296 }\nfn Test*(Value: Kind): Kind { return Value }");
	const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(DefaultScript);
	if (TestEqual(TEXT("Default enum parameter found"), Signature.Params.Num(), 1))
	{
		TestEqual(TEXT("Default enum uses int64"), Signature.Params[0].Type, EUEmkaValueType::Int);
	}
	FUEmkaScriptParam Result;
	FString Error;
	for (const int64 Value : {int64{-7}, int64{4294967296LL}})
	{
		TestTrue(TEXT("Default enum executes"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, DefaultScript, TEXT("Test"),
			{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, Value)}, EUEmkaValueType::Int, false, false, Result, Error));
		TestEqual(TEXT("Default enum preserves negative and wide values"), Result.IntValue, Value);
	}
	for (const bool bStatic : {false, true})
	{
		const FString Script = FString::Printf(TEXT("type Kind = enum(int8) { low = -7; high = 7 }\nfn Test*(Values: %sKind): (%sKind, Kind) { return Values, .low }"),
			bStatic ? TEXT("[2]") : TEXT("[]"), bStatic ? TEXT("[2]") : TEXT("[]"));
		const FString ResultTypes = FString::Printf(TEXT("%d:%d:1,%d:0:1"), static_cast<int32>(EUEmkaValueType::Int8), bStatic ? 2 : 1, static_cast<int32>(EUEmkaValueType::Int8));
		TArray<FUEmkaScriptParam> Results;
		const TArray<int64> Values = {-7, 7};
		TestTrue(TEXT("Signed enum arrays and tuple execute"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Script, TEXT("Test"),
			{UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int8, Values, bStatic)}, ResultTypes, Results, Error));
		if (TestEqual(TEXT("Enum tuple fields returned"), Results.Num(), 2))
		{
			TestTrue(TEXT("Narrow enum arrays preserve sign"), Results[0].IntArrayValue == Values);
			TestEqual(TEXT("Narrow enum tuple preserves sign"), Results[1].IntValue, int64{-7});
		}
		Results = {UUEmkaFunctionLibrary::MakeStrParam(TEXT("stale"))};
		TestFalse(TEXT("Narrow enum multi-return parameter overflow rejected"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Script, TEXT("Test"),
			{UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {-7, 128}, bStatic)}, ResultTypes, Results, Error));
		TestTrue(TEXT("Multi-return failure clears stale outputs"), Results.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaUInt32BlueprintTest, "UEmka.Editor.UInt32ArrayBlueprintExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaUInt32BlueprintTest::RunTest(const FString& Parameters)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	FEdGraphPinType ArrayType;
	ArrayType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	ArrayType.ContainerType = EPinContainerType::Array;
	for (const bool bStatic : {false, true})
	{
		for (const bool bMulti : {false, true})
		{
			const FString UmkaArrayType = bStatic ? TEXT("[2]uint32") : TEXT("[]uint32");
			const FString Script = bMulti
				? FString::Printf(TEXT("fn Test*(Values: %s): (%s, uint32) { return Values, 4294967295 }"), *UmkaArrayType, *UmkaArrayType)
				: FString::Printf(TEXT("fn Test*(Values: %s): %s { return Values }"), *UmkaArrayType, *UmkaArrayType);
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
				MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("UEmkaUInt32Test")),
				BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
			if (!TestNotNull(TEXT("Blueprint created"), Blueprint)) return false;
			UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("TestArrays"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
			TArray<UK2Node_FunctionEntry*> Entries;
			Graph->GetNodesOfClass(Entries);
			if (!TestEqual(TEXT("One function entry"), Entries.Num(), 1)) return false;
			UK2Node_FunctionEntry* Entry = Entries[0];
			UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
			if (!TestNotNull(TEXT("Function result created"), Exit)) return false;
			UEdGraphPin* Input = Entry->CreateUserDefinedPin(TEXT("Values"), ArrayType, EGPD_Output);
			UEdGraphPin* Output = Exit->CreateUserDefinedPin(TEXT("OutValues"), ArrayType, EGPD_Input);
			UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
			Graph->AddNode(Node);
			Node->CreateNewGuid();
			Node->Script = Script;
			Node->AllocateDefaultPins();
			UEdGraphPin* InputPin = Node->FindPin(TEXT("Values"));
			UEdGraphPin* OutputPin = Node->FindPin(bMulti ? TEXT("ReturnValue1") : TEXT("ReturnValue"));
			if (!TestNotNull(TEXT("Unsigned array input pin"), InputPin) || !TestNotNull(TEXT("Unsigned array output pin"), OutputPin)) return false;
			TestEqual(TEXT("uint32 array uses int64 pins"), InputPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Int64);
			TestEqual(TEXT("uint32 array output uses int64 pins"), OutputPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Int64);
			if (bMulti)
			{
				UEdGraphPin* ScalarOutputPin = Node->FindPin(TEXT("ReturnValue2"));
				if (!TestNotNull(TEXT("uint32 scalar output pin"), ScalarOutputPin)) return false;
				TestEqual(TEXT("uint32 scalar uses int64 pins"), ScalarOutputPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Int64);
			}
			TestTrue(TEXT("Entry connected"), Schema->TryCreateConnection(Entry->GetThenPin(), Node->GetExecPin()));
			TestTrue(TEXT("Exit connected"), Schema->TryCreateConnection(Node->GetThenPin(), Exit->GetExecPin()));
			TestTrue(TEXT("Unsigned array input connected"), Schema->TryCreateConnection(Input, InputPin));
			TestTrue(TEXT("Unsigned array output connected"), Schema->TryCreateConnection(OutputPin, Output));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FCompilerResultsLog CompileLog;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &CompileLog);
			if (!TestEqual(TEXT("Unsigned array Blueprint compiles"), CompileLog.NumErrors, 0)) return false;
			UFunction* Function = Blueprint->GeneratedClass->FindFunctionByName(Graph->GetFName());
			if (!TestNotNull(TEXT("Compiled function exists"), Function)) return false;
			FArrayProperty* InputProperty = FindFProperty<FArrayProperty>(Function, TEXT("Values"));
			FArrayProperty* OutputProperty = FindFProperty<FArrayProperty>(Function, TEXT("OutValues"));
			if (!TestNotNull(TEXT("Compiled input array exists"), InputProperty) || !TestNotNull(TEXT("Compiled output array exists"), OutputProperty)) return false;
			TestTrue(TEXT("Input storage uses int64 elements"), InputProperty->Inner->IsA<FInt64Property>());
			TestTrue(TEXT("Output storage uses int64 elements"), OutputProperty->Inner->IsA<FInt64Property>());
			FStructOnScope FunctionParams(Function);
			const TArray<int64> Values = {2147483648LL, 4294967295LL};
			*InputProperty->ContainerPtrToValuePtr<TArray<int64>>(FunctionParams.GetStructMemory()) = Values;
			UObject* Instance = NewObject<UObject>(GetTransientPackage(), Blueprint->GeneratedClass);
			Instance->ProcessEvent(Function, FunctionParams.GetStructMemory());
			TestTrue(TEXT("Compiled Blueprint preserves full uint32 range"),
				*OutputProperty->ContainerPtrToValuePtr<TArray<int64>>(FunctionParams.GetStructMemory()) == Values);
		}
	}
	return true;
}

#endif
