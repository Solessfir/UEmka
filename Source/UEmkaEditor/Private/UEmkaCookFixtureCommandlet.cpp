// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaCookFixtureCommandlet.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_UEmka.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UEmkaScriptAsset.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
UUEmkaScriptAsset* CreateModule(const FString& Name, const FString& Path, const FString& Source)
{
	UPackage* Package = CreatePackage(*(TEXT("/Game/UEmkaCookValidation/") + Name));
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>(Package, FName(*Name), RF_Public | RF_Standalone);
	Asset->ModulePath = Path;
	Asset->Source = Source;
	return Asset;
}

bool SaveFixture(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, Asset, *Filename, Args);
}
}

UUEmkaCookFixtureCommandlet::UUEmkaCookFixtureCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UUEmkaCookFixtureCommandlet::Main(const FString& Params)
{
	UUEmkaScriptAsset* Base = CreateModule(TEXT("Base"), TEXT("lib/base.um"),
		TEXT("const Default* = 7\nfn Twice*(Value: int): int { return Value * 2 }"));
	UUEmkaScriptAsset* Library = CreateModule(TEXT("Library"), TEXT("lib/library.um"),
		TEXT("import base = \"base.um\"\nconst Default* = base::Default\nfn Calculate*(Value: int): int { return base::Twice(Value) + 1 }"));
	Library->Imports.Add(Base);
	UUEmkaScriptAsset* Root = CreateModule(TEXT("Root"), TEXT("main.um"),
		TEXT("import library = \"lib/library.um\"\n")
		TEXT("type Row = struct { Number: int; Label: str }\n")
		TEXT("fn First*(): int { return -1 }\n")
		TEXT("fn Run*(Value: int = library::Default): (Row, map[str]Row, int) {\n")
		TEXT("    count := library::Calculate(Value)\n")
		TEXT("    row := Row{count, \"cooked 世界\"}\n")
		TEXT("    return row, map[str]Row{\"entry\": row}, count\n}"));
	Root->Imports.Add(Library);
	UPackage* Package = CreatePackage(TEXT("/Game/UEmkaCookValidation/Runner"));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package, TEXT("Runner"),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	if (!Blueprint) return 1;
	Blueprint->SetFlags(RF_Public | RF_Standalone);
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("ExecuteCooked"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (Entries.Num() != 1) return 1;
	UK2Node_FunctionEntry* Entry = Entries[0];
	UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
	if (!Exit) return 1;
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = TEXT("fn InlineFallback*(): int { return 999 }");
	Node->ScriptAsset = Root;
	Node->SelectedFunction = TEXT("Run");
	Node->bNativeStructPins = true;
	FString Error;
	int32 ErrorLine = -1;
	if (!Node->CompileCurrentScript(Root->Source, Error, ErrorLine))
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture script line %d: %s"), ErrorLine, *Error);
		return 1;
	}
	Node->AllocateDefaultPins();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!Schema->TryCreateConnection(Entry->GetThenPin(), Node->GetExecPin())
		|| !Schema->TryCreateConnection(Node->GetThenPin(), Exit->GetExecPin())) return 1;
	int32 OutputIndex = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction != EGPD_Output || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
		UEdGraphPin* Result = Exit->CreateUserDefinedPin(FName(*FString::Printf(TEXT("Out%d"), OutputIndex++)), Pin->PinType, EGPD_Input);
		if (!Schema->TryCreateConnection(Pin, Result)) return 1;
	}
	if (OutputIndex != 3) return 1;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
	if (Log.NumErrors || !Blueprint->GeneratedClass || (Blueprint->Status != BS_UpToDate && Blueprint->Status != BS_UpToDateWithWarnings)) return 1;
	for (UObject* Asset : TArray<UObject*>{Base, Library, Root, Blueprint})
	{
		if (!SaveFixture(Asset)) return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("Saved UEmka cooked validation fixtures under /Game/UEmkaCookValidation"));
	return 0;
}
