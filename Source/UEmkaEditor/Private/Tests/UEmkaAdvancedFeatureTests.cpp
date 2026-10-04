// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UEmkaCoverageCases.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
struct FNativeValue
{
	enum class EKind { Integer, Real, String, Boolean, Array, Record, Map };
	EKind Kind = EKind::Integer;
	int64 Integer = 0;
	double Real = 0;
	FString String;
	TArray<FString> Names;
	TArray<FNativeValue> Children;
};

FNativeValue Int(const int64 Value) { FNativeValue Result; Result.Integer = Value; return Result; }
FNativeValue Real(const double Value) { FNativeValue Result; Result.Kind = FNativeValue::EKind::Real; Result.Real = Value; return Result; }
FNativeValue Str(const FString& Value) { FNativeValue Result; Result.Kind = FNativeValue::EKind::String; Result.String = Value; return Result; }
FNativeValue Bool(const bool Value) { FNativeValue Result; Result.Kind = FNativeValue::EKind::Boolean; Result.Integer = Value; return Result; }
FNativeValue Array(TArray<FNativeValue> Values) { FNativeValue Result; Result.Kind = FNativeValue::EKind::Array; Result.Children = MoveTemp(Values); return Result; }
FNativeValue Map(TArray<FNativeValue> KeysAndValues) { FNativeValue Result; Result.Kind = FNativeValue::EKind::Map; Result.Children = MoveTemp(KeysAndValues); return Result; }
FNativeValue Record(TArray<FString> Names, TArray<FNativeValue> Values)
{
	FNativeValue Result;
	Result.Kind = FNativeValue::EKind::Record;
	Result.Names = MoveTemp(Names);
	Result.Children = MoveTemp(Values);
	return Result;
}

FNativeValue Scalar(const FUEmkaScriptParam& Value)
{
	switch (Value.Type)
	{
		case EUEmkaValueType::Bool: return Bool(Value.IntValue != 0);
		case EUEmkaValueType::Str: return Str(Value.StringValue);
		case EUEmkaValueType::Real: return Real(Value.RealValue);
		case EUEmkaValueType::Real32: return Real(Value.Real32Value);
		default: return Int(Value.IntValue);
	}
}

bool WriteNative(FAutomationTestBase& Test, const FString& Label, FProperty* Property, void* Data, const FNativeValue& Value)
{
	using EKind = FNativeValue::EKind;
	if (Value.Kind == EKind::Boolean)
	{
		FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" native bool"), BoolProperty)) return false;
		BoolProperty->SetPropertyValue(Data, Value.Integer != 0);
		return true;
	}
	if (Value.Kind == EKind::String)
	{
		FStrProperty* String = CastField<FStrProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" native string"), String)) return false;
		String->SetPropertyValue(Data, Value.String);
		return true;
	}
	if (Value.Kind == EKind::Integer || Value.Kind == EKind::Real)
	{
		FNumericProperty* Number = CastField<FNumericProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" native number"), Number)) return false;
		if (!Test.TestEqual(Label + TEXT(" numeric kind"), Number->IsFloatingPoint(), Value.Kind == EKind::Real)) return false;
		if (Value.Kind == EKind::Real) Number->SetFloatingPointPropertyValue(Data, Value.Real);
		else Number->SetIntPropertyValue(Data, static_cast<uint64>(Value.Integer));
		return true;
	}
	if (Value.Kind == EKind::Array)
	{
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" native array"), ArrayProperty)) return false;
		FScriptArrayHelper Helper(ArrayProperty, Data);
		Helper.Resize(Value.Children.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			if (!WriteNative(Test, Label + FString::Printf(TEXT("[%d]"), Index), ArrayProperty->Inner, Helper.GetRawPtr(Index), Value.Children[Index])) return false;
		}
		return true;
	}
	if (Value.Kind == EKind::Record)
	{
		FStructProperty* Struct = CastField<FStructProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" native struct"), Struct)
			|| !Test.TestEqual(Label + TEXT(" authored field count"), Value.Names.Num(), Value.Children.Num())) return false;
		int32 FieldCount = 0;
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
		{
			const int32 Index = Value.Names.IndexOfByKey(It->GetAuthoredName());
			if (!Test.TestTrue(Label + TEXT(" authored field ") + It->GetAuthoredName(), Index != INDEX_NONE)) return false;
			if (!WriteNative(Test, Label + TEXT(".") + It->GetAuthoredName(), *It, It->ContainerPtrToValuePtr<void>(Data), Value.Children[Index])) return false;
			++FieldCount;
		}
		return Test.TestEqual(Label + TEXT(" native field count"), FieldCount, Value.Children.Num());
	}
	FMapProperty* MapProperty = CastField<FMapProperty>(Property);
	if (!Test.TestNotNull(Label + TEXT(" native map"), MapProperty)
		|| !Test.TestEqual(Label + TEXT(" paired map entries"), Value.Children.Num() % 2, 0)) return false;
	FScriptMapHelper Helper(MapProperty, Data);
	Helper.EmptyValues();
	for (int32 Index = 0; Index < Value.Children.Num(); Index += 2)
	{
		const int32 Entry = Helper.AddDefaultValue_Invalid_NeedsRehash();
		if (!WriteNative(Test, Label + TEXT(" key"), MapProperty->KeyProp, Helper.GetKeyPtr(Entry), Value.Children[Index])
			|| !WriteNative(Test, Label + TEXT(" value"), MapProperty->ValueProp, Helper.GetValuePtr(Entry), Value.Children[Index + 1])) return false;
	}
	Helper.Rehash();
	return true;
}

struct FInput
{
	FString Name;
	FNativeValue Value;
};

