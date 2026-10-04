// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
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
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
struct FExecutionGraph
{
	UBlueprint* Blueprint;
	UEdGraph* Graph;
	UK2Node_FunctionEntry* Entry;
	UK2Node_FunctionResult* Exit;
	UK2Node_UEmka* Node;
};

FExecutionGraph MakeExecutionGraph(const FString& Script, const bool bStatus = false, const bool bAsset = false,
	UPackage* Package = GetTransientPackage())
{
	FExecutionGraph Result;
	Result.Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("UEmkaExecution")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	Result.Graph = FBlueprintEditorUtils::CreateNewGraph(Result.Blueprint, TEXT("ExecuteTest"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Result.Blueprint, Result.Graph, true, nullptr);
	TArray<UK2Node_FunctionEntry*> Entries;
	Result.Graph->GetNodesOfClass(Entries);
	Result.Entry = Entries[0];
	Result.Exit = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Result.Entry);
	Result.Node = NewObject<UK2Node_UEmka>(Result.Graph, NAME_None, RF_Transactional);
	Result.Graph->AddNode(Result.Node);
	Result.Node->CreateNewGuid();
	Result.Node->Script = Script;
	Result.Node->bExposeRuntimeStatus = bStatus;
	if (bAsset)
	{
		Result.Node->ScriptAsset = NewObject<UUEmkaScriptAsset>(Package);
		Result.Node->ScriptAsset->Source = Script;
	}
	Result.Node->AllocateDefaultPins();
	return Result;
}

bool ConnectExecutionGraph(FAutomationTestBase& Test, const FExecutionGraph& Graph)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	bool bConnected = Schema->TryCreateConnection(Graph.Entry->GetThenPin(), Graph.Node->GetExecPin())
		&& Schema->TryCreateConnection(Graph.Node->GetThenPin(), Graph.Exit->GetExecPin());
	for (UEdGraphPin* Pin : Graph.Node->Pins)
	{
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
		UEdGraphPin* Endpoint = Pin->Direction == EGPD_Input
			? Graph.Entry->CreateUserDefinedPin(Pin->PinName, Pin->PinType, EGPD_Output)
			: Graph.Exit->CreateUserDefinedPin(FName(*(TEXT("Out") + Pin->PinName.ToString())), Pin->PinType, EGPD_Input);
		bConnected &= Schema->TryCreateConnection(Pin, Endpoint);
	}
	return Test.TestTrue(TEXT("Execution, arguments, return values and status connect"), bConnected);
}

