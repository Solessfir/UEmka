// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "SGraphNode_UEmka.h"
#include "UEmkaScriptAsset.h"
#include "UEmkaScriptAssetFactory.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"

namespace
{
const FString OptionsScript = TEXT("fn First*(Value: int): int { return Value }\nfn Second*(Label: str): str { return Label }");

UK2Node_UEmka* MakeOptionsNode(UPackage* Package = GetTransientPackage())
{
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("UEmkaNodeOptions")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = OptionsScript;
	Node->AllocateDefaultPins();
	return Node;
}

TSharedPtr<SWidget> FindOptionsWidget(const TSharedRef<SWidget>& Root, FName Type)
{
	if (Root->GetType() == Type) return Root;
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<SWidget> Match = FindOptionsWidget(Children->GetChildAt(Index), Type)) return Match;
	}
	return nullptr;
}

TSharedPtr<SMultiLineEditableTextBox> FindScriptEditor(const TSharedRef<SWidget>& Root)
{
	if (Root->GetTag() == TEXT("UEmka.ScriptEditor")) return StaticCastSharedRef<SMultiLineEditableTextBox>(Root);
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<SMultiLineEditableTextBox> Editor = FindScriptEditor(Children->GetChildAt(Index))) return Editor;
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSelectedFunctionTest, "UEmka.Editor.Options.ExportedFunctionSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSelectedFunctionTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("// fn Comment*() {}\n/* fn Block*() {} */\n")
		TEXT("const Quoted = \"fn Quoted*() {}\"\n")
		TEXT("const Raw = `fn Raw*() {}\nfn MultilineRaw*() {}`\n")
		TEXT("fn Private() {}\n") + OptionsScript;
	const TArray<FString> Exports = UK2Node_UEmka::GetExportedFunctions(Script);
	if (TestEqual(TEXT("Discovery ignores comments, strings and private functions"), Exports.Num(), 2))
	{
		TestEqual(TEXT("Exports retain declaration order"), Exports[0], FString(TEXT("First")));
		TestEqual(TEXT("Second exported name"), Exports[1], FString(TEXT("Second")));
	}
	TestEqual(TEXT("Automatic selection keeps first export"), UK2Node_UEmka::ParseScript(Script).FunctionName, FString(TEXT("First")));
	const FUEmkaSignature Selected = UK2Node_UEmka::ParseScript(Script, TEXT("Second"));
	TestTrue(TEXT("Explicit export produces valid signature"), Selected.bValid && Selected.UnsupportedReason.IsEmpty());
	TestEqual(TEXT("Explicit export selects matching function"), Selected.FunctionName, FString(TEXT("Second")));
	if (TestEqual(TEXT("Selected function exposes its parameter"), Selected.Params.Num(), 1))
	{
		TestEqual(TEXT("Selected parameter name"), Selected.Params[0].Name, FString(TEXT("Label")));
		TestEqual(TEXT("Selected parameter type"), Selected.Params[0].Type, EUEmkaValueType::Str);
	}
	for (const FString& Name : {FString(TEXT("Missing")), FString(TEXT("Private")), FString(TEXT("second"))})
	{
		const FUEmkaSignature Missing = UK2Node_UEmka::ParseScript(Script, Name);
		TestFalse(TEXT("Missing, private or wrong-case choice reports a diagnostic"), Missing.UnsupportedReason.IsEmpty());
		TestEqual(TEXT("Rejected selection never falls back to another export"), Missing.FunctionName, Name);
		TestTrue(TEXT("Rejected choice exposes no data pins"), Missing.Params.IsEmpty() && !Missing.ReturnType.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaOptionsTransactionTest, "UEmka.Editor.Options.TransactionUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaOptionsTransactionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Editor transaction system exists"), GEditor)) return false;
	UK2Node_UEmka* Node = MakeOptionsNode();
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	Blueprint->Status = BS_UpToDate;
	TestTrue(TEXT("Node options appear in Details"), Node->ShouldShowNodeProperties());
	TestFalse(TEXT("Native struct pins default off"), Node->bNativeStructPins);
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("UEmka automation options")));
		Node->Modify();
		Node->SelectedFunction = TEXT("Second");
		Node->bNativeStructPins = true;
		Node->RefreshScript();
	}
	TestEqual(TEXT("Option edit marks Blueprint dirty"), Blueprint->Status, BS_Dirty);
	TestNotNull(TEXT("Selection reconstructs matching pin"), Node->FindPin(TEXT("Label")));
	TestNull(TEXT("Selection removes previous parameter"), Node->FindPin(TEXT("Value")));
	TestTrue(TEXT("Options transaction undoes"), GEditor->UndoTransaction());
	TestTrue(TEXT("Undo restores automatic selection"), Node->SelectedFunction.IsEmpty());
	TestFalse(TEXT("Undo restores struct option"), Node->bNativeStructPins);
	TestNotNull(TEXT("Undo reconstructs first function"), Node->FindPin(TEXT("Value")));
	TestTrue(TEXT("Options transaction redoes"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores selection"), Node->SelectedFunction, FString(TEXT("Second")));
	TestTrue(TEXT("Redo restores struct option"), Node->bNativeStructPins);
	TestNotNull(TEXT("Redo reconstructs selected function"), Node->FindPin(TEXT("Label")));
	TestEqual(TEXT("Options preserve inline source"), Node->Script, OptionsScript);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaOptionsPersistenceTest, "UEmka.Editor.Options.PackageRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaOptionsPersistenceTest::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Temp/UEmkaOptions_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	UK2Node_UEmka* Node = MakeOptionsNode(Package);
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	Blueprint->SetFlags(RF_Public | RF_Standalone);
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>(Package, TEXT("ReusableScript"), RF_Public | RF_Standalone);
	Asset->Source = OptionsScript;
	Node->ScriptAsset = Asset;
	Node->SelectedFunction = TEXT("Second");
	Node->bNativeStructPins = true;
	Node->RefreshScript();
	const FName BlueprintName = Blueprint->GetFName();
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/UEmka");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Filename = Directory / (FPaths::GetCleanFilename(PackageName) + TEXT(".uasset"));
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("Options package saves"), UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))) return false;
	Package->Rename(*(PackageName + TEXT("_Original")), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
	UPackage* LoadedPackage = LoadPackage(nullptr, *Filename, LOAD_None);
	UBlueprint* LoadedBlueprint = LoadedPackage ? FindObject<UBlueprint>(LoadedPackage, *BlueprintName.ToString()) : nullptr;
	if (TestNotNull(TEXT("Options Blueprint reloads"), LoadedBlueprint))
	{
		TArray<UK2Node_UEmka*> LoadedNodes;
		FBlueprintEditorUtils::FindEventGraph(LoadedBlueprint)->GetNodesOfClass(LoadedNodes);
		if (TestEqual(TEXT("Options node round trips"), LoadedNodes.Num(), 1))
		{
			TestEqual(TEXT("Selected export persists"), LoadedNodes[0]->SelectedFunction, FString(TEXT("Second")));
			TestTrue(TEXT("Native struct option persists"), LoadedNodes[0]->bNativeStructPins);
			TestNotNull(TEXT("Reusable asset reference persists"), LoadedNodes[0]->ScriptAsset.Get());
			TestEqual(TEXT("Inline fallback persists"), LoadedNodes[0]->Script, OptionsScript);
			TestNotNull(TEXT("Loaded node reconstructs selected function"), LoadedNodes[0]->FindPin(TEXT("Label")));
			TestNull(TEXT("Loaded node excludes automatic function pin"), LoadedNodes[0]->FindPin(TEXT("Value")));
		}
		LoadedBlueprint->ClearFlags(RF_Standalone);
	}
	Blueprint->ClearFlags(RF_Standalone);
	Asset->ClearFlags(RF_Standalone);
	IFileManager::Get().Delete(*Filename);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaAssetReadOnlyEditorTest, "UEmka.Editor.Options.AssetSourceReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaAssetReadOnlyEditorTest::RunTest(const FString& Parameters)
{
	UK2Node_UEmka* Node = MakeOptionsNode();
	UUEmkaScriptAssetFactory* Factory = NewObject<UUEmkaScriptAssetFactory>();
	TestTrue(TEXT("Reusable scripts can be created from the asset menu"), Factory->ShouldShowInNewMenu());
	UUEmkaScriptAsset* Asset = Cast<UUEmkaScriptAsset>(Factory->FactoryCreateNew(UUEmkaScriptAsset::StaticClass(),
		GetTransientPackage(), NAME_None, RF_Transactional, nullptr, nullptr));
	if (!TestNotNull(TEXT("Factory creates reusable script"), Asset)) return false;
	Asset->Source = TEXT("fn AssetFunction*() {}");
	Node->ScriptAsset = Asset;
	Node->RefreshScript();
	const TSharedRef<SGraphNode_UEmka> Widget = SNew(SGraphNode_UEmka, Node);
	Widget->SlatePrepass(1.f);
	const TSharedPtr<SMultiLineEditableTextBox> Editor = FindScriptEditor(Widget);
	const TSharedPtr<SWidget> FoundText = Editor.IsValid() ? FindOptionsWidget(Editor.ToSharedRef(), TEXT("SMultiLineEditableText")) : nullptr;
	if (!TestTrue(TEXT("Reusable node contains source editor"), Editor.IsValid() && FoundText.IsValid())) return false;
	const TSharedPtr<SMultiLineEditableText> Text = StaticCastSharedPtr<SMultiLineEditableText>(FoundText);
	TestTrue(TEXT("Asset source editor is read only"), Text->IsTextReadOnly());
	TestEqual(TEXT("Embedded editor displays asset source"), Editor->GetPlainText().ToString(), Asset->Source);
	Editor->GoTo(FTextLocation(0, 0));
	FoundText->OnKeyChar(FGeometry(), FCharacterEvent(TEXT('x'), FModifierKeysState(), 0, false));
	FoundText->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Tab, FModifierKeysState(), 0, false, 0, 0));
	TestEqual(TEXT("Typing and indentation cannot alter asset source"), Editor->GetPlainText().ToString(), Asset->Source);
	Editor->SetText(FText::FromString(TEXT("fn AccidentalCommit*() {}")));
	FoundText->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("Read-only commit never overwrites inline fallback"), Node->Script, OptionsScript);
	TestEqual(TEXT("Read-only commit never overwrites reusable source"), Asset->Source, FString(TEXT("fn AssetFunction*() {}")));
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	Blueprint->Status = BS_UpToDate;
	Asset->Source = TEXT("fn AssetFunction*(Label: str): str { return Label }");
	Asset->PostEditChange();
	TestNotNull(TEXT("Asset edit automatically reconstructs dependent node"), Node->FindPin(TEXT("Label")));
	TestEqual(TEXT("Asset edit marks dependent Blueprint dirty"), Blueprint->Status, BS_Dirty);
	TestEqual(TEXT("Asset refresh preserves inline fallback"), Node->Script, OptionsScript);
	Widget->UpdateGraphNode();
	Widget->SlatePrepass(1.f);
	const TSharedPtr<SMultiLineEditableTextBox> UpdatedEditor = FindScriptEditor(Widget);
	if (!TestTrue(TEXT("Rebuilt node contains source editor"), UpdatedEditor.IsValid())) return false;
	TestEqual(TEXT("Rebuilt editor binds refreshed asset source"), UpdatedEditor->GetText().ToString(), Asset->Source);
	TestEqual(TEXT("Rebuilt editor displays refreshed asset source"), UpdatedEditor->GetPlainText().ToString(), Asset->Source);
	Node->ScriptAsset = nullptr;
	Node->RefreshScript();
	Widget->UpdateGraphNode();
	Widget->SlatePrepass(1.f);
	const TSharedPtr<SMultiLineEditableTextBox> RestoredEditor = FindScriptEditor(Widget);
	if (!TestTrue(TEXT("Removing asset retains source editor"), RestoredEditor.IsValid())) return false;
	const TSharedPtr<SWidget> RestoredText = FindOptionsWidget(RestoredEditor.ToSharedRef(), TEXT("SMultiLineEditableText"));
	TestFalse(TEXT("Removing asset restores inline editing"), StaticCastSharedPtr<SMultiLineEditableText>(RestoredText)->IsTextReadOnly());
	return true;
}

#endif