struct FBlueprintFunction
{
	UBlueprint* Blueprint = nullptr;
	UK2Node_UEmka* Node = nullptr;
	UFunction* Function = nullptr;
	UObject* Instance = nullptr;
};

bool BuildFunction(FAutomationTestBase& Test, const FString& Label, const FString& Script,
	const TArray<FInput>& Inputs, const int32 NumOutputs, FBlueprintFunction& Built,
	const TMap<FName, FString>& Defaults = {}, UPackage* Package = GetTransientPackage(),
	const TFunction<void(UK2Node_UEmka*)>& Configure = {})
{
	FString Error;
	int32 Line = -1;
	if (!Test.TestTrue(Label + TEXT(" source compiles: ") + Error, UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, Line)))
	{
		Test.AddError(Label + FString::Printf(TEXT(" source line %d: %s"), Line, *Error));
		return false;
	}
	Built.Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("UEmkaAdvanced")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!Test.TestNotNull(Label + TEXT(" Blueprint"), Built.Blueprint)) return false;
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Built.Blueprint, TEXT("ExecuteAdvanced"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Built.Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (!Test.TestEqual(Label + TEXT(" entry count"), Entries.Num(), 1)) return false;
	UK2Node_FunctionEntry* Entry = Entries[0];
	UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
	if (!Test.TestNotNull(Label + TEXT(" result node"), Exit)) return false;
	Built.Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Built.Node);
	Built.Node->CreateNewGuid();
	Built.Node->Script = Script;
	if (Configure) Configure(Built.Node);
	Built.Node->AllocateDefaultPins();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Test.TestTrue(Label + TEXT(" entry execution"), Schema->TryCreateConnection(Entry->GetThenPin(), Built.Node->GetExecPin()))
		|| !Test.TestTrue(Label + TEXT(" exit execution"), Schema->TryCreateConnection(Built.Node->GetThenPin(), Exit->GetExecPin()))) return false;
	for (const FInput& Input : Inputs)
	{
		UEdGraphPin* Pin = Built.Node->FindPin(FName(*Input.Name), EGPD_Input);
		if (!Test.TestNotNull(Label + TEXT(" input ") + Input.Name, Pin)) return false;
		UEdGraphPin* EntryPin = Entry->CreateUserDefinedPin(FName(*Input.Name), Pin->PinType, EGPD_Output);
		if (!Test.TestTrue(Label + TEXT(" connect ") + Input.Name, Schema->TryCreateConnection(EntryPin, Pin))) return false;
	}
	for (const TPair<FName, FString>& Default : Defaults)
	{
		UEdGraphPin* Pin = Built.Node->FindPin(Default.Key, EGPD_Input);
		if (!Test.TestNotNull(Label + TEXT(" editable default"), Pin)) return false;
		Schema->TrySetDefaultValue(*Pin, Default.Value);
		if (!Test.TestEqual(Label + TEXT(" stored override"), Pin->DefaultValue, Default.Value)) return false;
	}
	TArray<UEdGraphPin*> ResultPins;
	for (UEdGraphPin* Pin : Built.Node->Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) ResultPins.Add(Pin);
	}
	if (!Test.TestEqual(Label + TEXT(" ordered output count"), ResultPins.Num(), NumOutputs)) return false;
	for (int32 Index = 0; Index < ResultPins.Num(); ++Index)
	{
		UEdGraphPin* ExitPin = Exit->CreateUserDefinedPin(FName(*FString::Printf(TEXT("Out%d"), Index)), ResultPins[Index]->PinType, EGPD_Input);
		if (!Test.TestTrue(Label + TEXT(" connect output"), Schema->TryCreateConnection(ResultPins[Index], ExitPin))) return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Built.Blueprint);
	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(Built.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
	if (!Test.TestEqual(Label + TEXT(" Blueprint compile errors"), Log.NumErrors, 0)
		|| !Test.TestTrue(Label + TEXT(" executable Blueprint"), Built.Blueprint->Status == BS_UpToDate || Built.Blueprint->Status == BS_UpToDateWithWarnings)) return false;
	Built.Function = Built.Blueprint->GeneratedClass->FindFunctionByName(Graph->GetFName());
	if (!Test.TestNotNull(Label + TEXT(" compiled function"), Built.Function)) return false;
	Built.Instance = NewObject<UObject>(GetTransientPackage(), Built.Blueprint->GeneratedClass);
	return true;
}

bool Invoke(FAutomationTestBase& Test, const FString& Label, const FBlueprintFunction& Built,
	const TArray<FInput>& Inputs, const TArray<FNativeValue>& Outputs)
{
	FStructOnScope Actual(Built.Function);
	FStructOnScope Expected(Built.Function);
	for (const FInput& Input : Inputs)
	{
		FProperty* Property = FindFProperty<FProperty>(Built.Function, FName(*Input.Name));
		if (!Test.TestNotNull(Label + TEXT(" reflected input ") + Input.Name, Property)
			|| !WriteNative(Test, Label + TEXT(" ") + Input.Name, Property, Property->ContainerPtrToValuePtr<void>(Actual.GetStructMemory()), Input.Value)) return false;
	}
	Built.Instance->ProcessEvent(Built.Function, Actual.GetStructMemory());
	bool bPassed = true;
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		const FString Name = FString::Printf(TEXT("Out%d"), Index);
		FProperty* Property = FindFProperty<FProperty>(Built.Function, FName(*Name));
		if (!Test.TestNotNull(Label + TEXT(" reflected output ") + Name, Property)) return false;
		void* ExpectedData = Property->ContainerPtrToValuePtr<void>(Expected.GetStructMemory());
		if (!WriteNative(Test, Label + TEXT(" expected ") + Name, Property, ExpectedData, Outputs[Index])) return false;
		bPassed &= Test.TestTrue(Label + TEXT(" native output equals expected ") + Name,
			Property->Identical(Property->ContainerPtrToValuePtr<void>(Actual.GetStructMemory()), ExpectedData));
	}
	return bPassed;
}

