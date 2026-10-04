// Copyright Solessfir 2026. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformProperties.h"
#include "Misc/AutomationTest.h"
#include "UEmkaScriptAsset.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/StrProperty.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
FProperty* FindAuthoredField(UStruct* Struct, const FString& Name)
{
	for (TFieldIterator<FProperty> Property(Struct); Property; ++Property)
	{
		if (Property->GetAuthoredName() == Name) return *Property;
	}
	return nullptr;
}

bool CheckRow(FAutomationTestBase& Test, FProperty* Property, const void* Data)
{
	FStructProperty* Record = CastField<FStructProperty>(Property);
	if (!Test.TestNotNull(TEXT("Cooked output retains native record type"), Record)) return false;
	FNumericProperty* Number = CastField<FNumericProperty>(FindAuthoredField(Record->Struct, TEXT("Number")));
	FStrProperty* Label = CastField<FStrProperty>(FindAuthoredField(Record->Struct, TEXT("Label")));
	if (!Test.TestNotNull(TEXT("Cooked record retains authored Number field"), Number)
		|| !Test.TestNotNull(TEXT("Cooked record retains authored Label field"), Label)) return false;
	bool bPassed = Test.TestEqual(TEXT("Cooked record contains transitive calculation"), Number->GetSignedIntPropertyValue(Number->ContainerPtrToValuePtr<void>(Data)), int64(15));
	bPassed &= Test.TestEqual(TEXT("Cooked record retains UTF-8 string"), Label->GetPropertyValue(Label->ContainerPtrToValuePtr<void>(Data)), FString(TEXT("cooked 世界")));
	return bPassed;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaCookedAssetExecutionTest, "UEmka.Cooked.AssetExecution",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaCookedAssetExecutionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Validation executes cooked data"), FPlatformProperties::RequiresCookedData())) return false;
	UClass* Runner = LoadObject<UClass>(nullptr, TEXT("/Game/UEmkaCookValidation/Runner.Runner_C"));
	if (!TestNotNull(TEXT("Cooked runner class loads"), Runner)) return false;
	UUEmkaScriptAsset* Root = FindObject<UUEmkaScriptAsset>(nullptr, TEXT("/Game/UEmkaCookValidation/Root.Root"));
	if (!TestNotNull(TEXT("Runner bytecode strongly references reusable script"), Root)) return false;
	TestTrue(TEXT("Cooked root retains source"), Root->Source.Contains(TEXT("fn Run*")));
	if (!TestEqual(TEXT("Cooked root retains module reference"), Root->Imports.Num(), 1)) return false;
	UUEmkaScriptAsset* Library = Root->Imports[0];
	if (!TestNotNull(TEXT("Cooked library module loads"), Library)
		|| !TestEqual(TEXT("Cooked library retains transitive import"), Library->Imports.Num(), 1)) return false;
	UUEmkaScriptAsset* Base = Library->Imports[0];
	if (!TestNotNull(TEXT("Cooked transitive module loads"), Base)) return false;
	TestTrue(TEXT("Cooked transitive module retains source"), Base->Source.Contains(TEXT("fn Twice*")));
	UFunction* Function = Runner->FindFunctionByName(TEXT("ExecuteCooked"));
	if (!TestNotNull(TEXT("Cooked Blueprint function survives"), Function)) return false;
	UObject* Instance = NewObject<UObject>(GetTransientPackage(), Runner);
	FStructOnScope Params(Function);
	Instance->ProcessEvent(Function, Params.GetStructMemory());
	FProperty* Row = FindFProperty<FProperty>(Function, TEXT("Out0"));
	FMapProperty* Map = FindFProperty<FMapProperty>(Function, TEXT("Out1"));
	FNumericProperty* Scalar = FindFProperty<FNumericProperty>(Function, TEXT("Out2"));
	if (!TestNotNull(TEXT("Cooked record output exists"), Row)
		|| !TestNotNull(TEXT("Cooked record map output exists"), Map)
		|| !TestNotNull(TEXT("Cooked scalar output exists"), Scalar)) return false;
	bool bPassed = CheckRow(*this, Row, Row->ContainerPtrToValuePtr<void>(Params.GetStructMemory()));
	bPassed &= TestEqual(TEXT("Selected export and default execute through transitive module"), Scalar->GetSignedIntPropertyValue(Scalar->ContainerPtrToValuePtr<void>(Params.GetStructMemory())), int64(15));
	FStrProperty* Key = CastField<FStrProperty>(Map->KeyProp);
	if (!TestNotNull(TEXT("Cooked map retains string keys"), Key)) return false;
	FScriptMapHelper Helper(Map, Map->ContainerPtrToValuePtr<void>(Params.GetStructMemory()));
	if (!TestEqual(TEXT("Cooked record map contains one result"), Helper.Num(), 1)) return false;
	for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
	{
		if (!Helper.IsValidIndex(Index)) continue;
		bPassed &= TestEqual(TEXT("Cooked map retains key"), Key->GetPropertyValue(Helper.GetKeyPtr(Index)), FString(TEXT("entry")));
		bPassed &= CheckRow(*this, Map->ValueProp, Helper.GetValuePtr(Index));
	}
	return bPassed;
}

#endif
