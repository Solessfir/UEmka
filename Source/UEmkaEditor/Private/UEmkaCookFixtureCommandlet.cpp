// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaCookFixtureCommandlet.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_UEmka.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UEmkaHostFunctions.h"
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

void CookHostDouble(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
	umkaGetResult(Params, Result)->intVal = umkaGetParam(Params, 0)->intVal * 2;
}

bool ConnectFixturePins(const UEdGraphSchema_K2* Schema, UEdGraphPin* Source, UEdGraphPin* Target, const FString& Context)
{
	if (!Source || !Target)
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture %s: missing connection pin"), *Context);
		return false;
	}
	if (Schema->TryCreateConnection(Source, Target)) return true;
	UE_LOG(LogTemp, Error, TEXT("Cook fixture %s: cannot connect '%s' to '%s': %s"), *Context,
		*Source->PinName.ToString(), *Target->PinName.ToString(), *Schema->CanCreateConnection(Source, Target).Message.ToString());
	return false;
}

bool AddValidationFunction(UBlueprint* Blueprint, const FName Name, const FString& Script,
	const FUEmkaExecutionOptions& Options = {}, const bool bRuntimeStatus = false, const bool bConnectInputs = true)
{
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (Entries.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture function '%s': expected one entry, found %d"), *Name.ToString(), Entries.Num());
		return false;
	}
	UK2Node_FunctionEntry* Entry = Entries[0];
	UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
	if (!Exit)
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture function '%s': could not create result node"), *Name.ToString());
		return false;
	}
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = Script;
	Node->ExecutionOptions = Options;
	Node->bExposeRuntimeStatus = bRuntimeStatus;
	FString Error;
	int32 ErrorLine = -1;
	if (!Node->CompileCurrentScript(Script, Error, ErrorLine))
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture function '%s' script line %d: %s"), *Name.ToString(), ErrorLine, *Error);
		return false;
	}
	Node->AllocateDefaultPins();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!ConnectFixturePins(Schema, Entry->GetThenPin(), Node->GetExecPin(), Name.ToString())
		|| !ConnectFixturePins(Schema, Node->GetThenPin(), Exit->GetExecPin(), Name.ToString())) return false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
		if (Pin->Direction == EGPD_Input && bConnectInputs)
		{
			UEdGraphPin* Source = Entry->CreateUserDefinedPin(Pin->PinName, Pin->PinType, EGPD_Output);
			if (!ConnectFixturePins(Schema, Source, Pin, Name.ToString())) return false;
		}
		if (Pin->Direction == EGPD_Output)
		{
			UEdGraphPin* Target = Exit->CreateUserDefinedPin(FName(*(TEXT("Out") + Pin->PinName.ToString())), Pin->PinType, EGPD_Input);
			if (!ConnectFixturePins(Schema, Pin, Target, Name.ToString())) return false;
		}
	}
	return true;
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
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture could not create Runner Blueprint"));
		return 1;
	}
	Blueprint->SetFlags(RF_Public | RF_Standalone);
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("ExecuteCooked"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Graph->GetNodesOfClass(Entries);
	if (Entries.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture ExecuteCooked: expected one entry, found %d"), Entries.Num());
		return 1;
	}
	UK2Node_FunctionEntry* Entry = Entries[0];
	UK2Node_FunctionResult* Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
	if (!Exit)
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture ExecuteCooked: could not create result node"));
		return 1;
	}
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
	if (!ConnectFixturePins(Schema, Entry->GetThenPin(), Node->GetExecPin(), TEXT("ExecuteCooked"))
		|| !ConnectFixturePins(Schema, Node->GetThenPin(), Exit->GetExecPin(), TEXT("ExecuteCooked"))) return 1;
	int32 OutputIndex = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction != EGPD_Output || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
		UEdGraphPin* Result = Exit->CreateUserDefinedPin(FName(*FString::Printf(TEXT("Out%d"), OutputIndex++)), Pin->PinType, EGPD_Input);
		if (!ConnectFixturePins(Schema, Pin, Result, TEXT("ExecuteCooked"))) return 1;
	}
	if (OutputIndex != 3)
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture ExecuteCooked: expected three outputs, found %d"), OutputIndex);
		FCompilerResultsLog ValidationLog;
		Node->ValidateNodeDuringCompilation(ValidationLog);
		return 1;
	}
	const FString NativeScript = TEXT("import u = \"ue.um\"\n")
		TEXT("fn RoundTrip*(V: u::Vector = u::Vector{1.25, -2.5, 3.75}, T: u::Transform = u::Transform{u::Quat{0, 0, 0, 1}, u::Vector{0, 0, 0}, u::Vector{1, 1, 1}}): (u::Vector, u::Transform) { return V, T }");
	FUEmkaExecutionOptions SessionOptions;
	SessionOptions.bUseSession = true;
	SessionOptions.MaxHeapBytes = 2 * 1024 * 1024;
	FUEmkaExecutionOptions BudgetOptions;
	BudgetOptions.MaxInstructions = 100;
	FUEmkaExecutionOptions HeapOptions;
	HeapOptions.MaxHeapBytes = 2 * 1024 * 1024;
	const FString HeapScript = TEXT("fn Allocate*(Count: int): int { Values := make([]int, Count); return len(Values) }");
	const TArray<UEmkaHostFunctions::FFunction> HostFunctions = {{TEXT("UEmkaCookHostDouble"), &CookHostDouble}};
	if (!UEmkaHostFunctions::RegisterModule(TEXT("cook/host.um"), TEXT("fn UEmkaCookHostDouble*(Value: int): int"), HostFunctions, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture host registration: %s"), *Error);
		return 1;
	}
	ON_SCOPE_EXIT { UEmkaHostFunctions::UnregisterModule(TEXT("cook/host.um"), Error); };
	if (!AddValidationFunction(Blueprint, TEXT("ExecuteCookedNative"), NativeScript)
		|| !AddValidationFunction(Blueprint, TEXT("ExecuteCookedNativeDefaults"), NativeScript, {}, false, false)
		|| !AddValidationFunction(Blueprint, TEXT("ExecuteCookedSession"), TEXT("var Count: int\nfn CountCalls*(): int { Count++; return Count }"), SessionOptions, true)
		|| !AddValidationFunction(Blueprint, TEXT("ExecuteCookedStatus"), TEXT("fn Divide*(Value: int): int { return 7 / Value }"), {}, true)
		|| !AddValidationFunction(Blueprint, TEXT("ExecuteCookedBudget"), TEXT("fn Budget*(): int { n := 0; for n < 2000 { n++ }; return n }"), BudgetOptions, true)
		|| !AddValidationFunction(Blueprint, TEXT("ExecuteCookedHeap"), HeapScript, HeapOptions, true)
		|| !AddValidationFunction(Blueprint, TEXT("ExecuteCookedSessionHeap"), HeapScript, SessionOptions, true)
		|| !AddValidationFunction(Blueprint, TEXT("ExecuteCookedHost"), TEXT("import \"cook/host.um\"\nfn Host*(): int { return host::UEmkaCookHostDouble(21) }"), {}, true)) return 1;
	UEdGraph* ResetGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("ResetCookedCaller"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, ResetGraph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> ResetEntries;
	ResetGraph->GetNodesOfClass(ResetEntries);
	if (ResetEntries.Num() != 1) return 1;
	UK2Node_FunctionResult* ResetExit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(ResetEntries[0]);
	UK2Node_CallFunction* ResetNode = NewObject<UK2Node_CallFunction>(ResetGraph);
	ResetGraph->AddNode(ResetNode);
	ResetNode->CreateNewGuid();
	ResetNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, ResetRuntimeSessionsForCaller), UUEmkaFunctionLibrary::StaticClass());
	ResetNode->AllocateDefaultPins();
	if (!ConnectFixturePins(Schema, ResetEntries[0]->GetThenPin(), ResetNode->GetExecPin(), TEXT("ResetCookedCaller"))
		|| !ConnectFixturePins(Schema, ResetNode->GetThenPin(), ResetExit ? ResetExit->GetExecPin() : nullptr, TEXT("ResetCookedCaller"))) return 1;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
	if (Log.NumErrors || !Blueprint->GeneratedClass || (Blueprint->Status != BS_UpToDate && Blueprint->Status != BS_UpToDateWithWarnings))
	{
		UE_LOG(LogTemp, Error, TEXT("Cook fixture Runner compilation failed: %d errors, Blueprint status %d"), Log.NumErrors, static_cast<int32>(Blueprint->Status));
		return 1;
	}
	for (UObject* Asset : TArray<UObject*>{Base, Library, Root, Blueprint})
	{
		if (!SaveFixture(Asset))
		{
			UE_LOG(LogTemp, Error, TEXT("Cook fixture failed to save '%s'"), *Asset->GetPathName());
			return 1;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Saved UEmka cooked validation fixtures under /Game/UEmkaCookValidation"));
	return 0;
}