bool Execute(FAutomationTestBase& Test, const FString& Label, const FString& Script,
	const TArray<FInput>& Inputs, const TArray<FNativeValue>& Outputs,
	const TArray<FInput>& SecondInputs = {}, const TArray<FNativeValue>& SecondOutputs = {},
	const TMap<FName, FString>& Defaults = {})
{
	FBlueprintFunction Built;
	if (!BuildFunction(Test, Label, Script, Inputs, Outputs.Num(), Built, Defaults)) return false;
	bool bPassed = Invoke(Test, Label, Built, Inputs, Outputs);
	if (!SecondInputs.IsEmpty()) bPassed &= Invoke(Test, Label + TEXT(" second call"), Built, SecondInputs, SecondOutputs);
	return bPassed;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintDefaultsTest, "UEmka.Editor.Advanced.Defaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintDefaultsTest::RunTest(const FString& Parameters)
{
	struct FCase { FString Type; FString Declaration; FString Literal; FNativeValue Expected; };
	const TArray<FCase> Cases = {
		{TEXT("int"), TEXT("const Seed = 6 * 7\n"), TEXT("Seed + 1"), Int(43)},
		{TEXT("int8"), {}, TEXT("-12"), Int(-12)}, {TEXT("int16"), {}, TEXT("-1234"), Int(-1234)},
		{TEXT("int32"), {}, TEXT("-1234567"), Int(-1234567)}, {TEXT("uint8"), {}, TEXT("255"), Int(255)},
		{TEXT("uint16"), {}, TEXT("65535"), Int(65535)}, {TEXT("uint32"), {}, TEXT("4294967295"), Int(4294967295LL)},
		{TEXT("uint"), {}, TEXT("18446744073709551615"), Int(-1)}, {TEXT("bool"), {}, TEXT("true"), Bool(true)},
		{TEXT("char"), {}, TEXT("'Z'"), Int(90)}, {TEXT("real"), {}, TEXT("1.25"), Real(1.25)},
		{TEXT("real32"), {}, TEXT("-3.5"), Real(-3.5)},
		{TEXT("str"), {}, TEXT("\"Zażółć 世界 🚀\\n\\t\\\"\\\\\""), Str(TEXT("Zażółć 世界 🚀\n\t\"\\"))},
		{TEXT("Kind"), TEXT("type Kind = enum(uint32) { low = 7; high = 4294967295 }\n"), TEXT("Kind.high"), Int(4294967295LL)}
	};
	for (const FCase& Case : Cases)
	{
		const FString Script = Case.Declaration + FString::Printf(TEXT("fn Test*(Value: %s = %s): %s { return Value }"), *Case.Type, *Case.Literal, *Case.Type);
		Execute(*this, TEXT("Unconnected default ") + Case.Type, Script, {}, {Case.Expected});
	}
	const FString DefaultScript = TEXT("fn Test*(Value: int = 43): int { return Value }");
	Execute(*this, TEXT("Edited default overrides source"), DefaultScript, {}, {Int(99)}, {}, {}, {{FName(TEXT("Value")), TEXT("99")}});
	Execute(*this, TEXT("Linked input overrides source"), DefaultScript, {{TEXT("Value"), Int(-37)}}, {Int(-37)});
	for (const TCHAR* Type : {TEXT("[]int"), TEXT("[2]int")})
	{
		Execute(*this, FString(TEXT("Array default ")) + Type,
			FString::Printf(TEXT("fn Test*(Value: %s = %s{3, 5}): %s { return Value }"), Type, Type, Type), {}, {Array({Int(3), Int(5)})});
	}
	Execute(*this, TEXT("Nested comparable struct default"),
		TEXT("type Inner = struct { Count: int; Label: str }\ntype Row = struct { Detail: Inner; Flags: [2]bool }\n")
		TEXT("fn Test*(Value: Row = Row{Inner{7, \"世界\"}, [2]bool{true, false}}): Row { return Value }"),
		{}, {Int(7), Str(TEXT("世界")), Array({Bool(true), Bool(false)})});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintContainerFieldsTest, "UEmka.Editor.Advanced.ContainerFieldsAndTuples", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintContainerFieldsTest::RunTest(const FString& Parameters)
{
	for (const bool bStatic : {false, true})
	{
		const FString Type = bStatic ? TEXT("[2]int") : TEXT("[]int");
		const FNativeValue Values = Array({Int(MIN_int64), Int(MAX_int64)});
		const FNativeValue Reversed = Array({Int(MAX_int64), Int(MIN_int64)});
		Execute(*this, TEXT("Flat array field ") + Type,
			FString::Printf(TEXT("type Row = struct { Values: %s; Label: str }\nfn Test*(Input: Row): Row { return Input }"), *Type),
			{{TEXT("Input_Values"), Values}, {TEXT("Input_Label"), Str(TEXT("世界"))}}, {Values, Str(TEXT("世界"))},
			{{TEXT("Input_Values"), bStatic ? Reversed : Array({})}, {TEXT("Input_Label"), Str(TEXT(""))}}, {bStatic ? Reversed : Array({}), Str(TEXT(""))});
	}
	const FString Declaration = TEXT("type Inner = struct { Number: int; Label: str }\ntype Row = struct { Detail: Inner; Samples: []real32; Flag: bool }\n");
	const TArray<FInput> Inputs = {{TEXT("Input_Detail_Number"), Int(-9182)}, {TEXT("Input_Detail_Label"), Str(TEXT("nested 世界"))},
		{TEXT("Input_Samples"), Array({Real(-3.5), Real(1.25)})}, {TEXT("Input_Flag"), Bool(true)}};
	Execute(*this, TEXT("Nested fields retain declaration order"), Declaration + TEXT("fn Test*(Input: Row): Row { return Input }"),
		Inputs, {Int(-9182), Str(TEXT("nested 世界")), Array({Real(-3.5), Real(1.25)}), Bool(true)});
	Execute(*this, TEXT("Mixed tuple recursively flattens records"), Declaration + TEXT("fn Test*(Input: Row): (str, Row, int, Inner) { return \"prefix\", Input, 71, Inner{8, \"tail\"} }"),
		Inputs, {Str(TEXT("prefix")), Int(-9182), Str(TEXT("nested 世界")), Array({Real(-3.5), Real(1.25)}), Bool(true), Int(71), Int(8), Str(TEXT("tail"))});
	const FString LocalDeclaration = TEXT("type Row = struct { Number: int; Label: str }\n");
	Execute(*this, TEXT("Struct result locals avoid scalar parameter names"), LocalDeclaration + TEXT("fn Test*(__r: int, __r_: str): Row { return Row{__r, __r_} }"),
		{{TEXT("__r"), Int(39)}, {TEXT("__r_"), Str(TEXT("reserved 世界"))}}, {Int(39), Str(TEXT("reserved 世界"))});
	Execute(*this, TEXT("Tuple result locals avoid scalar parameter names"), LocalDeclaration + TEXT("fn Test*(__r0: int, Label: str): (Row, int) { return Row{__r0, Label}, __r0 + 1 }"),
		{{TEXT("__r0"), Int(44)}, {TEXT("Label"), Str(TEXT("tuple 世界"))}}, {Int(44), Str(TEXT("tuple 世界")), Int(45)});
	const FString FixedFieldScript = TEXT("type Row = struct { Values: [2]int }\nfn Test*(Input: Row): Row { return Input }");
	FString Error;
	int32 ErrorLine = -1;
	if (!TestTrue(TEXT("Fixed field guard source compiles"), UUEmkaFunctionLibrary::CompileCheckScript(FixedFieldScript, Error, ErrorLine))) return false;
	const FUEmkaSignature FixedFieldSignature = UK2Node_UEmka::ParseScript(FixedFieldScript);
	FUEmkaScriptParam Result;
	AddExpectedError(TEXT("expects exactly 2 array elements"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Fixed flattened field rejects wrong element count"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		UK2Node_UEmka::GetEffectiveScript(FixedFieldScript, FixedFieldSignature), UK2Node_UEmka::GetEffectiveFunctionName(FixedFieldSignature),
		{UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {1}, true)}, EUEmkaValueType::Int, true, true, Result, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintMapsTest, "UEmka.Editor.Advanced.NativeMaps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintMapsTest::RunTest(const FString& Parameters)
{
	for (const UEmkaTests::FTypeCase& Case : UEmkaTests::GetTypeCases())
	{
		for (const bool bStringKey : {false, true})
		{
			const FString Type = FString::Printf(TEXT("map[%s]%s"), bStringKey ? TEXT("str") : TEXT("int"), *Case.Name);
			const FNativeValue Value = bStringKey ? Map({Str(TEXT("Zażółć 世界 🚀")), Scalar(Case.First), Str(TEXT("")), Scalar(Case.Second)})
				: Map({Int(MIN_int64), Scalar(Case.First), Int(MAX_int64), Scalar(Case.Second)});
			Execute(*this, TEXT("Native ") + Type, Case.Declaration + FString::Printf(TEXT("fn Test*(Input: %s): %s { return Input }"), *Type, *Type),
				{{TEXT("Input"), Value}}, {Value}, {{TEXT("Input"), Map({})}}, {Map({})});
		}
	}
	const FNativeValue Counts = Map({Str(TEXT("one")), Int(1), Str(TEXT("世界")), Int(-1)});
	Execute(*this, TEXT("Map inside flattened struct"), TEXT("type Row = struct { Counts: map[str]int; Number: int }\nfn Test*(Input: Row): Row { return Input }"),
		{{TEXT("Input_Counts"), Counts}, {TEXT("Input_Number"), Int(17)}}, {Counts, Int(17)});
	for (const UEmkaTests::FTypeCase& Case : UEmkaTests::GetTypeCases())
	{
		if (Case.Type > EUEmkaValueType::UInt) continue;
		const FString Type = FString::Printf(TEXT("map[%s]str"), *Case.Name);
		const FNativeValue Value = Map({Scalar(Case.First), Str(TEXT("first 世界")), Scalar(Case.Second), Str(TEXT("second"))});
		Execute(*this, TEXT("Integer-base keys ") + Type, Case.Declaration + FString::Printf(TEXT("fn Test*(Input: %s): %s { return Input }"), *Type, *Type), {{TEXT("Input"), Value}}, {Value});
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintRecordArraysTest, "UEmka.Editor.Advanced.NativeRecordArrays", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintRecordArraysTest::RunTest(const FString& Parameters)
{
	const FString Declaration = TEXT("type Kind = enum(uint32) { low = 0; high = 4294967295 }\n")
		TEXT("type Inner = struct { Label: str; Enabled: bool }\n")
		TEXT("type Row = struct { Number: int; Bits: uint; Kind: Kind; Detail: Inner; Samples: []real32; Fixed: [2]uint8; Counts: map[str]int }\n");
	const TArray<FString> Names = {TEXT("Number"), TEXT("Bits"), TEXT("Kind"), TEXT("Detail"), TEXT("Samples"), TEXT("Fixed"), TEXT("Counts")};
	const FNativeValue First = Record(Names, {Int(MIN_int64), Int(-1), Int(4294967295LL), Record({TEXT("Label"), TEXT("Enabled")}, {Str(TEXT("Zażółć 世界 🚀")), Bool(true)}),
		Array({Real(-3.5), Real(1.25)}), Array({Int(0), Int(255)}), Map({Str(TEXT("世界")), Int(-19)})});
	const FNativeValue Second = Record(Names, {Int(MAX_int64), Int(0), Int(0), Record({TEXT("Label"), TEXT("Enabled")}, {Str(TEXT("")), Bool(false)}),
		Array({}), Array({Int(255), Int(0)}), Map({})});
	for (const bool bStatic : {false, true})
	{
		const FString Type = bStatic ? TEXT("[2]Row") : TEXT("[]Row");
		Execute(*this, TEXT("Native record array identity ") + Type,
			Declaration + FString::Printf(TEXT("fn Test*(Input: %s): %s { return Input }"), *Type, *Type),
			{{TEXT("Input"), Array({First, Second})}}, {Array({First, Second})},
			{{TEXT("Input"), bStatic ? Array({Second, First}) : Array({})}}, {bStatic ? Array({Second, First}) : Array({})});
		Execute(*this, TEXT("Native record array reversed by Umka ") + Type,
			Declaration + FString::Printf(TEXT("fn Test*(Input: %s): %s { return %s{Input[1], Input[0]} }"), *Type, *Type, *Type),
			{{TEXT("Input"), Array({First, Second})}}, {Array({Second, First})});
	}
	Execute(*this, TEXT("Record array inside flattened struct and tuple"), Declaration + TEXT("type Batch = struct { Rows: []Row; Count: int }\n")
		TEXT("fn Test*(Input: Batch): (int, Batch) { return 73, Input }"),
		{{TEXT("Input_Rows"), Array({First, Second})}, {TEXT("Input_Count"), Int(2)}}, {Int(73), Array({First, Second}), Int(2)});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaCompositeGuardsTest, "UEmka.Editor.Advanced.CompositeGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaCompositeGuardsTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Row = struct { Number: uint8; Enabled: bool; Label: str }\n")
		TEXT("fn Test*(Rows: []Row, Wide: int, Text: str): []Row { return Rows }");
	const TArray<FInput> Inputs = {{TEXT("Rows"), Array({Record({TEXT("Number"), TEXT("Enabled"), TEXT("Label")}, {Int(7), Bool(true), Str(TEXT("世界"))})})},
		{TEXT("Wide"), Int(256)}, {TEXT("Text"), Str(TEXT("wrong shape"))}};
	FBlueprintFunction Built;
	if (!BuildFunction(*this, TEXT("Codec guard native properties"), Script, Inputs, 1, Built)) return false;
	FStructOnScope Source(Built.Function);
	FStructOnScope Destination(Built.Function);
	for (const FInput& Input : Inputs)
	{
		FProperty* Property = FindFProperty<FProperty>(Built.Function, FName(*Input.Name));
		if (!TestNotNull(TEXT("Native codec input"), Property)
			|| !WriteNative(*this, Input.Name, Property, Property->ContainerPtrToValuePtr<void>(Source.GetStructMemory()), Input.Value)) return false;
	}
	FArrayProperty* Rows = FindFProperty<FArrayProperty>(Built.Function, TEXT("Rows"));
	FProperty* Wide = FindFProperty<FProperty>(Built.Function, TEXT("Wide"));
	FProperty* Text = FindFProperty<FProperty>(Built.Function, TEXT("Text"));
	if (!TestNotNull(TEXT("Native row array"), Rows) || !TestNotNull(TEXT("Native wide integer"), Wide) || !TestNotNull(TEXT("Native string"), Text)) return false;
	FString Error;
	FUEmkaScriptParam Packet;
	if (!TestTrue(TEXT("Encode native array"), UUEmkaFunctionLibrary::EncodeComposite(Rows, Rows->ContainerPtrToValuePtr<void>(Source.GetStructMemory()), Packet, Error))) return false;
	TestEqual(TEXT("Encoded packet has a composite result marker"), Packet.Type, EUEmkaValueType::Composite);
	TestTrue(TEXT("Decode valid native array"), UUEmkaFunctionLibrary::DecodeComposite(Packet, Rows, Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()), Error));
	TestTrue(TEXT("Codec round trip preserves native data"), Rows->Identical(Rows->ContainerPtrToValuePtr<void>(Source.GetStructMemory()), Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()), PPF_None));
	TestFalse(TEXT("Missing decode property rejected"), UUEmkaFunctionLibrary::DecodeComposite(Packet, nullptr, Destination.GetStructMemory(), Error));
	TestFalse(TEXT("Missing decode memory rejected"), UUEmkaFunctionLibrary::DecodeComposite(Packet, Rows, nullptr, Error));
	for (int32 Length = 1; Length < Packet.CompositeValue.Num(); ++Length)
	{
		FUEmkaScriptParam Truncated = Packet;
		Truncated.CompositeValue.SetNum(Length);
		TestFalse(FString::Printf(TEXT("Truncated packet length %d rejected"), Length), UUEmkaFunctionLibrary::DecodeComposite(Truncated, Rows, Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()), Error));
		TestFalse(TEXT("Truncated packet reports its error"), Error.IsEmpty());
	}
	FUEmkaScriptParam Trailing = Packet;
	Trailing.CompositeValue.Add(0);
	TestFalse(TEXT("Trailing bytes rejected"), UUEmkaFunctionLibrary::DecodeComposite(Trailing, Rows, Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()), Error));
	FUEmkaScriptParam BadTag = Packet;
	BadTag.CompositeValue[0] = 255;
	TestFalse(TEXT("Unknown packet tag rejected"), UUEmkaFunctionLibrary::DecodeComposite(BadTag, Rows, Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()), Error));
	FUEmkaScriptParam BadLength = Packet;
	const int32 ImpossibleCount = MAX_int32;
	FMemory::Memcpy(BadLength.CompositeValue.GetData() + 1, &ImpossibleCount, sizeof(ImpossibleCount));
	TestFalse(TEXT("Excessive container length rejected"), UUEmkaFunctionLibrary::DecodeComposite(BadLength, Rows, Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()), Error));
	FUEmkaScriptParam ScalarPacket;
	TestTrue(TEXT("Encode native wide integer"), UUEmkaFunctionLibrary::EncodeComposite(Wide, Wide->ContainerPtrToValuePtr<void>(Source.GetStructMemory()), ScalarPacket, Error));
	FStructProperty* Row = CastField<FStructProperty>(Rows->Inner);
	if (!TestNotNull(TEXT("Native row element"), Row)) return false;
	FScriptArrayHelper DestinationRows(Rows, Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()));
	DestinationRows.Resize(1);
	FProperty* Number = nullptr;
	FProperty* Enabled = nullptr;
	for (TFieldIterator<FProperty> It(Row->Struct); It; ++It)
	{
		if (It->GetAuthoredName() == TEXT("Number")) Number = *It;
		if (It->GetAuthoredName() == TEXT("Enabled")) Enabled = *It;
	}
	if (!TestNotNull(TEXT("Native byte field"), Number) || !TestNotNull(TEXT("Native bool field"), Enabled)) return false;
	TestFalse(TEXT("Integer above reflected byte range rejected"), UUEmkaFunctionLibrary::DecodeComposite(ScalarPacket, Number, Number->ContainerPtrToValuePtr<void>(DestinationRows.GetRawPtr(0)), Error));
	TestFalse(TEXT("Invalid Boolean integer rejected"), UUEmkaFunctionLibrary::DecodeComposite(ScalarPacket, Enabled, Enabled->ContainerPtrToValuePtr<void>(DestinationRows.GetRawPtr(0)), Error));
	FUEmkaScriptParam StringPacket;
	TestTrue(TEXT("Encode native string"), UUEmkaFunctionLibrary::EncodeComposite(Text, Text->ContainerPtrToValuePtr<void>(Source.GetStructMemory()), StringPacket, Error));
	TestFalse(TEXT("Scalar cannot decode as array"), UUEmkaFunctionLibrary::DecodeComposite(StringPacket, Rows, Rows->ContainerPtrToValuePtr<void>(Destination.GetStructMemory()), Error));
	TestFalse(TEXT("String cannot decode as byte"), UUEmkaFunctionLibrary::DecodeComposite(StringPacket, Number, Number->ContainerPtrToValuePtr<void>(DestinationRows.GetRawPtr(0)), Error));

	FUEmkaScriptParam WrongLength = Packet;
	WrongLength.bIsStaticArray = true;
	AddExpectedError(TEXT("Composite array does not match the compiled array shape"), EAutomationExpectedErrorFlags::Contains, 1);
	FUEmkaScriptParam Result;
	TestFalse(TEXT("Fixed record array rejects wrong element count"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		TEXT("type Row = struct { Number: uint8; Enabled: bool; Label: str }\nfn Test*(Rows: [2]Row): [2]Row { return Rows }"), TEXT("Test"),
		{WrongLength}, EUEmkaValueType::Composite, true, true, Result, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeContainerPersistenceTest, "UEmka.Editor.Advanced.NativeContainerPackageRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeContainerPersistenceTest::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Temp/Automation/UEmka/UEmkaNativeContainers_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	const FString Script = TEXT("type Detail = struct { Label: str; Active: bool }\n")
		TEXT("type Row = struct { Number: int; Info: Detail; Names: []str; Counts: map[int]str }\n")
		TEXT("fn Test*(Rows: []Row, Scores: map[str]real): ([]Row, map[str]real) { return Rows, Scores }");
	const FNativeValue RowsValue = Array({Record({TEXT("Number"), TEXT("Info"), TEXT("Names"), TEXT("Counts")}, {
		Int(-73), Record({TEXT("Label"), TEXT("Active")}, {Str(TEXT("Zażółć 世界 🚀")), Bool(true)}),
		Array({Str(TEXT("first")), Str(TEXT("世界"))}), Map({Int(MIN_int64), Str(TEXT("saved")), Int(MAX_int64), Str(TEXT("loaded"))})})});
	const FNativeValue ScoresValue = Map({Str(TEXT("世界")), Real(-1234.125), Str(TEXT("")), Real(1.25)});
	const TArray<FInput> Inputs = {{TEXT("Rows"), RowsValue}, {TEXT("Scores"), ScoresValue}};
	FBlueprintFunction Built;
	if (!BuildFunction(*this, TEXT("Native package source"), Script, Inputs, 2, Built, {}, Package)) return false;
	if (!Invoke(*this, TEXT("Native package before save"), Built, Inputs, {RowsValue, ScoresValue})) return false;
	Built.Blueprint->SetFlags(RF_Public | RF_Standalone);
	const FName AssetName = Built.Blueprint->GetFName();
	UEdGraphPin* RowsPin = Built.Node->FindPin(TEXT("Rows"), EGPD_Input);
	UUserDefinedStruct* RecordType = RowsPin ? Cast<UUserDefinedStruct>(RowsPin->PinType.PinSubCategoryObject.Get()) : nullptr;
	if (!TestNotNull(TEXT("Record pin references a generated native type"), RecordType)) return false;
	const FName RecordName = RecordType->GetFName();
	TestEqual(TEXT("Native record export belongs to saved asset package"), RecordType->GetOuter(), static_cast<UObject*>(Package));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/UEmka");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Filename = Directory / (FPaths::GetCleanFilename(PackageName) + TEXT(".uasset"));
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("Compiled native-container Blueprint saves"), UPackage::SavePackage(Package, Built.Blueprint, *Filename, SaveArgs))) return false;
	Package->Rename(*(PackageName + TEXT("_Original")), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
	UPackage* LoadedPackage = LoadPackage(nullptr, *Filename, LOAD_None);
	FBlueprintFunction Loaded;
	Loaded.Blueprint = LoadedPackage ? FindObject<UBlueprint>(LoadedPackage, *AssetName.ToString()) : nullptr;
	UUserDefinedStruct* LoadedRecord = LoadedPackage ? FindObject<UUserDefinedStruct>(LoadedPackage, *RecordName.ToString()) : nullptr;
	bool bPassed = TestNotNull(TEXT("Compiled native-container Blueprint reloads"), Loaded.Blueprint)
		&& TestNotNull(TEXT("Referenced native record export reloads"), LoadedRecord);
	if (bPassed)
	{
		TestEqual(TEXT("Saved asset reloads under its serialized package name"), LoadedPackage->GetName(), PackageName);
		UEdGraph* LoadedGraph = nullptr;
		for (UEdGraph* Graph : Loaded.Blueprint->FunctionGraphs)
		{
			if (Graph->GetFName() == FName(TEXT("ExecuteAdvanced"))) LoadedGraph = Graph;
		}
		bPassed = TestNotNull(TEXT("Native function graph reloads"), LoadedGraph);
		if (bPassed)
		{
			TArray<UK2Node_UEmka*> Nodes;
			LoadedGraph->GetNodesOfClass(Nodes);
			bPassed = TestEqual(TEXT("Native script node reloads"), Nodes.Num(), 1);
			if (bPassed)
			{
				Loaded.Node = Nodes[0];
				UEdGraphPin* LoadedRowsPin = Loaded.Node->FindPin(TEXT("Rows"), EGPD_Input);
				UEdGraphPin* LoadedScoresPin = Loaded.Node->FindPin(TEXT("Scores"), EGPD_Input);
				bPassed = TestNotNull(TEXT("Native array pin reloads"), LoadedRowsPin) && TestNotNull(TEXT("Native map pin reloads"), LoadedScoresPin);
				if (bPassed)
				{
					TestEqual(TEXT("Reloaded array pin references its loaded package export"), LoadedRowsPin->PinType.PinSubCategoryObject.Get(), static_cast<UObject*>(LoadedRecord));
					TestEqual(TEXT("Reloaded array pin preserves its container"), LoadedRowsPin->PinType.ContainerType, EPinContainerType::Array);
					TestEqual(TEXT("Reloaded map pin preserves its container"), LoadedScoresPin->PinType.ContainerType, EPinContainerType::Map);
				}
			}
		}
		UClass* GeneratedClass = Loaded.Blueprint->GeneratedClass;
		bPassed &= TestNotNull(TEXT("Compiled generated class reloads"), GeneratedClass);
		if (GeneratedClass)
		{
			Loaded.Function = GeneratedClass->FindFunctionByName(TEXT("ExecuteAdvanced"));
			bPassed &= TestNotNull(TEXT("Compiled native function reloads"), Loaded.Function);
			if (Loaded.Function)
			{
				FArrayProperty* LoadedRows = FindFProperty<FArrayProperty>(Loaded.Function, TEXT("Rows"));
				FStructProperty* LoadedElement = LoadedRows ? CastField<FStructProperty>(LoadedRows->Inner) : nullptr;
				bPassed &= TestNotNull(TEXT("Generated function retains native record-array property"), LoadedElement);
				if (LoadedElement)
				{
					TestEqual(TEXT("Compiled property references the loaded native record"), LoadedElement->Struct.Get(), static_cast<UScriptStruct*>(LoadedRecord));
					TestEqual(TEXT("Compiled native record belongs to loaded package"), LoadedElement->Struct->GetOuter(), static_cast<UObject*>(LoadedPackage));
				}
				bPassed &= TestNotNull(TEXT("Generated function retains native map property"), FindFProperty<FMapProperty>(Loaded.Function, TEXT("Scores")));
				FArrayProperty* LoadedOutput = FindFProperty<FArrayProperty>(Loaded.Function, TEXT("Out0"));
				FStructProperty* LoadedOutputElement = LoadedOutput ? CastField<FStructProperty>(LoadedOutput->Inner) : nullptr;
				bPassed &= TestNotNull(TEXT("Generated output retains native record-array property"), LoadedOutputElement);
				if (LoadedOutputElement) TestEqual(TEXT("Compiled output references the loaded native record"), LoadedOutputElement->Struct.Get(), static_cast<UScriptStruct*>(LoadedRecord));
				bPassed &= TestNotNull(TEXT("Generated output retains native map property"), FindFProperty<FMapProperty>(Loaded.Function, TEXT("Out1")));
				if (bPassed)
				{
					Loaded.Instance = NewObject<UObject>(GetTransientPackage(), GeneratedClass);
					bPassed &= Invoke(*this, TEXT("Loaded native package executes"), Loaded, Inputs, {RowsValue, ScoresValue});
					bPassed &= Invoke(*this, TEXT("Loaded native package handles empty values"), Loaded,
						{{TEXT("Rows"), Array({})}, {TEXT("Scores"), Map({})}}, {Array({}), Map({})});
				}
			}
		}
		Loaded.Blueprint->ClearFlags(RF_Standalone);
	}
	Built.Blueprint->ClearFlags(RF_Standalone);
	IFileManager::Get().Delete(*Filename);
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeStructExecutionTest, "UEmka.Editor.Advanced.NativeStructPins", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeStructExecutionTest::RunTest(const FString& Parameters)
{
	const FString Declaration = TEXT("type Detail = struct { Label: str; Weight: real32 }\n")
		TEXT("type Row = struct { Id: int8; Info: Detail; Flags: [2]bool; Children: []Detail; Entries: map[str]Detail }\n");
	const FNativeValue Detail = Record({TEXT("Label"), TEXT("Weight")}, {Str(TEXT("世界 🚀")), Real(1.25)});
	const FNativeValue Row = Record({TEXT("Id"), TEXT("Info"), TEXT("Flags"), TEXT("Children"), TEXT("Entries")},
		{Int(-7), Detail, Array({Bool(true), Bool(false)}), Array({Detail}), Map({Str(TEXT("first")), Detail})});
	const FNativeValue Empty = Record({TEXT("Id"), TEXT("Info"), TEXT("Flags"), TEXT("Children"), TEXT("Entries")},
		{Int(0), Record({TEXT("Label"), TEXT("Weight")}, {Str(TEXT("")), Real(0)}), Array({Bool(false), Bool(false)}), Array({}), Map({})});
	const auto Configure = [](UK2Node_UEmka* Node) { Node->bNativeStructPins = true; Node->SelectedFunction = TEXT("Run"); };
	for (const bool bTuple : {false, true})
	{
		const FString Script = Declaration + TEXT("fn First*(): int { return -1 }\n")
			+ (bTuple ? TEXT("fn Run*(Input: Row): (Row, int, Row) { return Input, 17, Input }")
				: TEXT("fn Run*(Input: Row): Row { return Input }"));
		FBlueprintFunction Built;
		if (!BuildFunction(*this, TEXT("Native whole record"), Script, {{TEXT("Input"), Row}}, bTuple ? 3 : 1, Built, {}, GetTransientPackage(), Configure)) continue;
		TestFalse(TEXT("Native records avoid generated flattening wrappers"), UK2Node_UEmka::ParseScript(Script, TEXT("Run"), true).bNeedsShim);
		Invoke(*this, TEXT("Populated native record"), Built, {{TEXT("Input"), Row}}, bTuple ? TArray<FNativeValue>{Row, Int(17), Row} : TArray<FNativeValue>{Row});
		Invoke(*this, TEXT("Empty native record containers"), Built, {{TEXT("Input"), Empty}}, bTuple ? TArray<FNativeValue>{Empty, Int(17), Empty} : TArray<FNativeValue>{Empty});
	}
	for (const bool bMap : {false, true})
	{
		const FString Type = bMap ? TEXT("map[str]Row") : TEXT("[]Row");
		FBlueprintFunction Built;
		if (BuildFunction(*this, TEXT("Unconnected native container"), Declaration
			+ FString::Printf(TEXT("fn Run*(Input: %s): %s { return Input }"), *Type, *Type), {}, 1, Built, {}, GetTransientPackage(), Configure))
		{
			Invoke(*this, TEXT("Empty literal native container"), Built, {}, {bMap ? Map({}) : Array({})});
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeStructDefaultsExecutionTest, "UEmka.Editor.Advanced.NativeStructDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeStructDefaultsExecutionTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Child = struct { Label: str }\n")
		TEXT("type Row = struct { Number: int; Detail: Child; Samples: [2]int }\n")
		TEXT("fn Run*(Input: Row = Row{73, Child{\"compiler\"}, [2]int{3, 5}}): Row { return Input }");
	const FNativeValue Original = Record({TEXT("Number"), TEXT("Detail"), TEXT("Samples")},
		{Int(73), Record({TEXT("Label")}, {Str(TEXT("compiler"))}), Array({Int(3), Int(5)})});
	const FNativeValue Edited = Record({TEXT("Number"), TEXT("Detail"), TEXT("Samples")},
		{Int(73), Record({TEXT("Label")}, {Str(TEXT("edited"))}), Array({Int(3), Int(5)})});
	for (const bool bSplit : {false, true})
	{
		FBlueprintFunction Built;
		if (!BuildFunction(*this, TEXT("Native record compiler default"), Script, {}, 1, Built, {}, GetTransientPackage(),
			[](UK2Node_UEmka* Node) { Node->bNativeStructPins = true; })) continue;
		Invoke(*this, TEXT("Unconnected native compiler default"), Built, {}, {Original});
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		UEdGraphPin* Input = Built.Node->FindPinChecked(TEXT("Input"), EGPD_Input);
		Schema->TrySetDefaultValue(*Input, Input->DefaultValue.Replace(TEXT("compiler"), TEXT("edited")));
		if (bSplit)
		{
			Schema->SplitPin(Input, false);
			TestFalse(TEXT("Native input can split into standard Blueprint fields"), Input->SubPins.IsEmpty());
		}
		FCompilerResultsLog Log;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Built.Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Built.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
		if (!TestEqual(TEXT("Edited native default compiles"), Log.NumErrors, 0)) continue;
		Built.Function = Built.Blueprint->GeneratedClass->FindFunctionByName(TEXT("ExecuteAdvanced"));
		Built.Instance = NewObject<UObject>(GetTransientPackage(), Built.Blueprint->GeneratedClass);
		Invoke(*this, bSplit ? TEXT("Split native default executes") : TEXT("Edited native default executes"), Built, {}, {Edited});
	}
	return true;
}

#endif
