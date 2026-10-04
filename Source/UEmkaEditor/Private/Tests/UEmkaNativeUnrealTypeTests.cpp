// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"
#include "UEmkaNativeTypes.h"
#include "UEmkaRecordTypes.h"

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
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <type_traits>

namespace
{
const FString NativeScript = TEXT("import u = \"ue.um\"\n")
	TEXT("fn Test*(V: u::Vector, R: u::Rotator, C: u::LinearColor, Q: u::Quat, T: u::Transform, A: []u::Vector, M: map[str]u::Transform): (u::Vector, u::Rotator, u::LinearColor, u::Quat, u::Transform, []u::Vector, map[str]u::Transform) { return V, R, C, Q, T, A, M }");
const TArray<FString> NativeNames = {TEXT("Vector"), TEXT("Rotator"), TEXT("LinearColor"), TEXT("Quat"), TEXT("Transform")};

UFunction* BuildNativeFunction(FAutomationTestBase& Test, const FString& Script, const bool bConnectInputs, UObject*& OutInstance)
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/Automation/UEmka/NativeUE_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("NativeUE")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!Test.TestNotNull(TEXT("Native UE Blueprint"), Blueprint)) return nullptr;
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("ExecuteNative"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (!Test.TestEqual(TEXT("Native UE function entry"), Entries.Num(), 1)) return nullptr;
	UK2Node_FunctionEntry* Entry = Entries[0];
	UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = Script;
	Node->AllocateDefaultPins();
	Test.TestFalse(TEXT("Builtin structs remain whole pins with default flattening"), Node->bNativeStructPins);
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Test.TestTrue(TEXT("Native UE entry wiring"), Schema->TryCreateConnection(Entry->GetThenPin(), Node->GetExecPin()))
		|| !Test.TestTrue(TEXT("Native UE exit wiring"), Schema->TryCreateConnection(Node->GetThenPin(), Exit->GetExecPin()))) return nullptr;
	int32 Output = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
		if (Pin->Direction == EGPD_Input && bConnectInputs)
		{
			UEdGraphPin* Source = Entry->CreateUserDefinedPin(Pin->PinName, Pin->PinType, EGPD_Output);
			if (!Test.TestTrue(TEXT("Native UE input connection"), Schema->TryCreateConnection(Source, Pin))) return nullptr;
		}
		if (Pin->Direction == EGPD_Output)
		{
			UEdGraphPin* Target = Exit->CreateUserDefinedPin(FName(*FString::Printf(TEXT("Out%d"), Output++)), Pin->PinType, EGPD_Input);
			if (!Test.TestTrue(TEXT("Native UE output connection"), Schema->TryCreateConnection(Pin, Target))) return nullptr;
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
	if (!Test.TestEqual(TEXT("Native UE Blueprint compiles"), Log.NumErrors, 0)) return nullptr;
	UFunction* Function = Blueprint->GeneratedClass->FindFunctionByName(Graph->GetFName());
	OutInstance = NewObject<UObject>(GetTransientPackage(), Blueprint->GeneratedClass);
	return Function;
}

