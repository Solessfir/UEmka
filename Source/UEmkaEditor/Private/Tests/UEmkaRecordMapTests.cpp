// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

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
struct FValue
{
	enum class EKind { Integer, String, Array, Record, Map };
	EKind Kind = EKind::Integer;
	int64 Integer = 0;
	FString String;
	TArray<FString> Names;
	TArray<FValue> Children;
};

FValue Int(const int64 Value) { FValue Result; Result.Integer = Value; return Result; }
FValue Str(const FString& Value) { FValue Result; Result.Kind = FValue::EKind::String; Result.String = Value; return Result; }
FValue Array(TArray<FValue> Values) { FValue Result; Result.Kind = FValue::EKind::Array; Result.Children = MoveTemp(Values); return Result; }
FValue Map(TArray<FValue> Entries) { FValue Result; Result.Kind = FValue::EKind::Map; Result.Children = MoveTemp(Entries); return Result; }
FValue Record(TArray<FString> Names, TArray<FValue> Values)
{
	FValue Result;
	Result.Kind = FValue::EKind::Record;
	Result.Names = MoveTemp(Names);
	Result.Children = MoveTemp(Values);
	return Result;
}

const FString Declaration = TEXT("type Child = struct { Label: str }\n")
	TEXT("type Row = struct { Number: int8; Label: str; Detail: Child; Names: []str; Children: []Child; Counts: map[str]int }\n");

FValue Row(const int64 Number, const FString& Label)
{
	return Record({TEXT("Number"), TEXT("Label"), TEXT("Detail"), TEXT("Names"), TEXT("Children"), TEXT("Counts")}, {
		Int(Number), Str(Label), Record({TEXT("Label")}, {Str(Label + TEXT(" detail"))}),
		Array({Str(TEXT("")), Str(Label)}), Array({Record({TEXT("Label")}, {Str(Label + TEXT(" child"))})}),
		Map({Str(Label), Int(Number), Str(TEXT("")), Int(-Number)})});
}

