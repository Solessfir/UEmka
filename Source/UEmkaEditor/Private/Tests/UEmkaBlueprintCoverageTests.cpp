// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UEmkaCoverageCases.h"
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
struct FInputValue
{
	FString Name;
	FUEmkaScriptParam Value;
};

FEdGraphPinType ExpectedPinType(const FUEmkaScriptParam& Value)
{
	FEdGraphPinType PinType;
	switch (Value.Type)
	{
		case EUEmkaValueType::Int:
		case EUEmkaValueType::UInt:
		case EUEmkaValueType::UInt32: PinType.PinCategory = UEdGraphSchema_K2::PC_Int64; break;
		case EUEmkaValueType::Bool: PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; break;
		case EUEmkaValueType::Char:
		case EUEmkaValueType::UInt8: PinType.PinCategory = UEdGraphSchema_K2::PC_Byte; break;
		case EUEmkaValueType::Real:
		case EUEmkaValueType::Real32:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = Value.Type == EUEmkaValueType::Real ? UEdGraphSchema_K2::PC_Double : UEdGraphSchema_K2::PC_Float;
			break;
		case EUEmkaValueType::Str: PinType.PinCategory = UEdGraphSchema_K2::PC_String; break;
		default: PinType.PinCategory = UEdGraphSchema_K2::PC_Int; break;
	}
	PinType.ContainerType = Value.bIsArray ? EPinContainerType::Array : EPinContainerType::None;
	return PinType;
}

FUEmkaScriptParam MakeArray(const UEmkaTests::FTypeCase& Case, const bool bStatic, const bool bReverse = false)
{
	FUEmkaScriptParam Value;
	Value.Type = Case.Type;
	Value.bIsArray = true;
	Value.bIsStaticArray = bStatic;
	for (const FUEmkaScriptParam& Element : {bReverse ? Case.Second : Case.First, bReverse ? Case.First : Case.Second})
	{
		switch (Case.Type)
		{
			case EUEmkaValueType::Real: Value.RealArrayValue.Add(Element.RealValue); break;
			case EUEmkaValueType::Real32: Value.Real32ArrayValue.Add(Element.Real32Value); break;
			case EUEmkaValueType::Str: Value.StringArrayValue.Add(Element.StringValue); break;
			default: Value.IntArrayValue.Add(Element.IntValue); break;
		}
	}
	return Value;
}

int32 ArrayLength(const FUEmkaScriptParam& Value)
{
	switch (Value.Type)
	{
		case EUEmkaValueType::Real: return Value.RealArrayValue.Num();
		case EUEmkaValueType::Real32: return Value.Real32ArrayValue.Num();
		case EUEmkaValueType::Str: return Value.StringArrayValue.Num();
		default: return Value.IntArrayValue.Num();
	}
}

FUEmkaScriptParam ArrayElement(const FUEmkaScriptParam& Value, const int32 Index)
{
	FUEmkaScriptParam Element;
	Element.Type = Value.Type;
	switch (Value.Type)
	{
		case EUEmkaValueType::Real: Element.RealValue = Value.RealArrayValue[Index]; break;
		case EUEmkaValueType::Real32: Element.Real32Value = Value.Real32ArrayValue[Index]; break;
		case EUEmkaValueType::Str: Element.StringValue = Value.StringArrayValue[Index]; break;
		default: Element.IntValue = Value.IntArrayValue[Index]; break;
	}
	return Element;
}

bool WriteScalar(FAutomationTestBase& Test, const FString& Label, FProperty* Property, void* Data, const FUEmkaScriptParam& Value)
{
	if (Value.Type == EUEmkaValueType::Bool)
	{
		FBoolProperty* Bool = CastField<FBoolProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" Boolean property"), Bool)) return false;
		Bool->SetPropertyValue(Data, Value.IntValue != 0);
	}
	else if (Value.Type == EUEmkaValueType::Str)
	{
		FStrProperty* String = CastField<FStrProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" string property"), String)) return false;
		String->SetPropertyValue(Data, Value.StringValue);
	}
	else
	{
		FNumericProperty* Number = CastField<FNumericProperty>(Property);
		if (!Test.TestNotNull(Label + TEXT(" numeric property"), Number)) return false;
		if (Value.Type == EUEmkaValueType::Real || Value.Type == EUEmkaValueType::Real32)
			Number->SetFloatingPointPropertyValue(Data, Value.Type == EUEmkaValueType::Real ? Value.RealValue : Value.Real32Value);
		else
			Number->SetIntPropertyValue(Data, Value.IntValue);
	}
	return true;
}