template<typename T>
bool SetNative(FAutomationTestBase& Test, UFunction* Function, void* Memory, const TCHAR* Name, const T& Value)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Function, Name);
	if (!Test.TestNotNull(FString(Name) + TEXT(" native property"), Property)) return false;
	if (!Test.TestEqual(FString(Name) + TEXT(" native identity"), Property->Struct.Get(), TBaseStructure<T>::Get())) return false;
	*Property->ContainerPtrToValuePtr<T>(Memory) = Value;
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeUnrealLayoutTest, "UEmka.Editor.NativeUnrealTypes.LayoutAndIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeUnrealLayoutTest::RunTest(const FString& Parameters)
{
	FUEmkaCompiledSignature Signature;
	if (!TestTrue(TEXT("Builtin module and alias compile"), UUEmkaFunctionLibrary::InspectScriptFunction(NativeScript, TEXT("Test"), Signature))
		|| !TestEqual(TEXT("Builtin parameter count"), Signature.Params.Num(), 7)) return false;
	FString Error;
	UObject* Owner = NewObject<UEdGraph>(CreatePackage(*FString::Printf(TEXT("/Temp/Automation/UEmka/NativeLayout_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	for (int32 Index = 0; Index < NativeNames.Num(); ++Index)
	{
		const FString& Name = NativeNames[Index];
		const FUEmkaCompiledValue& Value = Signature.Params[Index];
		TestEqual(Name + TEXT(" declaration identity survives alias"), Value.NativeStructName, Name);
		TestTrue(Name + TEXT(" exact compiled layout"), UEmkaNativeTypes::IsValidLayout(Value, Name));
		const FEdGraphPinType PinType = UEmkaRecordTypes::GetPinType(Value, Owner, Error);
		TestTrue(Name + TEXT(" resolves without error"), Error.IsEmpty());
		TestEqual(Name + TEXT(" native Blueprint struct"), PinType.PinSubCategoryObject.Get(), static_cast<UObject*>(UEmkaNativeTypes::GetNativeStruct(Name)));
		if (Name != TEXT("Transform"))
		{
			int32 FieldIndex = 0;
			for (TFieldIterator<FProperty> It(UEmkaNativeTypes::GetNativeStruct(Name)); It; ++It)
			{
				if (!TestTrue(Name + TEXT(" reflected field exists in compiled layout"), FieldIndex < Value.Fields.Num())) return false;
				TestEqual(Name + TEXT(" reflected authored name"), It->GetAuthoredName(), Value.Fields[FieldIndex++].Name);
				FNumericProperty* Number = CastField<FNumericProperty>(*It);
				if (!TestNotNull(Name + TEXT(" numeric reflection"), Number)) return false;
				TestTrue(Name + TEXT(" floating point reflection"), Number->IsFloatingPoint());
				TestEqual(Name + TEXT(" reflected precision"), Number->GetElementSize(), Name == TEXT("LinearColor") ? 4 : 8);
			}
			TestEqual(Name + TEXT(" reflected field count"), FieldIndex, Value.Fields.Num());
		}
		FUEmkaCompiledValue Malformed = Value;
		Malformed.Fields[0].Name = Malformed.Fields[0].Name.ToLower();
		TestFalse(Name + TEXT(" rejects wrong field case"), UEmkaNativeTypes::IsValidLayout(Malformed, Name));
		UEmkaRecordTypes::GetPinType(Malformed, Owner, Error);
		TestFalse(Name + TEXT(" malformed native identity fails closed"), Error.IsEmpty());
		Malformed = Value;
		Malformed.Fields.RemoveAt(0);
		TestFalse(Name + TEXT(" rejects missing field"), UEmkaNativeTypes::IsValidLayout(Malformed, Name));
		Malformed = Value;
		Malformed.Fields[0].Type = EUEmkaValueType::Int;
		TestFalse(Name + TEXT(" rejects wrong scalar or nested type"), UEmkaNativeTypes::IsValidLayout(Malformed, Name));
	}
	TestEqual(TEXT("Array retains native vector"), UEmkaRecordTypes::GetPinType(Signature.Params[5], Owner, Error).PinSubCategoryObject.Get(), static_cast<UObject*>(TBaseStructure<FVector>::Get()));
	TestEqual(TEXT("Map retains native transform"), UEmkaRecordTypes::GetPinType(Signature.Params[6], Owner, Error).PinValueType.TerminalSubCategoryObject.Get(), static_cast<UObject*>(TBaseStructure<FTransform>::Get()));
	for (int32 Index = 0; Index < NativeNames.Num(); ++Index) TestEqual(TEXT("Tuple preserves native identity"), Signature.Result.Fields[Index].NativeStructName, NativeNames[Index]);
	FUEmkaCompiledSignature Ordinary;
	if (TestTrue(TEXT("Ordinary Vector compiles"), UUEmkaFunctionLibrary::InspectScriptFunction(TEXT("type Vector = struct { X, Y, Z: real }\nfn Test*(V: Vector): Vector { return V }"), TEXT("Test"), Ordinary)))
	{
		TestTrue(TEXT("Matching user type does not receive native identity"), Ordinary.Params[0].NativeStructName.IsEmpty());
		TestNotNull(TEXT("Matching user type remains generated"), Cast<UUserDefinedStruct>(UEmkaRecordTypes::GetPinType(Ordinary.Params[0], Owner, Error).PinSubCategoryObject.Get()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeUnrealWiringTest, "UEmka.Editor.NativeUnrealTypes.WiringRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeUnrealWiringTest::RunTest(const FString& Parameters)
{
	UObject* Instance = nullptr;
	UFunction* Function = BuildNativeFunction(*this, NativeScript, true, Instance);
	if (!TestNotNull(TEXT("Native UE compiled function"), Function)) return false;
	FStructOnScope Values(Function);
	const FVector Vector(9007199254740991.0, -1000000000000.125, 0.000000123456789);
	const FRotator Rotator(-720.125, 361.0625, 89.75);
	const FLinearColor Color(-3.5f, 15.25f, 0.125f, 0.625f);
	const FQuat Quat = FRotator(23.0, 47.0, -15.0).Quaternion();
	const FTransform Transform(Quat, Vector, FVector(2.0, 3.0, 4.0));
	void* Memory = Values.GetStructMemory();
	if (!SetNative(*this, Function, Memory, TEXT("V"), Vector) || !SetNative(*this, Function, Memory, TEXT("R"), Rotator)
		|| !SetNative(*this, Function, Memory, TEXT("C"), Color) || !SetNative(*this, Function, Memory, TEXT("Q"), Quat)
		|| !SetNative(*this, Function, Memory, TEXT("T"), Transform)) return false;
	FArrayProperty* Array = FindFProperty<FArrayProperty>(Function, TEXT("A"));
	FMapProperty* Map = FindFProperty<FMapProperty>(Function, TEXT("M"));
	if (!TestNotNull(TEXT("Native vector array input"), Array) || !TestNotNull(TEXT("Native transform map input"), Map)) return false;
	FScriptArrayHelper ArrayValues(Array, Array->ContainerPtrToValuePtr<void>(Memory));
	ArrayValues.Resize(2);
	*reinterpret_cast<FVector*>(ArrayValues.GetRawPtr(0)) = Vector;
	*reinterpret_cast<FVector*>(ArrayValues.GetRawPtr(1)) = FVector::ZeroVector;
	FScriptMapHelper MapValues(Map, Map->ContainerPtrToValuePtr<void>(Memory));
	const int32 MapIndex = MapValues.AddDefaultValue_Invalid_NeedsRehash();
	*reinterpret_cast<FString*>(MapValues.GetKeyPtr(MapIndex)) = TEXT("transform");
	*reinterpret_cast<FTransform*>(MapValues.GetValuePtr(MapIndex)) = Transform;
	MapValues.Rehash();
	Instance->ProcessEvent(Function, Memory);
	const TArray<FString> Inputs = {TEXT("V"), TEXT("R"), TEXT("C"), TEXT("Q"), TEXT("T"), TEXT("A"), TEXT("M")};
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		FProperty* Input = FindFProperty<FProperty>(Function, FName(*Inputs[Index]));
		FProperty* Output = FindFProperty<FProperty>(Function, FName(*FString::Printf(TEXT("Out%d"), Index)));
		if (!TestNotNull(TEXT("Native UE tuple output"), Output)) return false;
		TestTrue(Inputs[Index] + TEXT(" survives Blueprint and Umka round trip"), Output->Identical(Output->ContainerPtrToValuePtr<void>(Memory), Input->ContainerPtrToValuePtr<void>(Memory)));
	}
	FStructProperty* VectorOutput = FindFProperty<FStructProperty>(Function, TEXT("Out0"));
	TestEqual(TEXT("Large-world double precision is exact"), VectorOutput->ContainerPtrToValuePtr<FVector>(Memory)->X, Vector.X);
	ArrayValues.Resize(0);
	MapValues.EmptyValues();
	Instance->ProcessEvent(Function, Memory);
	FArrayProperty* ArrayOutput = FindFProperty<FArrayProperty>(Function, TEXT("Out5"));
	FMapProperty* MapOutput = FindFProperty<FMapProperty>(Function, TEXT("Out6"));
	TestEqual(TEXT("Empty native array round trips"), FScriptArrayHelper(ArrayOutput, ArrayOutput->ContainerPtrToValuePtr<void>(Memory)).Num(), 0);
	TestEqual(TEXT("Empty native map round trips"), FScriptMapHelper(MapOutput, MapOutput->ContainerPtrToValuePtr<void>(Memory)).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeUnrealDefaultsTest, "UEmka.Editor.NativeUnrealTypes.CompilerDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeUnrealDefaultsTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("import u = \"ue.um\"\n")
		TEXT("fn Test*(V: u::Vector = u::Vector{1.25, -2.5, 3.75}, R: u::Rotator = u::Rotator{10, 20, 30}, C: u::LinearColor = u::LinearColor{0.125, 0.25, 0.5, 1}, Q: u::Quat = u::Quat{0, 0, 0, 1}, T: u::Transform = u::Transform{u::Quat{0, 0, 0, 1}, u::Vector{0, 0, 0}, u::Vector{1, 1, 1}}): (u::Vector, u::Rotator, u::LinearColor, u::Quat, u::Transform) { return V, R, C, Q, T }");
	UObject* Instance = nullptr;
	UFunction* Function = BuildNativeFunction(*this, Script, false, Instance);
	if (!TestNotNull(TEXT("Native defaults compile without connections"), Function)) return false;
	FStructOnScope Values(Function);
	Instance->ProcessEvent(Function, Values.GetStructMemory());
	const auto Check = [this, Function, &Values](const TCHAR* Name, const auto& Expected)
	{
		using T = std::decay_t<decltype(Expected)>;
		FStructProperty* Output = FindFProperty<FStructProperty>(Function, Name);
		if (!TestNotNull(TEXT("Native default output"), Output)) return;
		const T& Actual = *Output->ContainerPtrToValuePtr<T>(Values.GetStructMemory());
		TestTrue(FString(Name) + TEXT(" native compiler default"), Actual == Expected);
	};
	Check(TEXT("Out0"), FVector(1.25, -2.5, 3.75));
	Check(TEXT("Out1"), FRotator(10, 20, 30));
	Check(TEXT("Out2"), FLinearColor(0.125f, 0.25f, 0.5f, 1.0f));
	Check(TEXT("Out3"), FQuat::Identity);
	FStructProperty* TransformOutput = FindFProperty<FStructProperty>(Function, TEXT("Out4"));
	if (!TestNotNull(TEXT("Identity transform default output"), TransformOutput)) return false;
	const FTransform& Identity = *TransformOutput->ContainerPtrToValuePtr<FTransform>(Values.GetStructMemory());
	TestTrue(TEXT("Identity rotation"), Identity.GetRotation() == FQuat::Identity);
	TestTrue(TEXT("Identity translation"), Identity.GetTranslation() == FVector::ZeroVector);
	TestTrue(TEXT("Identity scale"), Identity.GetScale3D() == FVector::OneVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeUnrealRejectedPayloadTest, "UEmka.Editor.NativeUnrealTypes.RejectedPayloadsAreAtomic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeUnrealRejectedPayloadTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Scripts = {
		TEXT("type Color = struct { R, G, B, A: real }\nfn Test*(C: Color = Color{0.5, 1e100, 0.25, 1}): Color { return C }"),
		TEXT("import u = \"ue.um\"\ntype Transform = struct { Rotation: u::Quat; Translation, scale3D: u::Vector }\nfn Test*(T: Transform = Transform{u::Quat{0, 0, 0, 1}, u::Vector{1, 2, 3}, u::Vector{1, 1, 1}}): Transform { return T }")
	};
	for (int32 Index = 0; Index < Scripts.Num(); ++Index)
	{
		FUEmkaCompiledSignature Signature;
		if (!TestTrue(TEXT("Rejected payload fixture compiles"), UUEmkaFunctionLibrary::InspectScriptFunction(Scripts[Index], TEXT("Test"), Signature))) return false;
		TUniquePtr<FStructProperty> Property = MakeUnique<FStructProperty>(nullptr, NAME_None);
		Property->Struct = Index == 0 ? TBaseStructure<FLinearColor>::Get() : TBaseStructure<FTransform>::Get();
		Property->SetElementSize(Property->Struct->GetStructureSize());
		FStructOnScope Destination(Property->Struct);
		FStructOnScope Original(Property->Struct);
		if (Index == 0)
		{
			*reinterpret_cast<FLinearColor*>(Destination.GetStructMemory()) = FLinearColor(3.0f, 2.0f, 1.0f, 0.5f);
		}
		else
		{
			*reinterpret_cast<FTransform*>(Destination.GetStructMemory()) = FTransform(FRotator(1, 2, 3), FVector(4, 5, 6), FVector(7, 8, 9));
		}
		Property->CopyCompleteValue(Original.GetStructMemory(), Destination.GetStructMemory());
		FUEmkaScriptParam Packet;
		Packet.Type = EUEmkaValueType::Composite;
		Packet.CompositeValue = Signature.Params[0].DefaultCompositeValue;
		FString Error;
		TestFalse(Index == 0 ? TEXT("Color float overflow rejected") : TEXT("Transform field case mismatch rejected"), UUEmkaFunctionLibrary::DecodeComposite(Packet, Property.Get(), Destination.GetStructMemory(), Error));
		TestFalse(TEXT("Rejected native payload reports an error"), Error.IsEmpty());
		TestTrue(TEXT("Rejected native payload preserves the complete destination"), Property->Identical(Destination.GetStructMemory(), Original.GetStructMemory(), 0));
	}
	return true;
}

#endif