bool WriteValue(FAutomationTestBase& Test, FProperty* Property, void* Data, const FValue& Value)
{
	if (Value.Kind == FValue::EKind::Integer)
	{
		FNumericProperty* Number = CastField<FNumericProperty>(Property);
		if (!Test.TestNotNull(TEXT("Integer property"), Number)) return false;
		Number->SetIntPropertyValue(Data, static_cast<uint64>(Value.Integer));
		return true;
	}
	if (Value.Kind == FValue::EKind::String)
	{
		FStrProperty* String = CastField<FStrProperty>(Property);
		if (!Test.TestNotNull(TEXT("String property"), String)) return false;
		String->SetPropertyValue(Data, Value.String);
		return true;
	}
	if (Value.Kind == FValue::EKind::Array)
	{
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
		if (!Test.TestNotNull(TEXT("Array property"), ArrayProperty)) return false;
		FScriptArrayHelper Helper(ArrayProperty, Data);
		Helper.Resize(Value.Children.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
			if (!WriteValue(Test, ArrayProperty->Inner, Helper.GetRawPtr(Index), Value.Children[Index])) return false;
		return true;
	}
	if (Value.Kind == FValue::EKind::Record)
	{
		FStructProperty* Struct = CastField<FStructProperty>(Property);
		if (!Test.TestNotNull(TEXT("Record property"), Struct)) return false;
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
		{
			const FString Name = It->GetAuthoredName();
			const int32 Index = Value.Names.IndexOfByPredicate([&Name](const FString& FieldName) { return FieldName.Equals(Name, ESearchCase::CaseSensitive); });
			if (!Test.TestTrue(TEXT("Record authored field ") + It->GetAuthoredName(), Value.Children.IsValidIndex(Index))) return false;
			if (!WriteValue(Test, *It, It->ContainerPtrToValuePtr<void>(Data), Value.Children[Index])) return false;
			++Count;
		}
		return Test.TestEqual(TEXT("Record field count"), Count, Value.Children.Num());
	}
	FMapProperty* MapProperty = CastField<FMapProperty>(Property);
	if (!Test.TestNotNull(TEXT("Map property"), MapProperty)
		|| !Test.TestEqual(TEXT("Map key/value pairs"), Value.Children.Num() % 2, 0)) return false;
	FScriptMapHelper Helper(MapProperty, Data);
	Helper.EmptyValues();
	for (int32 Index = 0; Index < Value.Children.Num(); Index += 2)
	{
		const int32 Entry = Helper.AddDefaultValue_Invalid_NeedsRehash();
		if (!WriteValue(Test, MapProperty->KeyProp, Helper.GetKeyPtr(Entry), Value.Children[Index])
			|| !WriteValue(Test, MapProperty->ValueProp, Helper.GetValuePtr(Entry), Value.Children[Index + 1])) return false;
	}
	Helper.Rehash();
	return true;
}

struct FBlueprintFunction
{
	UBlueprint* Blueprint = nullptr;
	UK2Node_UEmka* Node = nullptr;
	UFunction* Function = nullptr;
	UObject* Instance = nullptr;
	FName InputName;
};

bool BuildFunction(FAutomationTestBase& Test, const FString& Script, const FName InputName,
	FBlueprintFunction& Built, UPackage* Package = GetTransientPackage())
{
	FString Error;
	int32 Line = -1;
	if (!UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, Line))
	{
		Test.AddError(FString::Printf(TEXT("Record map source line %d: %s"), Line, *Error));
		return false;
	}
	Built.InputName = InputName;
	Built.Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("UEmkaRecordMap")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!Test.TestNotNull(TEXT("Record map Blueprint"), Built.Blueprint)) return false;
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Built.Blueprint, TEXT("ExecuteRecordMap"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Built.Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (!Test.TestEqual(TEXT("Function entry count"), Entries.Num(), 1)) return false;
	UK2Node_FunctionEntry* Entry = Entries[0];
	UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
	if (!Test.TestNotNull(TEXT("Function result"), Exit)) return false;
	Built.Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Built.Node);
	Built.Node->CreateNewGuid();
	Built.Node->Script = Script;
	Built.Node->AllocateDefaultPins();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Test.TestTrue(TEXT("Entry execution connected"), Schema->TryCreateConnection(Entry->GetThenPin(), Built.Node->GetExecPin()))
		|| !Test.TestTrue(TEXT("Result execution connected"), Schema->TryCreateConnection(Built.Node->GetThenPin(), Exit->GetExecPin()))) return false;
	UEdGraphPin* InputPin = Built.Node->FindPin(InputName, EGPD_Input);
	if (!Test.TestNotNull(TEXT("Native map input pin"), InputPin)) return false;
	if (!Test.TestTrue(TEXT("Native map container"), InputPin->PinType.ContainerType == EPinContainerType::Map)
		|| !Test.TestEqual(TEXT("Native record map terminal"), InputPin->PinType.PinValueType.TerminalCategory, UEdGraphSchema_K2::PC_Struct)) return false;
	UEdGraphPin* EntryPin = Entry->CreateUserDefinedPin(InputName, InputPin->PinType, EGPD_Output);
	if (!Test.TestTrue(TEXT("Native map input connected"), Schema->TryCreateConnection(EntryPin, InputPin))) return false;
	int32 Outputs = 0;
	for (UEdGraphPin* Pin : Built.Node->Pins)
	{
		if (Pin->Direction != EGPD_Output || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
		UEdGraphPin* ExitPin = Exit->CreateUserDefinedPin(TEXT("Output"), Pin->PinType, EGPD_Input);
		if (!Test.TestTrue(TEXT("Native map output connected"), Schema->TryCreateConnection(Pin, ExitPin))) return false;
		++Outputs;
	}
	if (!Test.TestEqual(TEXT("Native map output count"), Outputs, 1)) return false;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Built.Blueprint);
	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(Built.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
	if (!Test.TestEqual(TEXT("Record map Blueprint compile errors"), Log.NumErrors, 0)) return false;
	Built.Function = Built.Blueprint->GeneratedClass->FindFunctionByName(Graph->GetFName());
	if (!Test.TestNotNull(TEXT("Compiled native map function"), Built.Function)) return false;
	Built.Instance = NewObject<UObject>(GetTransientPackage(), Built.Blueprint->GeneratedClass);
	return true;
}

