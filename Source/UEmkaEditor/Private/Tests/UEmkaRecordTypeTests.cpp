// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaRecordTypes.h"
#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UEmkaFunctionLibrary.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
FUEmkaCompiledValue Scalar(const TCHAR* Name, const EUEmkaValueType Type)
{
	FUEmkaCompiledValue Value;
	Value.Name = Name;
	Value.Type = Type;
	Value.bSupported = true;
	return Value;
}

FUEmkaCompiledValue Record(const TCHAR* Name, TArray<FUEmkaCompiledValue> Fields)
{
	FUEmkaCompiledValue Value;
	Value.Name = Name;
	Value.bSupported = true;
	Value.bIsStruct = true;
	Value.Type = EUEmkaValueType::Composite;
	Value.Fields = MoveTemp(Fields);
	return Value;
}

FUEmkaCompiledValue Array(const TCHAR* Name, FUEmkaCompiledValue Element)
{
	FUEmkaCompiledValue Value;
	Value.Name = Name;
	Value.Type = Element.bIsStruct ? EUEmkaValueType::Composite : Element.Type;
	Value.bSupported = true;
	Value.bIsArray = true;
	Element.Name = TEXT("Element");
	Value.Fields.Add(MoveTemp(Element));
	return Value;
}

FProperty* FindAuthoredProperty(UUserDefinedStruct* Struct, const TCHAR* Name)
{
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		if (It->GetAuthoredName() == Name) return *It;
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRecordTypeTest, "UEmka.Editor.RecordTypes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRecordTypeTest::RunTest(const FString& Parameters)
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/Automation/UEmka/UEmkaRecordTypes_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UObject* Owner = NewObject<UEdGraph>(Package);
	FUEmkaCompiledValue Enum = Scalar(TEXT("kind"), EUEmkaValueType::Int32);
	Enum.bIsEnum = true;
	Enum.EnumByteSize = 4;
	FUEmkaCompiledValue Map;
	Map.Name = TEXT("counts");
	Map.bSupported = true;
	Map.bIsMap = true;
	Map.Type = EUEmkaValueType::Composite;
	Map.Fields = {Scalar(TEXT("Key"), EUEmkaValueType::Str), Scalar(TEXT("Value"), EUEmkaValueType::Int)};
	FUEmkaCompiledValue Value = Array(TEXT("records"), Record(TEXT("Row"), {
		Enum,
		Array(TEXT("samples"), Scalar(TEXT("Element"), EUEmkaValueType::Real32)),
		Record(TEXT("details"), {Scalar(TEXT("display_name"), EUEmkaValueType::Str)}),
		Map,
		Array(TEXT("children"), Record(TEXT("Child"), {Scalar(TEXT("enabled"), EUEmkaValueType::Bool)}))
	}));
	FString Error;
	const FEdGraphPinType PinType = UEmkaRecordTypes::GetPinType(Value, Owner, Error);
	TestTrue(TEXT("Record type compiles"), Error.IsEmpty());
	TestEqual(TEXT("Native struct category"), PinType.PinCategory, UEdGraphSchema_K2::PC_Struct);
	TestTrue(TEXT("Native array container"), PinType.ContainerType == EPinContainerType::Array);
	UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(PinType.PinSubCategoryObject.Get());
	if (!TestNotNull(TEXT("User-defined struct"), Struct)) return false;
	TestEqual(TEXT("Runtime export belongs to owning package"), Struct->GetOuter(), static_cast<UObject*>(Package));
	TestTrue(TEXT("Runtime export can be serialized"), Struct->HasAnyFlags(RF_Public));
	TestFalse(TEXT("Generated type is not a standalone asset"), Struct->HasAnyFlags(RF_Standalone));
	TestNotNull(TEXT("Enums preserve int32 Blueprint storage"), CastField<FIntProperty>(FindAuthoredProperty(Struct, TEXT("kind"))));
	FArrayProperty* Samples = CastField<FArrayProperty>(FindAuthoredProperty(Struct, TEXT("samples")));
	if (TestNotNull(TEXT("Native array field"), Samples)) TestNotNull(TEXT("real32 remains float"), CastField<FFloatProperty>(Samples->Inner));
	FStructProperty* Details = CastField<FStructProperty>(FindAuthoredProperty(Struct, TEXT("details")));
	if (TestNotNull(TEXT("Nested native record field"), Details)) TestNotNull(TEXT("Nested authored string field"), CastField<FStrProperty>(FindAuthoredProperty(CastChecked<UUserDefinedStruct>(Details->Struct), TEXT("display_name"))));
	FMapProperty* Counts = CastField<FMapProperty>(FindAuthoredProperty(Struct, TEXT("counts")));
	if (TestNotNull(TEXT("Native map field"), Counts))
	{
		TestNotNull(TEXT("Map string key"), CastField<FStrProperty>(Counts->KeyProp));
		TestNotNull(TEXT("Map int64 value"), CastField<FInt64Property>(Counts->ValueProp));
	}
	FArrayProperty* Children = CastField<FArrayProperty>(FindAuthoredProperty(Struct, TEXT("children")));
	if (TestNotNull(TEXT("Nested record array field"), Children)) TestNotNull(TEXT("Native record array element"), CastField<FStructProperty>(Children->Inner));

	// Cooked UDS retain authored names in property names after editor data is stripped.
	UObject* EditorData = Struct->EditorData;
	Struct->EditorData = nullptr;
	TestNotNull(TEXT("Authored names survive editor data stripping"), FindAuthoredProperty(Struct, TEXT("kind")));
	Struct->EditorData = EditorData;
	Value.Name = TEXT("renamedParameter");
	Value.bIsStaticArray = true;
	Value.ArrayLen = 3;
	TestEqual(TEXT("Same Blueprint shape reuses its serialized type"), UEmkaRecordTypes::GetPinType(Value, Owner, Error).PinSubCategoryObject.Get(), static_cast<UObject*>(Struct));
	Value.Fields[0].Fields[0].Type = EUEmkaValueType::UInt8;
	TestTrue(TEXT("Different enum storage gets a different type"), UEmkaRecordTypes::GetPinType(Value, Owner, Error).PinSubCategoryObject.Get() != Struct);
	Value.Fields[0].Fields[0].Type = EUEmkaValueType::Int32;

	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/UEmka");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString PackageName = Package->GetName();
	const FString Filename = Directory / (FPaths::GetCleanFilename(PackageName) + TEXT(".uasset"));
	const FName StructName = Struct->GetFName();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (TestTrue(TEXT("Generated record package saves"), UPackage::SavePackage(Package, Struct, *Filename, SaveArgs)))
	{
		Package->Rename(*(PackageName + TEXT("_Original")), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		UPackage* LoadedPackage = LoadPackage(nullptr, *Filename, LOAD_None);
		UUserDefinedStruct* LoadedStruct = LoadedPackage ? FindObject<UUserDefinedStruct>(LoadedPackage, *StructName.ToString()) : nullptr;
		if (TestNotNull(TEXT("Generated record export reloads"), LoadedStruct))
		{
			TestEqual(TEXT("Saved filename preserves the serialized package path"), LoadedPackage->GetName(), PackageName);
			TestNotNull(TEXT("Native map field survives package round trip"), CastField<FMapProperty>(FindAuthoredProperty(LoadedStruct, TEXT("counts"))));
			TestNotNull(TEXT("Nested record field survives package round trip"), CastField<FStructProperty>(FindAuthoredProperty(LoadedStruct, TEXT("details"))));
			UObject* LoadedOwner = NewObject<UEdGraph>(LoadedPackage);
			TestEqual(TEXT("Reloaded native export is reused"), UEmkaRecordTypes::GetPinType(Value, LoadedOwner, Error).PinSubCategoryObject.Get(), static_cast<UObject*>(LoadedStruct));
		}
	}
	IFileManager::Get().Delete(*Filename);

	FUEmkaCompiledValue Nested = Scalar(TEXT("end"), EUEmkaValueType::Int);
	for (int32 Depth = 0; Depth < 18; ++Depth) Nested = Record(TEXT("nested"), {MoveTemp(Nested)});
	UEmkaRecordTypes::GetPinType(Nested, Owner, Error);
	TestTrue(TEXT("Excessive nesting reports an error"), Error.Contains(TEXT("16 levels")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRecordFieldNameCollisionTest, "UEmka.Editor.RecordFieldNameCollisions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRecordFieldNameCollisionTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Row = struct { Foo: int; foo: int }\nfn Test*(Input: []Row): []Row { return Input }");
	FString Error;
	int32 ErrorLine = -1;
	if (!TestTrue(TEXT("Case-distinct record fields are valid Umka"), UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, ErrorLine))) return false;
	FUEmkaCompiledSignature Compiled;
	if (!TestTrue(TEXT("Case-distinct record schema is available"), UUEmkaFunctionLibrary::InspectScriptFunction(Script, TEXT("Test"), Compiled))
		|| !TestEqual(TEXT("Record array parameter is present"), Compiled.Params.Num(), 1)) return false;
	TestTrue(TEXT("Umka considers case-distinct fields supported"), Compiled.Params[0].bSupported);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("UEmkaFieldCollision")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Collision fixture has a graph"), Graph)) return false;
	const FEdGraphPinType RejectedType = UEmkaRecordTypes::GetPinType(Compiled.Params[0], Graph, Error);
	TestTrue(TEXT("Case-distinct record fields cannot generate an invalid native type"), RejectedType.PinCategory.IsNone());
	TestTrue(TEXT("Native error names both conflicting fields"), Error.Contains(TEXT("'Foo'"), ESearchCase::CaseSensitive) && Error.Contains(TEXT("'foo'"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Native error explains Blueprint's name constraint"), Error.Contains(TEXT("case-insensitive")));
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = Script;
	Node->AllocateDefaultPins();
	TestEqual(TEXT("Rejected native record retains execution pins only"), Node->Pins.Num(), 2);
	for (const UEdGraphPin* Pin : Node->Pins) TestEqual(TEXT("Rejected record produces no wildcard data pin"), Pin->PinType.PinCategory, UEdGraphSchema_K2::PC_Exec);
	AddExpectedError(TEXT("case-insensitive"), EAutomationExpectedErrorFlags::Contains, 1);
	FCompilerResultsLog Log;
	Node->ValidateNodeDuringCompilation(Log);
	TestEqual(TEXT("Blueprint validation reports native field collision"), Log.NumErrors, 1);
	TestTrue(TEXT("Blueprint validation preserves the useful native error"), Log.Messages.ContainsByPredicate([](const TSharedRef<FTokenizedMessage>& Message)
	{
		const FString Text = Message->ToText().ToString();
		return Text.Contains(TEXT("'Foo'"), ESearchCase::CaseSensitive) && Text.Contains(TEXT("'foo'"), ESearchCase::CaseSensitive) && Text.Contains(TEXT("case-insensitive"));
	}));
	for (const FString& CollidingScript : {
		FString(TEXT("fn Test*(execute: int): int { return execute }")),
		FString(TEXT("fn Test*(Foo: int, foo: int): int { return Foo + foo }")),
		FString(TEXT("type Row = struct { Foo: int; foo: int }\nfn Test*(Input: Row): Row { return Input }")),
		FString(TEXT("type Row = struct { Value: int }\nfn Test*(Input: Row, input_value: int): int { return Input.Value + input_value }"))})
	{
		if (!TestTrue(TEXT("Flattened collision fixture is valid Umka"), UUEmkaFunctionLibrary::CompileCheckScript(CollidingScript, Error, ErrorLine))) continue;
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(CollidingScript);
		TestTrue(TEXT("Case-insensitive pin collision has a diagnostic"), Signature.UnsupportedReason.Contains(TEXT("overlap")));
		TestTrue(TEXT("Colliding pin names expose no partial signature"), Signature.Params.IsEmpty() && Signature.ReturnParams.IsEmpty() && !Signature.ReturnType.IsSet());
	}
	return true;
}

#endif