bool CheckScalar(FAutomationTestBase& Test, const FString& Label, FProperty* Property, const void* Data, const FUEmkaScriptParam& Expected)
{
	if (Expected.Type == EUEmkaValueType::Bool)
	{
		const FBoolProperty* Bool = CastField<FBoolProperty>(Property);
		return Test.TestNotNull(Label + TEXT(" Boolean property"), Bool)
			&& Test.TestEqual(Label, Bool->GetPropertyValue(Data), Expected.IntValue != 0);
	}
	if (Expected.Type == EUEmkaValueType::Str)
	{
		const FStrProperty* String = CastField<FStrProperty>(Property);
		return Test.TestNotNull(Label + TEXT(" string property"), String)
			&& Test.TestEqual(Label, String->GetPropertyValue(Data), Expected.StringValue);
	}
	const FNumericProperty* Number = CastField<FNumericProperty>(Property);
	if (!Test.TestNotNull(Label + TEXT(" numeric property"), Number)) return false;
	if (Expected.Type == EUEmkaValueType::Real || Expected.Type == EUEmkaValueType::Real32)
	{
		const double Value = Expected.Type == EUEmkaValueType::Real ? Expected.RealValue : Expected.Real32Value;
		return Test.TestTrue(Label + TEXT(" preserves floating-point value exactly"), Number->GetFloatingPointPropertyValue(Data) == Value);
	}
	return Test.TestEqual(Label, Number->GetSignedIntPropertyValue(Data), Expected.IntValue);
}

bool WriteValue(FAutomationTestBase& Test, const FString& Label, FProperty* Property, void* Data, const FUEmkaScriptParam& Value)
{
	if (!Value.bIsArray) return WriteScalar(Test, Label, Property, Data, Value);
	FArrayProperty* Array = CastField<FArrayProperty>(Property);
	if (!Test.TestNotNull(Label + TEXT(" array property"), Array)) return false;
	FScriptArrayHelper Helper(Array, Data);
	Helper.Resize(ArrayLength(Value));
	for (int32 Index = 0; Index < Helper.Num(); ++Index)
	{
		if (!WriteScalar(Test, Label, Array->Inner, Helper.GetRawPtr(Index), ArrayElement(Value, Index))) return false;
	}
	return true;
}

bool CheckValue(FAutomationTestBase& Test, const FString& Label, FProperty* Property, void* Data, const FUEmkaScriptParam& Expected)
{
	if (!Expected.bIsArray) return CheckScalar(Test, Label, Property, Data, Expected);
	FArrayProperty* Array = CastField<FArrayProperty>(Property);
	if (!Test.TestNotNull(Label + TEXT(" array property"), Array)) return false;
	FScriptArrayHelper Helper(Array, Data);
	if (!Test.TestEqual(Label + TEXT(" length"), Helper.Num(), ArrayLength(Expected))) return false;
	bool bPassed = true;
	for (int32 Index = 0; Index < Helper.Num(); ++Index)
	{
		bPassed &= CheckScalar(Test, FString::Printf(TEXT("%s[%d]"), *Label, Index), Array->Inner, Helper.GetRawPtr(Index), ArrayElement(Expected, Index));
	}
	return bPassed;
}

