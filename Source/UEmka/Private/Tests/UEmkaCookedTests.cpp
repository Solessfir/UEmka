// Copyright Solessfir 2026. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformProperties.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UEmkaFunctionLibrary.h"
#include "UEmkaHostFunctions.h"
#include "UEmkaScriptAsset.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/NoExportTypes.h"
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

void CookedHostDouble(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	umkaGetResult(Params, Result)->intVal = umkaGetParam(Params, 0)->intVal * 2;
}

bool CheckStatus(FAutomationTestBase& Test, UFunction* Function, const void* Memory, const bool bExpectedSuccess)
{
	FBoolProperty* Success = FindFProperty<FBoolProperty>(Function, TEXT("OutSuccess"));
	FStrProperty* Error = FindFProperty<FStrProperty>(Function, TEXT("OutError"));
	if (!Test.TestNotNull(TEXT("Cooked Success pin survives"), Success) || !Test.TestNotNull(TEXT("Cooked Error pin survives"), Error)) return false;
	bool bPassed = Test.TestEqual(TEXT("Cooked runtime status"), Success->GetPropertyValue_InContainer(Memory), bExpectedSuccess);
	bPassed &= Test.TestEqual(TEXT("Cooked error clears on success and is present on failure"), Error->GetPropertyValue_InContainer(Memory).IsEmpty(), bExpectedSuccess);
	return bPassed;
}

bool CheckIntResult(FAutomationTestBase& Test, UFunction* Function, const void* Memory, const int64 Expected)
{
	FInt64Property* Result = FindFProperty<FInt64Property>(Function, TEXT("OutReturnValue"));
	return Test.TestNotNull(TEXT("Cooked script return pin survives"), Result)
		&& Test.TestEqual(TEXT("Cooked script return value"), Result->GetPropertyValue_InContainer(Memory), Expected);
}