UFunction* CompileExecutionGraph(FAutomationTestBase& Test, const FExecutionGraph& Graph)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Graph.Blueprint);
	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(Graph.Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
	if (!Test.TestEqual(TEXT("Execution node compiles"), Log.NumErrors, 0)) return nullptr;
	return Graph.Blueprint->GeneratedClass->FindFunctionByName(Graph.Graph->GetFName());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRuntimeStatusNodeTest, "UEmka.Editor.Execution.RuntimeStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRuntimeStatusNodeTest::RunTest(const FString& Parameters)
{
	for (const bool bAsset : {false, true})
	{
		for (const bool bMulti : {false, true})
		{
			const FString Script = bMulti
				? TEXT("fn Test*(Success: int, Error: int): (int, str) { return Success / Error, \"ok\" }")
				: TEXT("fn Test*(Success: int, Error: int): int { return Success / Error }");
			const FExecutionGraph Graph = MakeExecutionGraph(Script, true, bAsset);
			if (!ConnectExecutionGraph(*this, Graph)) return false;
			UFunction* Function = CompileExecutionGraph(*this, Graph);
			if (!TestNotNull(TEXT("Status function exists"), Function)) return false;
			UObject* Instance = NewObject<UObject>(GetTransientPackage(), Graph.Blueprint->GeneratedClass);
			FStructOnScope Args(Function);
			FInt64Property* Numerator = FindFProperty<FInt64Property>(Function, TEXT("Success"));
			FInt64Property* Denominator = FindFProperty<FInt64Property>(Function, TEXT("Error"));
			FBoolProperty* Success = FindFProperty<FBoolProperty>(Function, TEXT("OutSuccess"));
			FStrProperty* Error = FindFProperty<FStrProperty>(Function, TEXT("OutError"));
			FInt64Property* Result = FindFProperty<FInt64Property>(Function, bMulti ? TEXT("OutReturnValue1") : TEXT("OutReturnValue"));
			if (!TestNotNull(TEXT("Numerator input"), Numerator) || !TestNotNull(TEXT("Denominator input"), Denominator)
				|| !TestNotNull(TEXT("Success output"), Success) || !TestNotNull(TEXT("Error output"), Error)
				|| !TestNotNull(TEXT("Script return output"), Result)) return false;
			Numerator->SetPropertyValue_InContainer(Args.GetStructMemory(), 14);
			Denominator->SetPropertyValue_InContainer(Args.GetStructMemory(), 0);
			AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 1);
			Instance->ProcessEvent(Function, Args.GetStructMemory());
			TestFalse(TEXT("Runtime failure reaches Then and exposes false"), Success->GetPropertyValue_InContainer(Args.GetStructMemory()));
			TestTrue(TEXT("Runtime failure exposes diagnostic"), Error->GetPropertyValue_InContainer(Args.GetStructMemory()).Contains(TEXT("zero")));
			Denominator->SetPropertyValue_InContainer(Args.GetStructMemory(), 2);
			Instance->ProcessEvent(Function, Args.GetStructMemory());
			TestTrue(TEXT("Successful call exposes true"), Success->GetPropertyValue_InContainer(Args.GetStructMemory()));
			TestTrue(TEXT("Successful call clears previous diagnostic"), Error->GetPropertyValue_InContainer(Args.GetStructMemory()).IsEmpty());
			TestEqual(TEXT("Script return remains independent of runtime status"), Result->GetPropertyValue_InContainer(Args.GetStructMemory()), int64(7));
			if (bMulti)
			{
				FStrProperty* Other = FindFProperty<FStrProperty>(Function, TEXT("OutReturnValue2"));
				if (TestNotNull(TEXT("Second script result"), Other)) TestEqual(TEXT("Tuple result remains wired"), Other->GetPropertyValue_InContainer(Args.GetStructMemory()), FString(TEXT("ok")));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaExecutionOptionNodeTest, "UEmka.Editor.Execution.OptionsAndUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaExecutionOptionNodeTest::RunTest(const FString& Parameters)
{
	const FExecutionGraph Graph = MakeExecutionGraph(TEXT("fn Test*(Value: int): int { return Value }"));
	TestFalse(TEXT("Existing nodes remain stateless"), Graph.Node->ExecutionOptions.bUseSession);
	TestEqual(TEXT("Existing nodes have no instruction limit"), Graph.Node->ExecutionOptions.MaxInstructions, int64(0));
	TestFalse(TEXT("Runtime status defaults hidden"), Graph.Node->bExposeRuntimeStatus);
	TestEqual(TEXT("Default signature retains four pins"), Graph.Node->Pins.Num(), 4);
	TestNull(TEXT("Default signature has no Success output"), Graph.Node->FindPin(TEXT("Success"), EGPD_Output));
	TestNull(TEXT("Default signature has no Error output"), Graph.Node->FindPin(TEXT("Error"), EGPD_Output));
	if (!ConnectExecutionGraph(*this, Graph) || !TestNotNull(TEXT("Editor transaction system"), GEditor)) return false;
	UEdGraphPin* Source = Graph.Entry->FindPin(TEXT("Value"), EGPD_Output);
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("UEmka execution options")));
		Graph.Node->Modify();
		Graph.Node->ExecutionOptions.bUseSession = true;
		Graph.Node->ExecutionOptions.MaxInstructions = 1234;
		Graph.Node->bExposeRuntimeStatus = true;
		Graph.Node->RefreshScript();
	}
	TestNotNull(TEXT("Status option adds Success"), Graph.Node->FindPin(TEXT("Success"), EGPD_Output));
	TestTrue(TEXT("Status reconstruction preserves input connection"), Graph.Node->FindPin(TEXT("Value"), EGPD_Input)->LinkedTo.Contains(Source));
	TestTrue(TEXT("Execution option edit undoes"), GEditor->UndoTransaction());
	TestFalse(TEXT("Undo restores stateless option"), Graph.Node->ExecutionOptions.bUseSession);
	TestEqual(TEXT("Undo restores unlimited budget"), Graph.Node->ExecutionOptions.MaxInstructions, int64(0));
	TestNull(TEXT("Undo removes status output"), Graph.Node->FindPin(TEXT("Success"), EGPD_Output));
	TestTrue(TEXT("Execution option edit redoes"), GEditor->RedoTransaction());
	TestTrue(TEXT("Redo restores session option"), Graph.Node->ExecutionOptions.bUseSession);
	TestEqual(TEXT("Redo restores budget"), Graph.Node->ExecutionOptions.MaxInstructions, int64(1234));
	TestNotNull(TEXT("Redo restores status output"), Graph.Node->FindPin(TEXT("Success"), EGPD_Output));
	TestTrue(TEXT("Redo retains input connection"), Graph.Node->FindPin(TEXT("Value"), EGPD_Input)->LinkedTo.Contains(Source));
	Graph.Node->ExecutionOptions.MaxInstructions = 4321;
	Graph.Node->RefreshScript();
	TestTrue(TEXT("Budget edit preserves input connection"), Graph.Node->FindPin(TEXT("Value"), EGPD_Input)->LinkedTo.Contains(Source));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRuntimeStatusCollisionTest, "UEmka.Editor.Execution.StatusNameCollisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRuntimeStatusCollisionTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Result = struct { Success: bool; Error: str }\n")
		TEXT("fn Test*(Value: int): Result { return Result{7 / Value > 0, \"script result\"} }");
	const FExecutionGraph Graph = MakeExecutionGraph(Script, true);
	if (!TestNotNull(TEXT("Colliding Success label gets RuntimeSuccess status"), Graph.Node->FindPin(TEXT("RuntimeSuccess"), EGPD_Output))
		|| !TestNotNull(TEXT("Colliding Error label gets RuntimeError status"), Graph.Node->FindPin(TEXT("RuntimeError"), EGPD_Output))
		|| !ConnectExecutionGraph(*this, Graph)) return false;
	UFunction* Function = CompileExecutionGraph(*this, Graph);
	if (!TestNotNull(TEXT("Status collision function compiles"), Function)) return false;
	UObject* Instance = NewObject<UObject>(GetTransientPackage(), Graph.Blueprint->GeneratedClass);
	FStructOnScope Args(Function);
	FInt64Property* Value = FindFProperty<FInt64Property>(Function, TEXT("Value"));
	FBoolProperty* Status = FindFProperty<FBoolProperty>(Function, TEXT("OutRuntimeSuccess"));
	FStrProperty* Error = FindFProperty<FStrProperty>(Function, TEXT("OutRuntimeError"));
	FBoolProperty* ScriptSuccess = FindFProperty<FBoolProperty>(Function, TEXT("OutReturnValue1"));
	FStrProperty* ScriptError = FindFProperty<FStrProperty>(Function, TEXT("OutReturnValue2"));
	if (!TestNotNull(TEXT("Collision argument"), Value) || !TestNotNull(TEXT("Collision runtime status"), Status)
		|| !TestNotNull(TEXT("Collision runtime diagnostic"), Error) || !TestNotNull(TEXT("Independent script Success field"), ScriptSuccess)
		|| !TestNotNull(TEXT("Independent script Error field"), ScriptError)) return false;
	Value->SetPropertyValue_InContainer(Args.GetStructMemory(), 1);
	Instance->ProcessEvent(Function, Args.GetStructMemory());
	TestTrue(TEXT("Runtime success remains independent"), Status->GetPropertyValue_InContainer(Args.GetStructMemory()));
	TestTrue(TEXT("Script Success field remains wired"), ScriptSuccess->GetPropertyValue_InContainer(Args.GetStructMemory()));
	TestEqual(TEXT("Script Error field retains its return value"), ScriptError->GetPropertyValue_InContainer(Args.GetStructMemory()), FString(TEXT("script result")));
	TestTrue(TEXT("Runtime Error output is empty on success"), Error->GetPropertyValue_InContainer(Args.GetStructMemory()).IsEmpty());
	Value->SetPropertyValue_InContainer(Args.GetStructMemory(), 0);
	AddExpectedError(TEXT("zero"), EAutomationExpectedErrorFlags::Contains, 1);
	Instance->ProcessEvent(Function, Args.GetStructMemory());
	TestFalse(TEXT("Runtime failure uses renamed status output"), Status->GetPropertyValue_InContainer(Args.GetStructMemory()));
	TestTrue(TEXT("Runtime failure uses renamed diagnostic output"), Error->GetPropertyValue_InContainer(Args.GetStructMemory()).Contains(TEXT("zero")));
	TestFalse(TEXT("Runtime failure clears script Success result"), ScriptSuccess->GetPropertyValue_InContainer(Args.GetStructMemory()));
	TestTrue(TEXT("Runtime failure clears script Error result"), ScriptError->GetPropertyValue_InContainer(Args.GetStructMemory()).IsEmpty());
	Value->SetPropertyValue_InContainer(Args.GetStructMemory(), 1);
	Instance->ProcessEvent(Function, Args.GetStructMemory());
	TestTrue(TEXT("Recovery clears runtime diagnostic"), Error->GetPropertyValue_InContainer(Args.GetStructMemory()).IsEmpty());
	const FExecutionGraph Repeated = MakeExecutionGraph(TEXT("type Result = struct { Success, RuntimeSuccess: bool; Error, RuntimeError: str }\n")
		TEXT("fn Test*(): Result { return Result{true, false, \"script\", \"other\"} }"), true);
	TestNotNull(TEXT("Existing RuntimeSuccess label adds a suffix"), Repeated.Node->FindPin(TEXT("RuntimeSuccess1"), EGPD_Output));
	TestNotNull(TEXT("Existing RuntimeError label adds a suffix"), Repeated.Node->FindPin(TEXT("RuntimeError1"), EGPD_Output));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSessionNodeTest, "UEmka.Editor.Execution.SessionIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSessionNodeTest::RunTest(const FString& Parameters)
{
	const FExecutionGraph Graph = MakeExecutionGraph(TEXT("var Count: int\nfn Test*(): int { Count++; return Count }"), true);
	Graph.Node->ExecutionOptions.bUseSession = true;
	if (!ConnectExecutionGraph(*this, Graph)) return false;
	UFunction* Function = CompileExecutionGraph(*this, Graph);
	if (!TestNotNull(TEXT("Session function exists"), Function)) return false;
	UObject* First = NewObject<UObject>(GetTransientPackage(), Graph.Blueprint->GeneratedClass);
	UObject* Second = NewObject<UObject>(GetTransientPackage(), Graph.Blueprint->GeneratedClass);
	FStructOnScope Args(Function);
	FInt64Property* Result = FindFProperty<FInt64Property>(Function, TEXT("OutReturnValue"));
	if (!TestNotNull(TEXT("Session result property"), Result)) return false;
	First->ProcessEvent(Function, Args.GetStructMemory());
	TestEqual(TEXT("Compiled session begins with fresh globals"), Result->GetPropertyValue_InContainer(Args.GetStructMemory()), int64(1));
	First->ProcessEvent(Function, Args.GetStructMemory());
	TestEqual(TEXT("Node GUID retains session across calls"), Result->GetPropertyValue_InContainer(Args.GetStructMemory()), int64(2));
	Second->ProcessEvent(Function, Args.GetStructMemory());
	TestEqual(TEXT("DefaultToSelf separates caller sessions"), Result->GetPropertyValue_InContainer(Args.GetStructMemory()), int64(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaExecutionBudgetNodeTest, "UEmka.Editor.Execution.InstructionLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaExecutionBudgetNodeTest::RunTest(const FString& Parameters)
{
	const FExecutionGraph Graph = MakeExecutionGraph(TEXT("fn Test*(): int { n := 0; for n < 1000 { n++ }; return n }"), true);
	Graph.Node->ExecutionOptions.MaxInstructions = 100;
	if (!ConnectExecutionGraph(*this, Graph)) return false;
	UFunction* Function = CompileExecutionGraph(*this, Graph);
	if (!TestNotNull(TEXT("Budget function exists"), Function)) return false;
	UObject* Instance = NewObject<UObject>(GetTransientPackage(), Graph.Blueprint->GeneratedClass);
	FStructOnScope Args(Function);
	AddExpectedError(TEXT("Test:"), EAutomationExpectedErrorFlags::Contains, 1);
	Instance->ProcessEvent(Function, Args.GetStructMemory());
	FBoolProperty* Success = FindFProperty<FBoolProperty>(Function, TEXT("OutSuccess"));
	FStrProperty* Error = FindFProperty<FStrProperty>(Function, TEXT("OutError"));
	if (!TestNotNull(TEXT("Budget Success output"), Success) || !TestNotNull(TEXT("Budget Error output"), Error)) return false;
	TestFalse(TEXT("Compiled options enforce instruction budget"), Success->GetPropertyValue_InContainer(Args.GetStructMemory()));
	TestFalse(TEXT("Budget exhaustion produces diagnostic"), Error->GetPropertyValue_InContainer(Args.GetStructMemory()).IsEmpty());
	Graph.Node->ExecutionOptions.MaxInstructions = -1;
	FCompilerResultsLog Log;
	AddExpectedError(TEXT("Max Instructions must be zero or greater"), EAutomationExpectedErrorFlags::Contains, 1);
	Graph.Node->ValidateNodeDuringCompilation(Log);
	TestTrue(TEXT("Negative instruction budget is a compile error"), Log.NumErrors > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaExecutionPersistenceNodeTest, "UEmka.Editor.Execution.PackageRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaExecutionPersistenceNodeTest::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Temp/UEmkaExecution_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	const FExecutionGraph Graph = MakeExecutionGraph(TEXT("fn Test*(): int { return 7 }"), true, false, Package);
	Graph.Node->ExecutionOptions.bUseSession = true;
	Graph.Node->ExecutionOptions.MaxInstructions = 5678;
	const FGuid Guid = Graph.Node->NodeGuid;
	Graph.Blueprint->SetFlags(RF_Public | RF_Standalone);
	const FName BlueprintName = Graph.Blueprint->GetFName();
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/UEmka");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Filename = Directory / (FPaths::GetCleanFilename(PackageName) + TEXT(".uasset"));
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("Execution options save"), UPackage::SavePackage(Package, Graph.Blueprint, *Filename, SaveArgs))) return false;
	Package->Rename(*(PackageName + TEXT("_Original")), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
	UPackage* LoadedPackage = LoadPackage(nullptr, *Filename, LOAD_None);
	UBlueprint* LoadedBlueprint = LoadedPackage ? FindObject<UBlueprint>(LoadedPackage, *BlueprintName.ToString()) : nullptr;
	if (TestNotNull(TEXT("Execution Blueprint reloads"), LoadedBlueprint))
	{
		UEdGraph* LoadedGraph = nullptr;
		for (UEdGraph* Candidate : LoadedBlueprint->FunctionGraphs)
		{
			if (Candidate->GetFName() == TEXT("ExecuteTest")) LoadedGraph = Candidate;
		}
		TArray<UK2Node_UEmka*> Nodes;
		if (TestNotNull(TEXT("Execution graph reloads"), LoadedGraph)) LoadedGraph->GetNodesOfClass(Nodes);
		if (TestEqual(TEXT("Execution node reloads"), Nodes.Num(), 1))
		{
			TestTrue(TEXT("Session option persists"), Nodes[0]->ExecutionOptions.bUseSession);
			TestEqual(TEXT("Instruction limit persists"), Nodes[0]->ExecutionOptions.MaxInstructions, int64(5678));
			TestTrue(TEXT("Status option persists"), Nodes[0]->bExposeRuntimeStatus);
			TestEqual(TEXT("Session identity persists"), Nodes[0]->NodeGuid, Guid);
			TestNotNull(TEXT("Status pin persists"), Nodes[0]->FindPin(TEXT("Success"), EGPD_Output));
		}
		LoadedBlueprint->ClearFlags(RF_Standalone);
	}
	Graph.Blueprint->ClearFlags(RF_Standalone);
	IFileManager::Get().Delete(*Filename);
	return true;
}

#endif