bool Invoke(FAutomationTestBase& Test, const FBlueprintFunction& Built, const FValue& Input, const FValue& Output)
{
	FStructOnScope Actual(Built.Function);
	FStructOnScope Expected(Built.Function);
	FProperty* InputProperty = FindFProperty<FProperty>(Built.Function, Built.InputName);
	FProperty* OutputProperty = FindFProperty<FProperty>(Built.Function, TEXT("Output"));
	if (!Test.TestNotNull(TEXT("Reflected map input"), InputProperty)
		|| !Test.TestNotNull(TEXT("Reflected map output"), OutputProperty)) return false;
	if (!WriteValue(Test, InputProperty, InputProperty->ContainerPtrToValuePtr<void>(Actual.GetStructMemory()), Input)
		|| !WriteValue(Test, OutputProperty, OutputProperty->ContainerPtrToValuePtr<void>(Expected.GetStructMemory()), Output)) return false;
	Built.Instance->ProcessEvent(Built.Function, Actual.GetStructMemory());
	return Test.TestTrue(TEXT("Native map records preserve all nested values"), OutputProperty->Identical(
		OutputProperty->ContainerPtrToValuePtr<void>(Actual.GetStructMemory()), OutputProperty->ContainerPtrToValuePtr<void>(Expected.GetStructMemory())));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintRecordMapsTest, "UEmka.Editor.Advanced.NativeRecordMaps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintRecordMapsTest::RunTest(const FString& Parameters)
{
	struct FCase { FString Declaration; FString KeyType; FValue FirstKey; FValue SecondKey; };
	const TArray<FCase> Cases = {
		{{}, TEXT("str"), Str(TEXT("Zażółć 世界 🚀")), Str(TEXT(""))},
		{{}, TEXT("int"), Int(MIN_int64), Int(MAX_int64)},
		{TEXT("type Kind = enum(uint32) { low = 7; high = 4294967295 }\n"), TEXT("Kind"), Int(7), Int(4294967295LL)}
	};
	for (const FCase& Case : Cases)
	{
		const FString MapType = FString::Printf(TEXT("map[%s]Row"), *Case.KeyType);
		FBlueprintFunction Built;
		if (!BuildFunction(*this, Case.Declaration + Declaration + FString::Printf(
			TEXT("fn Test*(Input: %s): %s { return Input }"), *MapType, *MapType), TEXT("Input"), Built)) continue;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const FValue Value = Map({Case.FirstKey, Row(-128 + Index, FString::Printf(TEXT("世界 %d"), Index)), Case.SecondKey, Row(127 - Index, TEXT("Zażółć 🚀"))});
			Invoke(*this, Built, Value, Value);
			Invoke(*this, Built, Map({}), Map({}));
		}
	}
	FBlueprintFunction Flattened;
	const FValue Value = Map({Str(TEXT("nested")), Row(-19, TEXT("nested 世界"))});
	if (BuildFunction(*this, Declaration + TEXT("type Wrapper = struct { Rows: map[str]Row }\nfn Test*(Input: Wrapper): Wrapper { return Input }"), TEXT("Input_Rows"), Flattened))
		Invoke(*this, Flattened, Value, Value);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintRecordMapFailureTest, "UEmka.Editor.Advanced.NativeRecordMapErrorReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintRecordMapFailureTest::RunTest(const FString& Parameters)
{
	FBlueprintFunction Built;
	if (!BuildFunction(*this, Declaration + TEXT("fn Test*(Input: map[str]Row): map[str]Row { return Input }"), TEXT("Input"), Built)) return false;
	FStructOnScope Data(Built.Function);
	FMapProperty* Input = FindFProperty<FMapProperty>(Built.Function, TEXT("Input"));
	FMapProperty* Output = FindFProperty<FMapProperty>(Built.Function, TEXT("Output"));
	if (!TestNotNull(TEXT("Native record map input"), Input) || !TestNotNull(TEXT("Native record map output"), Output)) return false;
	void* InputData = Input->ContainerPtrToValuePtr<void>(Data.GetStructMemory());
	void* OutputData = Output->ContainerPtrToValuePtr<void>(Data.GetStructMemory());
	if (!WriteValue(*this, Input, InputData, Map({Str(TEXT("valid")), Row(7, TEXT("valid 世界"))}))) return false;
	Built.Instance->ProcessEvent(Built.Function, Data.GetStructMemory());
	TestEqual(TEXT("Successful call populates record map output"), FScriptMapHelper(Output, OutputData).Num(), 1);
	if (!WriteValue(*this, Input, InputData, Map({Str(TEXT("invalid")), Row(128, TEXT("invalid 世界"))}))) return false;
	AddExpectedError(TEXT("Composite integer exceeds the compiled type range"), EAutomationExpectedErrorFlags::Contains, 1);
	Built.Instance->ProcessEvent(Built.Function, Data.GetStructMemory());
	TestEqual(TEXT("Failed nested record conversion clears the previous output"), FScriptMapHelper(Output, OutputData).Num(), 0);
	const FValue Recovered = Map({Str(TEXT("recovered")), Row(-128, TEXT("recovered 🚀"))});
	Invoke(*this, Built, Recovered, Recovered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaBlueprintRecordMapPersistenceTest, "UEmka.Editor.Advanced.NativeRecordMapPackageRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaBlueprintRecordMapPersistenceTest::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Temp/Automation/UEmka/UEmkaRecordMaps_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	FBlueprintFunction Built;
	if (!BuildFunction(*this, Declaration + TEXT("fn Test*(Input: map[str]Row): map[str]Row { return Input }"), TEXT("Input"), Built, Package)) return false;
	const FValue Value = Map({Str(TEXT("saved 世界")), Row(-73, TEXT("Zażółć 世界 🚀")), Str(TEXT("")), Row(127, TEXT("loaded"))});
	if (!Invoke(*this, Built, Value, Value)) return false;
	Built.Blueprint->SetFlags(RF_Public | RF_Standalone);
	const FName AssetName = Built.Blueprint->GetFName();
	UEdGraphPin* InputPin = Built.Node->FindPin(TEXT("Input"), EGPD_Input);
	UUserDefinedStruct* RecordType = InputPin ? Cast<UUserDefinedStruct>(InputPin->PinType.PinValueType.TerminalSubCategoryObject.Get()) : nullptr;
	if (!TestNotNull(TEXT("Map terminal has a generated record export"), RecordType)) return false;
	const FName RecordName = RecordType->GetFName();
	TestEqual(TEXT("Map record export belongs to the Blueprint package"), RecordType->GetOuter(), static_cast<UObject*>(Package));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/UEmka");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Filename = Directory / (FPaths::GetCleanFilename(PackageName) + TEXT(".uasset"));
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("Record map Blueprint package saves"), UPackage::SavePackage(Package, Built.Blueprint, *Filename, SaveArgs))) return false;
	Package->Rename(*(PackageName + TEXT("_Original")), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
	UPackage* LoadedPackage = LoadPackage(nullptr, *Filename, LOAD_None);
	FBlueprintFunction Loaded;
	Loaded.InputName = TEXT("Input");
	Loaded.Blueprint = LoadedPackage ? FindObject<UBlueprint>(LoadedPackage, *AssetName.ToString()) : nullptr;
	UUserDefinedStruct* LoadedRecord = LoadedPackage ? FindObject<UUserDefinedStruct>(LoadedPackage, *RecordName.ToString()) : nullptr;
	bool bPassed = TestNotNull(TEXT("Record map Blueprint reloads"), Loaded.Blueprint)
		&& TestNotNull(TEXT("Map value record export reloads"), LoadedRecord);
	if (bPassed)
	{
		TestEqual(TEXT("Reloaded package retains its authored path"), LoadedPackage->GetName(), PackageName);
		for (UEdGraph* Graph : Loaded.Blueprint->FunctionGraphs)
		{
			TArray<UK2Node_UEmka*> Nodes;
			Graph->GetNodesOfClass(Nodes);
			for (UK2Node_UEmka* Node : Nodes)
			{
				UEdGraphPin* Pin = Node->FindPin(TEXT("Input"), EGPD_Input);
				if (TestNotNull(TEXT("Saved map pin reloads"), Pin))
					TestEqual(TEXT("Saved map terminal references the loaded record export"), Pin->PinType.PinValueType.TerminalSubCategoryObject.Get(), static_cast<UObject*>(LoadedRecord));
			}
		}
		UClass* GeneratedClass = Loaded.Blueprint->GeneratedClass;
		bPassed = TestNotNull(TEXT("Compiled record map class reloads"), GeneratedClass);
		if (bPassed)
		{
			Loaded.Function = GeneratedClass->FindFunctionByName(TEXT("ExecuteRecordMap"));
			bPassed = TestNotNull(TEXT("Compiled record map function reloads"), Loaded.Function);
			if (bPassed)
			{
				FMapProperty* Input = FindFProperty<FMapProperty>(Loaded.Function, TEXT("Input"));
				FStructProperty* Item = Input ? CastField<FStructProperty>(Input->ValueProp) : nullptr;
				bPassed = TestNotNull(TEXT("Reloaded map retains native record values"), Item);
				if (bPassed)
				{
					TestEqual(TEXT("Reloaded map property uses the loaded record export"), Item->Struct.Get(), static_cast<UScriptStruct*>(LoadedRecord));
					Loaded.Instance = NewObject<UObject>(GetTransientPackage(), GeneratedClass);
					bPassed &= Invoke(*this, Loaded, Value, Value);
					bPassed &= Invoke(*this, Loaded, Map({}), Map({}));
				}
			}
		}
		Loaded.Blueprint->ClearFlags(RF_Standalone);
	}
	Built.Blueprint->ClearFlags(RF_Standalone);
	IFileManager::Get().Delete(*Filename);
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRecordMapFieldNamesTest, "UEmka.Editor.Advanced.NativeRecordMapFieldNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRecordMapFieldNamesTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Row = struct { First: int; Other: int }\nfn Test*(Input: map[str]Row): map[str]Row { return Input }");
	FBlueprintFunction Built;
	if (!BuildFunction(*this, Script, TEXT("Input"), Built)) return false;
	FStructOnScope Data(Built.Function);
	FStructOnScope Expected(Built.Function);
	FMapProperty* Input = FindFProperty<FMapProperty>(Built.Function, TEXT("Input"));
	if (!TestNotNull(TEXT("Native record map field-name fixture"), Input)) return false;
	void* InputData = Input->ContainerPtrToValuePtr<void>(Data.GetStructMemory());
	void* ExpectedData = Input->ContainerPtrToValuePtr<void>(Expected.GetStructMemory());
	const FValue Value = Map({Str(TEXT("entry")), Record({TEXT("First"), TEXT("Other")}, {Int(17), Int(-29)})});
	if (!WriteValue(*this, Input, InputData, Value)) return false;
	Input->CopyCompleteValue(ExpectedData, InputData);
	FString Error;
	FUEmkaScriptParam Packet;
	if (!TestTrue(TEXT("Record map field-name fixture encodes"), UUEmkaFunctionLibrary::EncodeComposite(Input, InputData, Packet, Error))) return false;
	struct FCase { const ANSICHAR* From; const ANSICHAR* To; const TCHAR* NativeError; };
	for (const FCase& Case : {
		FCase{"First", "first", TEXT("missing a compiled field")},
		FCase{"Other", "First", TEXT("duplicate fields")},
		FCase{"Other", "Third", TEXT("missing a compiled field")}})
	{
		FUEmkaScriptParam Malformed = Packet;
		const int32 NameLength = FCStringAnsi::Strlen(Case.From);
		int32 Offset = INDEX_NONE;
		int32 Matches = 0;
		for (int32 Index = 0; Index <= Malformed.CompositeValue.Num() - NameLength; ++Index)
		{
			if (FMemory::Memcmp(Malformed.CompositeValue.GetData() + Index, Case.From, NameLength) == 0)
			{
				Offset = Index;
				++Matches;
			}
		}
		if (!TestEqual(TEXT("Malformed fixture targets one field name"), Matches, 1)) return false;
		FMemory::Memcpy(Malformed.CompositeValue.GetData() + Offset, Case.To, NameLength);
		TestFalse(TEXT("Malformed reflected record field is rejected"), UUEmkaFunctionLibrary::DecodeComposite(Malformed, Input, InputData, Error));
		TestTrue(TEXT("Rejected field packet leaves reflected values intact"), Input->Identical(InputData, ExpectedData, 0));
		FUEmkaScriptParam Result;
		AddExpectedError(Case.NativeError, EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Malformed compiled record field is rejected"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"), {Malformed}, EUEmkaValueType::Composite, false, false, Result, Error));
		TestTrue(TEXT("Rejected field packet clears runtime result"), Result.CompositeValue.IsEmpty());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
