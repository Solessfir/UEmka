// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaFunctionLibrary.h"
#include "UEmkaScriptAsset.h"
#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "umka_api.h"

namespace
{
UUEmkaScriptAsset* MakeModule(const FString& Path, const FString& Source)
{
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>();
	Asset->ModulePath = Path;
	Asset->Source = Source;
	return Asset;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaModuleGraphTest, "UEmka.Modules.AssetGraphValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaModuleGraphTest::RunTest(const FString& Parameters)
{
	UUEmkaScriptAsset* Root = MakeModule(TEXT("main.um"), TEXT("fn Test*(): int { return 1 }"));
	UUEmkaScriptAsset* Base = MakeModule(TEXT("lib/base.um"), TEXT("const Value* = 3"));
	UUEmkaScriptAsset* Library = MakeModule(TEXT("lib/library.um"), TEXT("import \"base.um\"\nfn Value*(): int { return base::Value }"));
	Library->Imports = {Base};
	Root->Imports = {Library, Base};
	FString Source;
	FString FileName;
	FString Error;
	TArray<FUEmkaModuleSource> Modules;
	TestTrue(TEXT("Transitive graph resolves"), UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error));
	TestEqual(TEXT("Main source is retained"), Source, Root->Source);
	TestEqual(TEXT("Main virtual path is retained"), FileName, Root->ModulePath);
	TestEqual(TEXT("Shared import is registered once"), Modules.Num(), 2);
	Root->ModulePath.Reset();
	TestTrue(TEXT("New assets have distinct default paths"), UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error));
	TestEqual(TEXT("Default path follows asset name"), FileName, Root->GetName() + TEXT(".um"));
	Root->ModulePath = TEXT("main.um");
	UUEmkaScriptAsset* Conflict = MakeModule(Base->ModulePath, Base->Source);
	Root->Imports.Add(Conflict);
	TestFalse(TEXT("Distinct assets cannot claim the same module path"), UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error));
	TestTrue(TEXT("Conflict names the path and clears output"), Error.Contains(TEXT("Conflicting")) && Modules.IsEmpty() && Source.IsEmpty() && FileName.IsEmpty());
	Root->Imports = {Library};
	Base->Imports = {Root};
	TestFalse(TEXT("Import cycles fail"), UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error));
	TestTrue(TEXT("Cycle diagnostic"), Error.Contains(TEXT("Cyclic")));
	Base->Imports = {nullptr};
	TestFalse(TEXT("Missing retained reference fails"), UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error));
	TestTrue(TEXT("Missing reference diagnostic"), Error.Contains(TEXT("Missing")));
	Base->Imports.Reset();
	for (const FString& Invalid : {TEXT("/absolute.um"), TEXT("../outside.um"), TEXT("lib/./base.um"), TEXT("lib//base.um"), TEXT("C:\\base.um"), TEXT("base.txt")})
	{
		Base->ModulePath = Invalid;
		TestFalse(TEXT("Invalid path: ") + Invalid, UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error));
		TestTrue(TEXT("Invalid path diagnostic"), Error.Contains(TEXT("Invalid Umka module path")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaModuleRuntimeTest, "UEmka.Modules.ImportedSignaturesAndExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaModuleRuntimeTest::RunTest(const FString& Parameters)
{
	UUEmkaScriptAsset* Base = MakeModule(TEXT("lib/base.um"), TEXT("type Number* = int\nconst Default* = 7\nfn Twice*(Value: Number): Number { return Value * 2 }"));
	UUEmkaScriptAsset* Library = MakeModule(TEXT("lib/library.um"), TEXT("import b = \"base.um\"\ntype Number* = b::Number\nfn Calculate*(Value: Number): Number { return b::Twice(Value) + 1 }"));
	Library->Imports = {Base};
	UUEmkaScriptAsset* Root = MakeModule(TEXT("main.um"), TEXT("import (m = \"lib/library.um\"; b = \"lib/base.um\")\nfn Test*(Value: m::Number = b::Default): m::Number { return m::Calculate(Value) }\nfn Pair*(Value: m::Number): (m::Number, str) { return m::Calculate(Value), \"module\" }"));
	Root->Imports = {Library};
	FString Source;
	FString FileName;
	FString Error;
	int32 Line;
	TArray<FUEmkaModuleSource> Modules;
	if (!TestTrue(TEXT("Graph resolves"), UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error))) return false;
	TestTrue(TEXT("Asset source compiles without disk imports"), UUEmkaFunctionLibrary::CompileCheckScript(Source, Error, Line, Modules, FileName, false));
	FUEmkaCompiledSignature Signature;
	if (TestTrue(TEXT("Imported alias signature is inspected"), UUEmkaFunctionLibrary::InspectScriptFunction(Source, TEXT("Test"), Signature, Modules, FileName, false)))
	{
		TestEqual(TEXT("Alias resolves to scalar type"), Signature.Result.Type, EUEmkaValueType::Int);
		if (TestEqual(TEXT("One imported parameter"), Signature.Params.Num(), 1))
		{
			TestEqual(TEXT("Imported default is retained"), Signature.Params[0].DefaultValue, FString(TEXT("7")));
			TestTrue(TEXT("Imported default flag"), Signature.Params[0].bHasDefault);
		}
	}
	FUEmkaScriptParam Result;
	const FUEmkaScriptParam Param = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 7);
	TestTrue(TEXT("Asset executes registered transitive dependencies"), UUEmkaFunctionLibrary::RunUmkaAsset(nullptr, Root, Source, TEXT("Test"), {Param}, EUEmkaValueType::Int, false, false, Result, Error));
	TestEqual(TEXT("Imported execution result"), Result.IntValue, int64{15});
	TArray<FUEmkaScriptParam> Results;
	TestTrue(TEXT("Asset multi-return executes"), UUEmkaFunctionLibrary::RunUmkaAssetMulti(nullptr, Root, Source, TEXT("Pair"), {Param}, TEXT("0:0:8,12:0:8"), Results, Error));
	if (TestEqual(TEXT("Multi-return count"), Results.Num(), 2))
	{
		TestEqual(TEXT("Imported multi integer"), Results[0].IntValue, int64{15});
		TestEqual(TEXT("Imported multi string"), Results[1].StringValue, FString(TEXT("module")));
	}
	AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 3);
	TestFalse(TEXT("Missing asset cannot fall back to inline source"), UUEmkaFunctionLibrary::RunUmkaAsset(nullptr, nullptr, Source, TEXT("Test"), {Param}, EUEmkaValueType::Int, false, false, Result, Error));
	TestTrue(TEXT("Missing asset clears result"), Result.IntValue == 0 && Error.Contains(TEXT("Missing")));
	TestFalse(TEXT("Imported argument metadata is checked"), UUEmkaFunctionLibrary::RunUmkaAsset(nullptr, Root, Source, TEXT("Test"), {UUEmkaFunctionLibrary::MakeStrParam(TEXT("bad"))}, EUEmkaValueType::Int, false, false, Result, Error));
	TestFalse(TEXT("Imported result metadata is checked"), UUEmkaFunctionLibrary::RunUmkaAsset(nullptr, Root, Source, TEXT("Test"), {Param}, EUEmkaValueType::Str, false, false, Result, Error));
	AddExpectedError(TEXT("Pair:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Missing asset clears multi-return results"), UUEmkaFunctionLibrary::RunUmkaAssetMulti(nullptr, nullptr, Source, TEXT("Pair"), {Param}, TEXT("0:0:8,12:0:8"), Results, Error));
	TestTrue(TEXT("Missing asset clears all multi results"), Results.IsEmpty());
	Base->Source = TEXT("type Number* = str\nconst Default* = \"changed\"\nfn Twice*(Value: Number): Number { return Value }");
	TestTrue(TEXT("Changed import graph resolves"), UUEmkaScriptAsset::ResolveAsset(Root, Source, FileName, Modules, Error));
	TestFalse(TEXT("Imported incompatible signature produces a compiler error"), UUEmkaFunctionLibrary::CompileCheckScript(Source, Error, Line, Modules, FileName, false));
	TestTrue(TEXT("Compile error retains imported filename"), Error.Contains(TEXT("library.um")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaModuleReloadTest, "UEmka.Modules.SeparatePackageReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaModuleReloadTest::RunTest(const FString& Parameters)
{
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Mount = TEXT("/UEmkaModuleReload_") + Id + TEXT("/");
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/UEmkaModuleReload") / Id) + TEXT("/");
	IFileManager::Get().MakeDirectory(*Directory, true);
	FPackageName::RegisterMountPoint(Mount, Directory);
	const TArray<FString> Names = {Mount + TEXT("Base"), Mount + TEXT("Library"), Mount + TEXT("Root"), Mount + TEXT("Runner")};
	ON_SCOPE_EXIT
	{
		TArray<UPackage*> Packages;
		for (const FString& Name : Names)
			if (UPackage* Package = FindPackage(nullptr, *Name)) Packages.Add(Package);
		FText UnloadError;
		if (!Packages.IsEmpty()) UPackageTools::UnloadPackages(Packages, UnloadError, true);
		FPackageName::UnRegisterMountPoint(Mount, Directory);
		IFileManager::Get().DeleteDirectory(*Directory, false, true);
	};
	TArray<UPackage*> Packages;
	for (const FString& Name : Names) Packages.Add(CreatePackage(*Name));
	UUEmkaScriptAsset* Base = NewObject<UUEmkaScriptAsset>(Packages[0], TEXT("Base"), RF_Public | RF_Standalone);
	Base->ModulePath = TEXT("lib/base.um");
	Base->Source = TEXT("const Default* = 7\nfn Twice*(Value: int): int { return Value * 2 }");
	UUEmkaScriptAsset* Library = NewObject<UUEmkaScriptAsset>(Packages[1], TEXT("Library"), RF_Public | RF_Standalone);
	Library->ModulePath = TEXT("lib/library.um");
	Library->Source = TEXT("import base = \"base.um\"\nconst Default* = base::Default\nfn Calculate*(Value: int): int { return base::Twice(Value) + 1 }");
	Library->Imports = {Base};
	UUEmkaScriptAsset* Root = NewObject<UUEmkaScriptAsset>(Packages[2], TEXT("Root"), RF_Public | RF_Standalone);
	Root->ModulePath = TEXT("main.um");
	Root->Source = TEXT("import lib = \"lib/library.um\"\nfn First*(): int { return -1 }\nfn Run*(Value: int = lib::Default): int { return lib::Calculate(Value) }");
	Root->Imports = {Library};
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Packages[3], TEXT("Runner"),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!TestNotNull(TEXT("Reload Blueprint created"), Blueprint)) return false;
	Blueprint->SetFlags(RF_Public | RF_Standalone);
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!TestNotNull(TEXT("Reload Blueprint graph"), Graph)) return false;
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = TEXT("fn InlineFallback*(): int { return 999 }");
	Node->ScriptAsset = Root;
	Node->SelectedFunction = TEXT("Run");
	Node->AllocateDefaultPins();
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	for (UObject* Asset : TArray<UObject*>{Base, Library, Root, Blueprint})
	{
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (!TestTrue(TEXT("Separate dependency package saves"), UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))) return false;
	}
	FText UnloadError;
	if (!TestTrue(TEXT("Separate fixture packages unload"), UPackageTools::UnloadPackages(Packages, UnloadError, true))) return false;
	if (!TestNull(TEXT("Root script is unloaded before the Blueprint reload"), FindObject<UUEmkaScriptAsset>(nullptr, *(Names[2] + TEXT(".Root"))))) return false;
	UBlueprint* Loaded = LoadObject<UBlueprint>(nullptr, *(Names[3] + TEXT(".Runner")));
	if (!TestNotNull(TEXT("Blueprint reloads before explicitly loading its script assets"), Loaded)) return false;
	TArray<UK2Node_UEmka*> Nodes;
	FBlueprintEditorUtils::FindEventGraph(Loaded)->GetNodesOfClass(Nodes);
	if (!TestEqual(TEXT("Reloaded Blueprint preserves the script node"), Nodes.Num(), 1)) return false;
	TestEqual(TEXT("Reloaded node retains selected export"), Nodes[0]->SelectedFunction, FString(TEXT("Run")));
	TestNotNull(TEXT("Reloaded node reconstructs imported default parameter"), Nodes[0]->FindPin(TEXT("Value")));
	FUEmkaCompiledSignature Signature;
	FString Source, FileName, Error;
	TArray<FUEmkaModuleSource> Modules;
	if (!TestTrue(TEXT("Reloaded transitive asset graph resolves"), UUEmkaScriptAsset::ResolveAsset(Nodes[0]->ScriptAsset, Source, FileName, Modules, Error))) return false;
	TestEqual(TEXT("Reloaded transitive graph retains both modules"), Modules.Num(), 2);
	TestTrue(TEXT("Reloaded selected function compiles"), UUEmkaFunctionLibrary::InspectScriptFunction(Source, TEXT("Run"), Signature, Modules, FileName, false));
	FUEmkaScriptParam Result;
	TestTrue(TEXT("Reloaded graph executes embedded modules"), UUEmkaFunctionLibrary::RunUmkaAsset(nullptr, Nodes[0]->ScriptAsset, Source, TEXT("Run"),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 7)}, EUEmkaValueType::Int, false, false, Result, Error));
	TestEqual(TEXT("Reloaded graph result"), Result.IntValue, int64{15});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaManagedResultsTest, "UEmka.Modules.ManagedResultOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaManagedResultsTest::RunTest(const FString& Parameters)
{
	UUEmkaScriptAsset* Asset = MakeModule(TEXT("main.um"), TEXT("type Row = struct { Number: int; Label: str }\n")
		TEXT("fn Test*(Value: str): (str, map[str]Row, []Row, [2]str) {\n")
		TEXT(" label := Value + \" shared\"\n row := Row{15, label}\n")
		TEXT(" return label, map[str]Row{\"entry\": row}, []Row{row}, [2]str{label, label}\n}"));
	FString Error;
	TArray<FUEmkaScriptParam> Results;
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		if (!TestTrue(TEXT("Shared managed result executes"), UUEmkaFunctionLibrary::RunUmkaAssetMulti(nullptr, Asset, Asset->Source, TEXT("Test"),
			{UUEmkaFunctionLibrary::MakeStrParam(TEXT("native"))}, TEXT("12:0:8,15:0:8,15:1:8,12:2:8"), Results, Error))) return false;
		if (!TestEqual(TEXT("Managed multi-return field count"), Results.Num(), 4)) return false;
		TestEqual(TEXT("Copied scalar string survives VM destruction"), Results[0].StringValue, FString(TEXT("native shared")));
		TestTrue(TEXT("Copied record map survives VM destruction"), !Results[1].CompositeValue.IsEmpty());
		TestTrue(TEXT("Copied record array survives VM destruction"), !Results[2].CompositeValue.IsEmpty());
		TestTrue(TEXT("Copied static strings survive VM destruction"), Results[3].StringArrayValue == TArray<FString>{TEXT("native shared"), TEXT("native shared")});
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaManagedParameterFailureTest, "UEmka.Modules.ManagedParameterFailureCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaManagedParameterFailureTest::RunTest(const FString& Parameters)
{
	const FString Single = TEXT("fn Failure*(First: str, Second: int): str { return First }");
	const FString Multi = TEXT("fn MultiFailure*(First: str, Second: int): (str, int) { return First, Second }");
	const TArray<FUEmkaScriptParam> Valid = {UUEmkaFunctionLibrary::MakeStrParam(TEXT("native")), UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 7)};
	const TArray<FUEmkaScriptParam> Invalid = {Valid[0], UUEmkaFunctionLibrary::MakeStrParam(TEXT("bad second"))};
	FUEmkaScriptParam Result;
	FString Error;
	AddExpectedError(TEXT("] Failure:"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Single call releases a managed parameter before a later parameter failure"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Single, TEXT("Failure"), Invalid, EUEmkaValueType::Str, false, false, Result, Error));
	TestFalse(TEXT("Single call releases managed parameters on result metadata failure"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Single, TEXT("Failure"), Valid, EUEmkaValueType::Int, false, false, Result, Error));
	TestTrue(TEXT("Valid call consumes parameter ownership and releases returned ownership"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Single, TEXT("Failure"), Valid, EUEmkaValueType::Str, false, false, Result, Error));
	TestEqual(TEXT("Valid single result survives cleanup"), Result.StringValue, FString(TEXT("native")));
	TArray<FUEmkaScriptParam> Results;
	AddExpectedError(TEXT("] MultiFailure:"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Multi call releases a managed parameter before a later parameter failure"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Multi, TEXT("MultiFailure"), Invalid, TEXT("12:0:8,0:0:8"), Results, Error));
	TestFalse(TEXT("Multi call releases managed parameters on result metadata failure"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Multi, TEXT("MultiFailure"), Valid, TEXT("0:0:8,0:0:8"), Results, Error));
	TestTrue(TEXT("Valid multi call consumes parameter ownership"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Multi, TEXT("MultiFailure"), Valid, TEXT("12:0:8,0:0:8"), Results, Error));
	if (TestEqual(TEXT("Valid multi result count"), Results.Num(), 2))
	{
		TestEqual(TEXT("Valid multi string survives cleanup"), Results[0].StringValue, FString(TEXT("native")));
		TestEqual(TEXT("Valid multi integer"), Results[1].IntValue, int64{7});
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaModuleTypeNamesTest, "UEmka.Modules.ImportedStructAndEnumWrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaModuleTypeNamesTest::RunTest(const FString& Parameters)
{
	const FString LocalDefault = TEXT("type Child = struct { Label: str }\ntype Row = struct { Number: int; Detail: Child; Samples: [2]int }\nfn Run*(Input: Row = Row{73, Child{\"compiler\"}, [2]int{3, 5}}): Row { return Input }");
	FUEmkaCompiledSignature LocalSignature;
	if (TestTrue(TEXT("Local native record default is inspected"), UUEmkaFunctionLibrary::InspectScriptFunction(LocalDefault, TEXT("Run"), LocalSignature)))
	{
		TestTrue(TEXT("Local native record return remains supported"), LocalSignature.Result.bSupported);
		if (TestEqual(TEXT("Local native record default parameter count"), LocalSignature.Params.Num(), 1))
		{
			const FUEmkaCompiledValue& Input = LocalSignature.Params[0];
			TestTrue(TEXT("Native whole default encoding preserves supported metadata"), Input.bSupported && Input.bHasDefault && !Input.DefaultCompositeValue.IsEmpty());
			if (TestEqual(TEXT("Native default field count"), Input.Fields.Num(), 3))
			{
				if (TestEqual(TEXT("Native default nested field count"), Input.Fields[1].Fields.Num(), 1))
				{
					TestEqual(TEXT("Native nested string compiler default"), Input.Fields[1].Fields[0].DefaultValue, FString(TEXT("compiler")));
				}
				TestTrue(TEXT("Native static-array compiler default is encoded"), Input.Fields[2].bSupported && !Input.Fields[2].DefaultCompositeValue.IsEmpty());
			}
		}
	}
	const TArray<FUEmkaModuleSource> Modules = {{TEXT("types.um"), TEXT("type Point* = struct { X: int; Y: int }\ntype Mode* = enum(uint8) { Idle; Run }")}};
	const FString Source = TEXT("import foo = \"types.um\"\nfn Test*(Value: foo::Point = foo::Point{2, 3}): foo::Point { return Value }\nfn Pair*(Value: foo::Point): (foo::Point, str) { return Value, \"point\" }\nfn Enum*(Value: foo::Mode = foo::Mode.Run): foo::Mode { return Value }");
	FUEmkaCompiledSignature Compiled;
	FString Error;
	int32 Line;
	if (TestTrue(TEXT("Imported struct metadata"), UUEmkaFunctionLibrary::InspectScriptFunction(Source, TEXT("Test"), Compiled, Modules, TEXT("main.um"), false)))
	{
		TestEqual(TEXT("Struct type is qualified by compiler import alias"), Compiled.Result.TypeName, FString(TEXT("foo::Point")));
		if (TestEqual(TEXT("Imported struct parameter"), Compiled.Params.Num(), 1))
		{
			TestTrue(TEXT("Whole imported struct default is encoded"), Compiled.Params[0].bHasDefault && !Compiled.Params[0].DefaultCompositeValue.IsEmpty());
		}
	}
	if (TestTrue(TEXT("Imported enum metadata"), UUEmkaFunctionLibrary::InspectScriptFunction(Source, TEXT("Enum"), Compiled, Modules, TEXT("main.um"), false)))
	{
		TestEqual(TEXT("Enum type is qualified by compiler import alias"), Compiled.Result.TypeName, FString(TEXT("foo::Mode")));
		TestTrue(TEXT("Imported enum identity survives inspection"), Compiled.Result.bIsEnum && Compiled.Result.EnumByteSize == 1);
	}
	for (const FString& Function : {TEXT("Test"), TEXT("Pair"), TEXT("Enum")})
	{
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Source, Function, false, Modules, TEXT("main.um"), false);
		TestTrue(Function + TEXT(" imported signature generates pins"), Signature.bValid);
		TestTrue(Function + TEXT(" effective wrapper compiles"), UUEmkaFunctionLibrary::CompileCheckScript(UK2Node_UEmka::GetEffectiveScript(Source, Signature), Error, Line, Modules, TEXT("main.um"), false));
	}
	const FUEmkaSignature Native = UK2Node_UEmka::ParseScript(Source, TEXT("Test"), true, Modules, TEXT("main.um"), false);
	TestTrue(TEXT("Native imported struct pins retain whole default"), Native.bValid && Native.Params.Num() == 1 && !Native.Params[0].CompiledValue.DefaultCompositeValue.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaModuleIsolationTest, "UEmka.Modules.FilesystemIsolationAndBuiltinImports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaModuleIsolationTest::RunTest(const FString& Parameters)
{
	Umka* Uncompiled = umkaAlloc();
	if (!TestNotNull(TEXT("Uncompiled VM allocates"), Uncompiled)) return false;
	const bool bInitialized = umkaInit(Uncompiled, "script.um", "fn Test*() {}", 65536, nullptr, 0, nullptr, false, false, nullptr);
	TestTrue(TEXT("Uncompiled VM initializes"), bInitialized);
	umkaFree(Uncompiled);
	if (!bInitialized) return false;
	const FString DiskPath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("UmkaUnregistered"), TEXT(".um"));
	if (!TestTrue(TEXT("Prepare unregistered disk module"), FFileHelper::SaveStringToFile(TEXT("fn Value*(): int { return 99 }"), *DiskPath))) return false;
	const FString Script = FString::Printf(TEXT("import disk = \"%s\"\nfn Test*(): int { return disk::Value() }"), *DiskPath);
	FString Error;
	int32 Line;
	TestTrue(TEXT("Inline compiler preserves disk import compatibility"), UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, Line));
	TestFalse(TEXT("Asset compiler refuses an existing unregistered disk module"), UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, Line, {}, TEXT("main.um"), false));
	TestTrue(TEXT("Missing module diagnostic explains registration"), Error.Contains(TEXT("not registered")));
	IFileManager::Get().Delete(*DiskPath);
	TestTrue(TEXT("Builtin modules remain available without files"), UUEmkaFunctionLibrary::CompileCheckScript(TEXT("import \"std.um\"\nfn Test*(): real { return std::pi }"), Error, Line, {}, TEXT("main.um"), false));
	const TArray<FUEmkaModuleSource> Duplicate = {{TEXT("same.um"), TEXT("const Value* = 1")}, {TEXT("same.um"), TEXT("const Value* = 2")}};
	TestFalse(TEXT("Compiler API rejects duplicate module registration"), UUEmkaFunctionLibrary::CompileCheckScript(TEXT("fn Test*() {}"), Error, Line, Duplicate));
	TestTrue(TEXT("Module registration diagnostic"), Error.Contains(TEXT("Failed to register")));
	const FString LongPath = FString::ChrN(300, TEXT('x')) + TEXT(".um");
	TestFalse(TEXT("Compiler API rejects an oversized module path without cleanup bytecode"), UUEmkaFunctionLibrary::CompileCheckScript(TEXT("fn Test*() {}"), Error, Line, {{LongPath, TEXT("const Value* = 1")}}));
	TestTrue(TEXT("Oversized registration retains its diagnostic"), Error.Contains(TEXT("Failed to register")));
	FUEmkaCompiledSignature Signature;
	TestFalse(TEXT("Inspection fails for unregistered modules"), UUEmkaFunctionLibrary::InspectScriptFunction(TEXT("import \"missing.um\"\nfn Test*() {}"), TEXT("Test"), Signature, {}, TEXT("main.um"), false));
	TestFalse(TEXT("Inline compiler failure"), UUEmkaFunctionLibrary::CompileCheckScript(TEXT("fn Test*(): int {\n return Missing\n}"), Error, Line));
	TestEqual(TEXT("Inline diagnostic formatting remains compatible"), Error, FString(TEXT("Line 2: Unknown identifier Missing")));
	return true;
}

#endif