bool ExecuteBlueprint(FAutomationTestBase& Test, const FString& Label, const FString& Script,
	const TArray<FInputValue>& Inputs, const TArray<FUEmkaScriptParam>& Outputs,
	const TArray<FInputValue>& SecondInputs = {}, const TArray<FUEmkaScriptParam>& SecondOutputs = {})
{
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("UEmkaCoverage")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!Test.TestNotNull(Label + TEXT(" Blueprint"), Blueprint)) return false;
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("ExecuteTest"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (!Test.TestEqual(Label + TEXT(" entry count"), Entries.Num(), 1)) return false;
	UK2Node_FunctionEntry* Entry = Entries[0];
	UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
	if (!Test.TestNotNull(Label + TEXT(" result node"), Exit)) return false;
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = Script;
	Node->AllocateDefaultPins();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Test.TestTrue(Label + TEXT(" entry execution connected"), Schema->TryCreateConnection(Entry->GetThenPin(), Node->GetExecPin()))
		|| !Test.TestTrue(Label + TEXT(" result execution connected"), Schema->TryCreateConnection(Node->GetThenPin(), Exit->GetExecPin()))) return false;
	for (const FInputValue& Input : Inputs)
	{
		UEdGraphPin* Pin = Node->FindPin(FName(*Input.Name), EGPD_Input);
		if (!Test.TestNotNull(Label + TEXT(" input ") + Input.Name, Pin)) return false;
		const FEdGraphPinType Type = ExpectedPinType(Input.Value);
		if (!Test.TestTrue(Label + TEXT(" input pin type ") + Input.Name, Pin->PinType == Type)) return false;
		UEdGraphPin* EntryPin = Entry->CreateUserDefinedPin(FName(*Input.Name), Type, EGPD_Output);
		if (!Test.TestTrue(Label + TEXT(" input connected ") + Input.Name, Schema->TryCreateConnection(EntryPin, Pin))) return false;
	}
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		const FName PinName = Outputs.Num() == 1 ? FName(TEXT("ReturnValue")) : FName(*FString::Printf(TEXT("ReturnValue%d"), Index + 1));
		UEdGraphPin* Pin = Node->FindPin(PinName, EGPD_Output);
		if (!Test.TestNotNull(Label + TEXT(" output ") + PinName.ToString(), Pin)) return false;
		const FEdGraphPinType Type = ExpectedPinType(Outputs[Index]);
		if (!Test.TestTrue(Label + TEXT(" output pin type ") + PinName.ToString(), Pin->PinType == Type)) return false;
		UEdGraphPin* ExitPin = Exit->CreateUserDefinedPin(FName(*FString::Printf(TEXT("Out%d"), Index)), Type, EGPD_Input);
		if (!Test.TestTrue(Label + TEXT(" output connected ") + PinName.ToString(), Schema->TryCreateConnection(Pin, ExitPin))) return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FCompilerResultsLog CompileLog;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &CompileLog);
	if (!Test.TestEqual(Label + TEXT(" compile errors"), CompileLog.NumErrors, 0)
		|| !Test.TestTrue(Label + TEXT(" executable Blueprint"), Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)) return false;
	UFunction* Function = Blueprint->GeneratedClass->FindFunctionByName(Graph->GetFName());
	if (!Test.TestNotNull(Label + TEXT(" compiled function"), Function)) return false;
	UObject* Instance = NewObject<UObject>(GetTransientPackage(), Blueprint->GeneratedClass);
	auto Invoke = [&](const TArray<FInputValue>& Values, const TArray<FUEmkaScriptParam>& Expected)
	{
		FStructOnScope Params(Function);
		for (const FInputValue& Input : Values)
		{
			FProperty* Property = FindFProperty<FProperty>(Function, FName(*Input.Name));
			if (!Test.TestNotNull(Label + TEXT(" compiled input ") + Input.Name, Property)
				|| !WriteValue(Test, Label + TEXT(" ") + Input.Name, Property, Property->ContainerPtrToValuePtr<void>(Params.GetStructMemory()), Input.Value)) return false;
		}
		Instance->ProcessEvent(Function, Params.GetStructMemory());
		bool bPassed = true;
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			const FString Name = FString::Printf(TEXT("Out%d"), Index);
			FProperty* Property = FindFProperty<FProperty>(Function, FName(*Name));
			if (!Test.TestNotNull(Label + TEXT(" compiled output ") + Name, Property)) return false;
			bPassed &= CheckValue(Test, Label + TEXT(" ") + Name, Property, Property->ContainerPtrToValuePtr<void>(Params.GetStructMemory()), Expected[Index]);
		}
		return bPassed;
	};
	bool bPassed = Invoke(Inputs, Outputs);
	if (!SecondInputs.IsEmpty()) bPassed &= Invoke(SecondInputs, SecondOutputs);
	return bPassed;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintTypeCoverageTest, "UEmka.Editor.BlueprintTypeCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintTypeCoverageTest::RunTest(const FString& Parameters)
{
	for (const UEmkaTests::FTypeCase& Case : UEmkaTests::GetTypeCases())
	{
		for (const int32 Shape : {0, 1, 2})
		{
			const FString Type = (Shape == 0 ? TEXT("") : Shape == 1 ? TEXT("[]") : TEXT("[2]")) + Case.Name;
			const FUEmkaScriptParam First = Shape == 0 ? Case.First : MakeArray(Case, Shape == 2);
			FUEmkaScriptParam Second = Shape == 0 ? Case.Second : MakeArray(Case, Shape == 2, true);
			if (Shape == 1)
			{
				Second.IntArrayValue.Empty();
				Second.RealArrayValue.Empty();
				Second.Real32ArrayValue.Empty();
				Second.StringArrayValue.Empty();
			}
			const FString SingleScript = Case.Declaration + FString::Printf(TEXT("fn Test*(Value: %s): %s { return Value }"), *Type, *Type);
			ExecuteBlueprint(*this, Type + TEXT(" single"), SingleScript, {{TEXT("Value"), First}}, {First}, {{TEXT("Value"), Second}}, {Second});
			const FString MultiScript = Case.Declaration + FString::Printf(TEXT("fn Test*(Value: %s, Other: %s): (%s, %s) { return Value, Other }"), *Type, *Case.Name, *Type, *Case.Name);
			ExecuteBlueprint(*this, Type + TEXT(" tuple"), MultiScript, {{TEXT("Value"), First}, {TEXT("Other"), Case.Second}}, {First, Case.Second},
				{{TEXT("Value"), Second}, {TEXT("Other"), Case.First}}, {Second, Case.First});
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintStructCoverageTest, "UEmka.Editor.BlueprintStructCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintStructCoverageTest::RunTest(const FString& Parameters)
{
	const TArray<UEmkaTests::FTypeCase> Cases = UEmkaTests::GetTypeCases();
	for (const UEmkaTests::FTypeCase& Case : Cases)
	{
		const FString SingleFieldScript = Case.Declaration + FString::Printf(TEXT("type Record = struct { Value: %s }\nfn Test*(Input: Record): Record { return Input }"), *Case.Name);
		ExecuteBlueprint(*this, Case.Name + TEXT(" single-field struct"), SingleFieldScript, {{TEXT("Input_Value"), Case.First}}, {Case.First},
			{{TEXT("Input_Value"), Case.Second}}, {Case.Second});
	}
	// Flattened fields consume Umka parameters, including its hidden result and closure slots.
	constexpr int32 FieldsPerStruct = 7;
	for (int32 Start = 0; Start < Cases.Num(); Start += FieldsPerStruct)
	{
		FString Declarations;
		TArray<FString> Fields;
		TArray<FInputValue> Inputs;
		TArray<FInputValue> SecondInputs;
		TArray<FUEmkaScriptParam> Outputs;
		TArray<FUEmkaScriptParam> SecondOutputs;
		for (int32 Index = Start; Index < FMath::Min(Start + FieldsPerStruct, Cases.Num()); ++Index)
		{
			const UEmkaTests::FTypeCase& Case = Cases[Index];
			Declarations += Case.Declaration;
			const FString Field = FString::Printf(TEXT("Field%d"), Index);
			Fields.Add(Field + TEXT(": ") + Case.Name);
			Inputs.Add({TEXT("Input_") + Field, Case.First});
			SecondInputs.Add({TEXT("Input_") + Field, Case.Second});
			Outputs.Add(Case.First);
			SecondOutputs.Add(Case.Second);
		}
		Declarations += TEXT("type Record = struct { ") + FString::Join(Fields, TEXT("; ")) + TEXT(" }\n");
		ExecuteBlueprint(*this, FString::Printf(TEXT("Mixed flat struct %d"), Start / FieldsPerStruct),
			Declarations + TEXT("fn Test*(Input: Record): Record { return Input }"), Inputs, Outputs, SecondInputs, SecondOutputs);
	}

	const FString RecordDeclaration = TEXT("type Record = struct { Value: int; Flag: bool }\n");
	const FUEmkaScriptParam Number = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 123456789012345LL);
	const FUEmkaScriptParam Flag = UUEmkaFunctionLibrary::MakeBoolParam(true);
	const TArray<FInputValue> RecordInputs = {{TEXT("Input_Value"), Number}, {TEXT("Input_Flag"), Flag}};
	AddExpectedError(TEXT("Struct parameter was lost"), EAutomationExpectedErrorFlags::Contains, 1);
	ExecuteBlueprint(*this, TEXT("Struct parameter void"), RecordDeclaration +
		TEXT("fn Test*(Input: Record) { if Input.Value != 123456789012345 || !Input.Flag { exit(1, \"Struct parameter was lost\") } }"), RecordInputs, {},
		{{TEXT("Input_Value"), Number}, {TEXT("Input_Flag"), UUEmkaFunctionLibrary::MakeBoolParam(false)}}, {});
	ExecuteBlueprint(*this, TEXT("Struct parameter scalar"), RecordDeclaration + TEXT("fn Test*(Input: Record): int { if Input.Flag { return Input.Value }; return 0 }"),
		RecordInputs, {Number});
	ExecuteBlueprint(*this, TEXT("Struct parameter tuple"), RecordDeclaration + TEXT("fn Test*(Input: Record): (int, bool) { return Input.Value, Input.Flag }"),
		RecordInputs, {Number, Flag});
	for (const bool bStatic : {false, true})
	{
		FUEmkaScriptParam Array = UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {Number.IntValue, Number.IntValue}, bStatic);
		const FString Type = bStatic ? TEXT("[2]int") : TEXT("[]int");
		ExecuteBlueprint(*this, TEXT("Struct parameter ") + Type, RecordDeclaration +
			FString::Printf(TEXT("fn Test*(Input: Record): %s { return %s{Input.Value, Input.Value} }"), *Type, *Type), RecordInputs, {Array});
	}
	return true;
}

#endif