template<typename T>
T* GetNativeValue(FAutomationTestBase& Test, UFunction* Function, void* Memory, const TCHAR* Name)
{
	FStructProperty* Property = FindFProperty<FStructProperty>(Function, Name);
	if (!Test.TestNotNull(FString(Name) + TEXT(" cooked native property"), Property)
		|| !Test.TestEqual(FString(Name) + TEXT(" cooked native struct identity"), Property->Struct.Get(), TBaseStructure<T>::Get())) return nullptr;
	return Property->ContainerPtrToValuePtr<T>(Memory);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaCookedNativeExecutionTest, "UEmka.Cooked.NativeTypes",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaCookedNativeExecutionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Native validation executes cooked data"), FPlatformProperties::RequiresCookedData())) return false;
	UClass* Runner = LoadObject<UClass>(nullptr, TEXT("/Game/UEmkaCookValidation/Runner.Runner_C"));
	if (!TestNotNull(TEXT("Cooked native runner class loads"), Runner)) return false;
	UObject* Instance = NewObject<UObject>(GetTransientPackage(), Runner);
	UFunction* RoundTrip = Runner->FindFunctionByName(TEXT("ExecuteCookedNative"));
	UFunction* Defaults = Runner->FindFunctionByName(TEXT("ExecuteCookedNativeDefaults"));
	if (!TestNotNull(TEXT("Cooked native roundtrip function"), RoundTrip) || !TestNotNull(TEXT("Cooked native defaults function"), Defaults)) return false;
	FStructOnScope Values(RoundTrip);
	void* Memory = Values.GetStructMemory();
	FVector* Vector = GetNativeValue<FVector>(*this, RoundTrip, Memory, TEXT("V"));
	FTransform* Transform = GetNativeValue<FTransform>(*this, RoundTrip, Memory, TEXT("T"));
	FVector* OutVector = GetNativeValue<FVector>(*this, RoundTrip, Memory, TEXT("OutReturnValue1"));
	FTransform* OutTransform = GetNativeValue<FTransform>(*this, RoundTrip, Memory, TEXT("OutReturnValue2"));
	if (!Vector || !Transform || !OutVector || !OutTransform) return false;
	*Vector = FVector(9007199254740991.0, -123456789.125, 0.000000123456789);
	*Transform = FTransform(FRotator(15, 30, 45).Quaternion(), *Vector, FVector(2, 3, 4));
	Instance->ProcessEvent(RoundTrip, Memory);
	TestTrue(TEXT("Cooked native Vector retains exact large-world precision"), *OutVector == *Vector);
	TestTrue(TEXT("Cooked native Transform retains rotation"), OutTransform->GetRotation() == Transform->GetRotation());
	TestTrue(TEXT("Cooked native Transform retains translation"), OutTransform->GetTranslation() == Transform->GetTranslation());
	TestTrue(TEXT("Cooked native Transform retains scale"), OutTransform->GetScale3D() == Transform->GetScale3D());
	FStructOnScope DefaultValues(Defaults);
	Instance->ProcessEvent(Defaults, DefaultValues.GetStructMemory());
	OutVector = GetNativeValue<FVector>(*this, Defaults, DefaultValues.GetStructMemory(), TEXT("OutReturnValue1"));
	OutTransform = GetNativeValue<FTransform>(*this, Defaults, DefaultValues.GetStructMemory(), TEXT("OutReturnValue2"));
	if (!OutVector || !OutTransform) return false;
	TestTrue(TEXT("Cooked native Vector default survives"), *OutVector == FVector(1.25, -2.5, 3.75));
	TestTrue(TEXT("Cooked native Transform default has identity rotation"), OutTransform->GetRotation() == FQuat::Identity);
	TestTrue(TEXT("Cooked native Transform default has zero translation"), OutTransform->GetTranslation() == FVector::ZeroVector);
	TestTrue(TEXT("Cooked native Transform default has unit scale"), OutTransform->GetScale3D() == FVector::OneVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaCookedExecutionControlTest, "UEmka.Cooked.ExecutionControls",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaCookedExecutionControlTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Execution control validation executes cooked data"), FPlatformProperties::RequiresCookedData())) return false;
	UClass* Runner = LoadObject<UClass>(nullptr, TEXT("/Game/UEmkaCookValidation/Runner.Runner_C"));
	if (!TestNotNull(TEXT("Cooked execution runner class loads"), Runner)) return false;
	UObject* First = NewObject<UObject>(GetTransientPackage(), Runner);
	UObject* Second = NewObject<UObject>(GetTransientPackage(), Runner);
	ON_SCOPE_EXIT { UUEmkaFunctionLibrary::ResetAllRuntimeSessions(); };
	UFunction* Session = Runner->FindFunctionByName(TEXT("ExecuteCookedSession"));
	UFunction* Status = Runner->FindFunctionByName(TEXT("ExecuteCookedStatus"));
	UFunction* Budget = Runner->FindFunctionByName(TEXT("ExecuteCookedBudget"));
	if (!TestNotNull(TEXT("Cooked session function"), Session) || !TestNotNull(TEXT("Cooked status function"), Status)
		|| !TestNotNull(TEXT("Cooked budget function"), Budget)) return false;
	FStructOnScope SessionValues(Session);
	First->ProcessEvent(Session, SessionValues.GetStructMemory());
	CheckStatus(*this, Session, SessionValues.GetStructMemory(), true);
	CheckIntResult(*this, Session, SessionValues.GetStructMemory(), 1);
	First->ProcessEvent(Session, SessionValues.GetStructMemory());
	CheckIntResult(*this, Session, SessionValues.GetStructMemory(), 2);
	Second->ProcessEvent(Session, SessionValues.GetStructMemory());
	CheckIntResult(*this, Session, SessionValues.GetStructMemory(), 1);
	FStructOnScope StatusValues(Status);
	FInt64Property* Denominator = FindFProperty<FInt64Property>(Status, TEXT("Value"));
	if (!TestNotNull(TEXT("Cooked division input"), Denominator)) return false;
	Denominator->SetPropertyValue_InContainer(StatusValues.GetStructMemory(), 1);
	First->ProcessEvent(Status, StatusValues.GetStructMemory());
	CheckStatus(*this, Status, StatusValues.GetStructMemory(), true);
	CheckIntResult(*this, Status, StatusValues.GetStructMemory(), 7);
	Denominator->SetPropertyValue_InContainer(StatusValues.GetStructMemory(), 0);
	AddExpectedError(TEXT("Divide:"), EAutomationExpectedErrorFlags::Contains, 1);
	First->ProcessEvent(Status, StatusValues.GetStructMemory());
	CheckStatus(*this, Status, StatusValues.GetStructMemory(), false);
	CheckIntResult(*this, Status, StatusValues.GetStructMemory(), 0);
	Denominator->SetPropertyValue_InContainer(StatusValues.GetStructMemory(), 2);
	First->ProcessEvent(Status, StatusValues.GetStructMemory());
	CheckStatus(*this, Status, StatusValues.GetStructMemory(), true);
	CheckIntResult(*this, Status, StatusValues.GetStructMemory(), 3);
	FStructOnScope BudgetValues(Budget);
	AddExpectedError(TEXT("Budget:"), EAutomationExpectedErrorFlags::Contains, 1);
	First->ProcessEvent(Budget, BudgetValues.GetStructMemory());
	CheckStatus(*this, Budget, BudgetValues.GetStructMemory(), false);
	CheckIntResult(*this, Budget, BudgetValues.GetStructMemory(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaCookedHostFunctionTest, "UEmka.Cooked.HostFunctions",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaCookedHostFunctionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Host callback validation executes cooked data"), FPlatformProperties::RequiresCookedData())) return false;
	FString Error;
	const TArray<UEmkaHostFunctions::FFunction> Functions = {{TEXT("UEmkaCookHostDouble"), &CookedHostDouble}};
	if (!TestTrue(TEXT("Cooked host module registers"), UEmkaHostFunctions::RegisterModule(TEXT("cook/host.um"),
		TEXT("fn UEmkaCookHostDouble*(Value: int): int"), Functions, Error))) return false;
	ON_SCOPE_EXIT { UEmkaHostFunctions::UnregisterModule(TEXT("cook/host.um"), Error); };
	UClass* Runner = LoadObject<UClass>(nullptr, TEXT("/Game/UEmkaCookValidation/Runner.Runner_C"));
	if (!TestNotNull(TEXT("Cooked host runner class loads"), Runner)) return false;
	UFunction* Function = Runner->FindFunctionByName(TEXT("ExecuteCookedHost"));
	if (!TestNotNull(TEXT("Cooked host function exists"), Function)) return false;
	UObject* Instance = NewObject<UObject>(GetTransientPackage(), Runner);
	FStructOnScope Values(Function);
	Instance->ProcessEvent(Function, Values.GetStructMemory());
	CheckStatus(*this, Function, Values.GetStructMemory(), true);
	CheckIntResult(*this, Function, Values.GetStructMemory(), 42);
	return true;
}

#endif
