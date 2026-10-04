// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaRecordTypes.h"
#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UEmkaFunctionLibrary.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeRecordDefaultsTest, "UEmka.Editor.NativeRecordDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeRecordDefaultsTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Child = struct { Label: str }\n")
		TEXT("type Row = struct { Number: int; Label: str; Detail: Child; Samples: [2]int }\n")
		TEXT("fn Test*(Input: Row = Row{73, \"Zażółć 世界 🚀\", Child{\"nested 世界\"}, [2]int{3, 5}}): Row { return Input }");
	FUEmkaCompiledSignature Signature;
	if (!TestTrue(TEXT("Native default schema inspected"), UUEmkaFunctionLibrary::InspectScriptFunction(Script, TEXT("Test"), Signature))
		|| !TestEqual(TEXT("Native default parameter count"), Signature.Params.Num(), 1)) return false;
	const FUEmkaCompiledValue& Value = Signature.Params[0];
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/Automation/UEmka/UEmkaRecordDefault_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("UEmkaRecordDefault")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!TestNotNull(TEXT("Native default Blueprint owner"), Blueprint)) return false;
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Native default Blueprint graph"), Graph)) return false;
	FString Error;
	const FEdGraphPinType PinType = UEmkaRecordTypes::GetPinType(Value, Graph, Error);
	if (!TestTrue(TEXT("Native default type compiles"), Error.IsEmpty())) return false;
	UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get());
	if (!TestNotNull(TEXT("Native default record type"), Struct)) return false;
	FString DefaultValue;
	if (!TestTrue(TEXT("Compiler record default exports to Blueprint text"), UEmkaRecordTypes::GetDefaultValue(Value, PinType, DefaultValue, Error))) return false;
	TestTrue(TEXT("Record default export reports no error"), Error.IsEmpty());
	FStructOnScope Imported(Struct);
	if (!TestNotNull(TEXT("Blueprint record text imports"), Struct->ImportText(*DefaultValue, Imported.GetStructMemory(), nullptr, PPF_SerializedAsImportText, GLog, Struct->GetName()))) return false;
	FNumericProperty* Number = nullptr;
	FStrProperty* Label = nullptr;
	FStructProperty* Detail = nullptr;
	FArrayProperty* Samples = nullptr;
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (It->GetAuthoredName() == TEXT("Number")) Number = CastField<FNumericProperty>(*It);
		if (It->GetAuthoredName() == TEXT("Label")) Label = CastField<FStrProperty>(*It);
		if (It->GetAuthoredName() == TEXT("Detail")) Detail = CastField<FStructProperty>(*It);
		if (It->GetAuthoredName() == TEXT("Samples")) Samples = CastField<FArrayProperty>(*It);
	}
	if (!TestNotNull(TEXT("Native number field"), Number) || !TestNotNull(TEXT("Native string field"), Label)
		|| !TestNotNull(TEXT("Native nested default field"), Detail) || !TestNotNull(TEXT("Native default array field"), Samples)) return false;
	TestEqual(TEXT("Imported compiler number default"), Number->GetSignedIntPropertyValue(Number->ContainerPtrToValuePtr<void>(Imported.GetStructMemory())), int64{73});
	TestEqual(TEXT("Imported compiler string default"), Label->GetPropertyValue(Label->ContainerPtrToValuePtr<void>(Imported.GetStructMemory())), FString(TEXT("Zażółć 世界 🚀")));
	FScriptArrayHelper SampleValues(Samples, Samples->ContainerPtrToValuePtr<void>(Imported.GetStructMemory()));
	if (!TestEqual(TEXT("Imported compiler array length"), SampleValues.Num(), 2)) return false;
	FNumericProperty* SampleNumber = CastField<FNumericProperty>(Samples->Inner);
	if (!TestNotNull(TEXT("Native default array element"), SampleNumber)) return false;
	TestEqual(TEXT("Imported compiler first array value"), SampleNumber->GetSignedIntPropertyValue(SampleValues.GetRawPtr(0)), int64{3});
	TestEqual(TEXT("Imported compiler second array value"), SampleNumber->GetSignedIntPropertyValue(SampleValues.GetRawPtr(1)), int64{5});
	UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	const auto SplitDefaults = [this, Node, Schema, Struct, Number, Label, Detail](const TCHAR* Name, const FString& Text, const FString& ExpectedNumber, const FString& ExpectedLabel)
	{
		UEdGraphPin* Pin = Node->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, Struct, FName(Name));
		Pin->DefaultValue = Text;
		Pin->AutogeneratedDefaultValue = Text;
		Schema->SplitPin(Pin, false);
		const auto FindField = [Pin](const FProperty* Field)
		{
			const FName FieldName(*FString::Printf(TEXT("%s_%s"), *Pin->PinName.ToString(), *Field->GetName()));
			for (UEdGraphPin* SubPin : Pin->SubPins)
				if (SubPin->PinName == FieldName) return SubPin;
			return static_cast<UEdGraphPin*>(nullptr);
		};
		UEdGraphPin* NumberPin = FindField(Number);
		UEdGraphPin* LabelPin = FindField(Label);
		UEdGraphPin* DetailPin = FindField(Detail);
		if (!TestNotNull(TEXT("Split number field"), NumberPin) || !TestNotNull(TEXT("Split string field"), LabelPin)
			|| !TestNotNull(TEXT("Split nested record field"), DetailPin)) return false;
		TestEqual(TEXT("Split field inherits the parent number default"), NumberPin->DefaultValue, ExpectedNumber);
		TestEqual(TEXT("Split field inherits the parent string default"), LabelPin->DefaultValue, ExpectedLabel);
		Schema->SplitPin(DetailPin, false);
		if (!TestEqual(TEXT("Nested default split field count"), DetailPin->SubPins.Num(), 1)) return false;
		return TestEqual(TEXT("Nested split inherits compiler string default"), DetailPin->SubPins[0]->DefaultValue, FString(TEXT("nested 世界")));
	};
	SplitDefaults(TEXT("Original"), DefaultValue, TEXT("73"), TEXT("Zażółć 世界 🚀"));
	Number->SetIntPropertyValue(Number->ContainerPtrToValuePtr<void>(Imported.GetStructMemory()), int64{99});
	Label->SetPropertyValue(Label->ContainerPtrToValuePtr<void>(Imported.GetStructMemory()), TEXT("edited 世界"));
	FString EditedValue;
	Struct->ExportText(EditedValue, Imported.GetStructMemory(), Imported.GetStructMemory(), nullptr, PPF_SerializedAsImportText, nullptr);
	SplitDefaults(TEXT("Edited"), EditedValue, TEXT("99"), TEXT("edited 世界"));
	TArray<uint8> EncodedBytes;
	if (!TestTrue(TEXT("Edited native default encodes into an owned packet"), UEmkaRecordTypes::EncodeDefaultValue(PinType, TEXT(" \t") + EditedValue + TEXT("\n"), EncodedBytes, Error))) return false;
	FUEmkaCompiledValue EncodedValue = Value;
	EncodedValue.DefaultCompositeValue = EncodedBytes;
	FString EncodedText;
	if (!TestTrue(TEXT("Encoded edited default reexports"), UEmkaRecordTypes::GetDefaultValue(EncodedValue, PinType, EncodedText, Error))) return false;
	TestEqual(TEXT("Encoded edited default preserves all reflected values"), EncodedText, EditedValue);
	TestFalse(TEXT("Trailing default text is rejected"), UEmkaRecordTypes::EncodeDefaultValue(PinType, EditedValue + TEXT("junk"), EncodedBytes, Error));
	TestTrue(TEXT("Rejected default text clears stale bytes"), EncodedBytes.IsEmpty());
	TestTrue(TEXT("Trailing default text has a diagnostic"), Error.Contains(TEXT("trailing")));
	TestFalse(TEXT("Malformed default text is rejected"), UEmkaRecordTypes::EncodeDefaultValue(PinType, TEXT("invalid"), EncodedBytes, Error));
	TestFalse(TEXT("Malformed default text has a diagnostic"), Error.IsEmpty());
	TestTrue(TEXT("Empty native default encodes initialized values"), UEmkaRecordTypes::EncodeDefaultValue(PinType, TEXT(""), EncodedBytes, Error));
	TestFalse(TEXT("Initialized native default has an owned payload"), EncodedBytes.IsEmpty());
	FEdGraphPinType ContainerType = PinType;
	ContainerType.ContainerType = EPinContainerType::Array;
	TestFalse(TEXT("Array default rejects a scalar record literal"), UEmkaRecordTypes::EncodeDefaultValue(ContainerType, EditedValue, EncodedBytes, Error));
	TestTrue(TEXT("Rejected array default clears stale bytes"), EncodedBytes.IsEmpty());
	TestTrue(TEXT("Array of records default encodes"), UEmkaRecordTypes::EncodeDefaultValue(ContainerType, TEXT("(") + EditedValue + TEXT(")"), EncodedBytes, Error));
	TestFalse(TEXT("Array of records has an owned payload"), EncodedBytes.IsEmpty());
	const TArray<uint8> RecordArrayBytes = EncodedBytes;
	TestTrue(TEXT("Empty array of records encodes initialized storage"), UEmkaRecordTypes::EncodeDefaultValue(ContainerType, TEXT(""), EncodedBytes, Error));
	TestTrue(TEXT("Empty record array has a distinct owned payload"), !EncodedBytes.IsEmpty() && EncodedBytes != RecordArrayBytes);
	FString FieldText;
	if (!TestTrue(TEXT("Split array default exports from current parent text"), UEmkaRecordTypes::GetFieldDefaultValue(PinType, EditedValue, Samples->GetFName(), FieldText, Error))) return false;
	TestEqual(TEXT("Split array export retains compiler elements"), FieldText, FString(TEXT("(3,5)")));
	FEdGraphPinType ArrayType;
	ArrayType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	ArrayType.ContainerType = EPinContainerType::Array;
	if (!TestTrue(TEXT("Split array default encodes"), UEmkaRecordTypes::EncodeDefaultValue(ArrayType, FieldText, EncodedBytes, Error))) return false;
	FUEmkaScriptParam ArrayPacket;
	ArrayPacket.Type = EUEmkaValueType::Composite;
	ArrayPacket.CompositeValue = EncodedBytes;
	if (!TestTrue(TEXT("Split array packet decodes into reflected storage"), UUEmkaFunctionLibrary::DecodeComposite(ArrayPacket, Samples, Samples->ContainerPtrToValuePtr<void>(Imported.GetStructMemory()), Error))) return false;
	TestEqual(TEXT("Split array roundtrip preserves count"), SampleValues.Num(), 2);
	TestEqual(TEXT("Split array roundtrip preserves first value"), SampleNumber->GetSignedIntPropertyValue(SampleValues.GetRawPtr(0)), int64{3});
	TestEqual(TEXT("Split array roundtrip preserves second value"), SampleNumber->GetSignedIntPropertyValue(SampleValues.GetRawPtr(1)), int64{5});
	TestTrue(TEXT("Current edited scalar field exports"), UEmkaRecordTypes::GetFieldDefaultValue(PinType, EditedValue, Number->GetFName(), FieldText, Error));
	TestEqual(TEXT("Current edited scalar field value"), FieldText, FString(TEXT("99")));
	TestFalse(TEXT("Unknown split field fails"), UEmkaRecordTypes::GetFieldDefaultValue(PinType, EditedValue, TEXT("Missing"), FieldText, Error));
	TestTrue(TEXT("Unknown split field clears stale text"), FieldText.IsEmpty() && !Error.IsEmpty());
	FEdGraphPinType MapType;
	MapType.PinCategory = UEdGraphSchema_K2::PC_String;
	MapType.ContainerType = EPinContainerType::Map;
	MapType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Struct;
	MapType.PinValueType.TerminalSubCategoryObject = Struct;
	TestTrue(TEXT("Map with record default encodes"), UEmkaRecordTypes::EncodeDefaultValue(MapType, TEXT("((\"entry\",") + EditedValue + TEXT("))"), EncodedBytes, Error));
	TestFalse(TEXT("Map with record default has an owned payload"), EncodedBytes.IsEmpty());
	const TArray<uint8> RecordMapBytes = EncodedBytes;
	TestTrue(TEXT("Empty map default encodes initialized storage"), UEmkaRecordTypes::EncodeDefaultValue(MapType, TEXT(""), EncodedBytes, Error));
	TestTrue(TEXT("Empty record map has a distinct owned payload"), !EncodedBytes.IsEmpty() && EncodedBytes != RecordMapBytes);
	FUEmkaCompiledValue Invalid = Value;
	Invalid.DefaultCompositeValue.SetNum(1);
	TestFalse(TEXT("Truncated compiler default is rejected"), UEmkaRecordTypes::GetDefaultValue(Invalid, PinType, DefaultValue, Error));
	TestTrue(TEXT("Failed default export clears stale text"), DefaultValue.IsEmpty());
	TestFalse(TEXT("Failed default export provides a diagnostic"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaExactCaseDefaultRefreshTest, "UEmka.Editor.ExactCaseDefaultRefresh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaExactCaseDefaultRefreshTest::RunTest(const FString& Parameters)
{
	const FString LowerScript = TEXT("fn Run*(Value: str = \"abc\"): str { return Value }");
	const FString UpperScript = TEXT("fn Run*(Value: str = \"ABC\"): str { return Value }");
	const FUEmkaSignature Lower = UK2Node_UEmka::ParseScript(LowerScript);
	const FUEmkaSignature Upper = UK2Node_UEmka::ParseScript(UpperScript);
	TestTrue(TEXT("Case-only compiler default edits change the signature"), Lower != Upper);
	FUEmkaCompiledValue Named;
	Named.Name = TEXT("Value");
	Named.TypeName = TEXT("Record");
	FUEmkaCompiledValue Changed = Named;
	Changed.Name = TEXT("value");
	TestTrue(TEXT("Compiled parameter names compare exact case"), Named != Changed);
	Changed = Named;
	Changed.TypeName = TEXT("record");
	TestTrue(TEXT("Compiled type names compare exact case"), Named != Changed);
	FUEmkaCompiledValue Field = Named;
	Named.Fields.Add(MoveTemp(Field));
	Changed = Named;
	Changed.Fields[0].Name = TEXT("value");
	TestTrue(TEXT("Nested compiled fields compare exact case"), Named != Changed);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("UEmkaCaseDefault")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!TestNotNull(TEXT("Case default Blueprint"), Blueprint)) return false;
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Case default graph"), Graph)) return false;
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = LowerScript;
	Node->AllocateDefaultPins();
	UEdGraphPin* Pin = Node->FindPin(TEXT("Value"), EGPD_Input);
	if (!TestNotNull(TEXT("Lowercase default input"), Pin)) return false;
	TestTrue(TEXT("Original autogenerated default preserves lowercase"), Pin->AutogeneratedDefaultValue.Equals(TEXT("abc"), ESearchCase::CaseSensitive));
	Node->OnScriptChanged(UpperScript);
	Pin = Node->FindPin(TEXT("Value"), EGPD_Input);
	if (!TestNotNull(TEXT("Updated default input"), Pin)) return false;
	TestTrue(TEXT("Case-only edit refreshes the autogenerated default"), Pin->AutogeneratedDefaultValue.Equals(TEXT("ABC"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Case-only edit refreshes the active pin literal"), Pin->DefaultValue.Equals(TEXT("ABC"), ESearchCase::CaseSensitive));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
