// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDetailsView.h"
#include "Input/HittestGrid.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PropertyEditorModule.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "SGraphNode_UEmka.h"
#include "UEmkaScriptAsset.h"
#include "UEmkaScriptAssetFactory.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Types/PaintArgs.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
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
	if (Root->GetType() == Type || Root->GetTag() == Type) return Root;
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

TSharedPtr<STextBlock> FindOptionsText(const TSharedRef<SWidget>& Root, const FString& Text)
{
	if (Root->GetType() == TEXT("STextBlock"))
	{
		const TSharedRef<STextBlock> Label = StaticCastSharedRef<STextBlock>(Root);
		if (Label->GetText().ToString() == Text) return Label;
	}
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<STextBlock> Match = FindOptionsText(Children->GetChildAt(Index), Text)) return Match;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaFunctionSelectorVisibilityTest, "UEmka.Editor.Options.FunctionSelectorVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaFunctionSelectorVisibilityTest::RunTest(const FString& Parameters)
{
	UK2Node_UEmka* Node = MakeOptionsNode();
	Node->Script = TEXT("fn Only*(Value: int): int { return Value }");
	Node->RefreshScript();
	const TSharedRef<SGraphNode_UEmka> Widget = SNew(SGraphNode_UEmka, Node);
	const auto GetSelector = [&]()
	{
		return StaticCastSharedPtr<SComboBox<TSharedPtr<FString>>>(FindOptionsWidget(Widget, TEXT("UEmka.FunctionSelector")));
	};
	TSharedPtr<SComboBox<TSharedPtr<FString>>> Selector = GetSelector();
	if (!TestTrue(TEXT("Function selector widget exists"), Selector.IsValid())) return false;
	TestTrue(TEXT("Single export hides the selector"), Selector->GetVisibility() == EVisibility::Collapsed);
	if (TestTrue(TEXT("Single export is selected"), Selector->GetSelectedItem().IsValid()))
	{
		TestEqual(TEXT("Default selection is the actual function"), *Selector->GetSelectedItem(), FString(TEXT("Only")));
	}
	Node->OnScriptChanged(OptionsScript);
	Widget->UpdateGraphNode();
	Selector = GetSelector();
	TestTrue(TEXT("Multiple exports show the selector"), Selector->GetVisibility() == EVisibility::Visible);
	if (TestTrue(TEXT("First export is selected by default"), Selector->GetSelectedItem().IsValid()))
	{
		TestEqual(TEXT("Default entry names the first export"), *Selector->GetSelectedItem(), FString(TEXT("First")));
	}
	TSharedPtr<STextBlock> Label = StaticCastSharedPtr<STextBlock>(FindOptionsWidget(Widget, TEXT("UEmka.FunctionSelectorLabel")));
	if (TestTrue(TEXT("Selector label exists"), Label.IsValid()))
	{
		TestEqual(TEXT("Default label names the function"), Label->GetText().ToString(), FString(TEXT("First")));
	}
	Node->SelectedFunction = TEXT("Second");
	Node->RefreshScript();
	Widget->UpdateGraphNode();
	Selector = GetSelector();
	if (TestTrue(TEXT("Explicit export is selected"), Selector->GetSelectedItem().IsValid()))
	{
		TestEqual(TEXT("Explicit selection remains intact"), *Selector->GetSelectedItem(), FString(TEXT("Second")));
	}
	Node->SelectedFunction.Reset();
	Node->OnScriptChanged(TEXT("fn Only*() {}"));
	Widget->UpdateGraphNode();
	TestTrue(TEXT("Returning to one export hides the selector"), GetSelector()->GetVisibility() == EVisibility::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaOptionsDetailsPresentationTest, "UEmka.Editor.Options.DetailsPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaOptionsDetailsPresentationTest::RunTest(const FString& Parameters)
{
	FProperty* PreserveProperty = FUEmkaExecutionOptions::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FUEmkaExecutionOptions, bUseSession));
	if (!TestNotNull(TEXT("Serialized session option remains reflected"), PreserveProperty)) return false;
	TestEqual(TEXT("Session option has the requested display name"), PreserveProperty->GetDisplayNameText().ToString(), FString(TEXT("Preserve Script State")));
	TestEqual(TEXT("Session tooltip comes from its concise comment"), PreserveProperty->GetToolTipText().ToString(), FString(TEXT("Keep script globals between calls on this node and Blueprint instance.")));
	for (const FName Name : {GET_MEMBER_NAME_CHECKED(FUEmkaExecutionOptions, bUseSession), GET_MEMBER_NAME_CHECKED(FUEmkaExecutionOptions, MaxInstructions),
		GET_MEMBER_NAME_CHECKED(FUEmkaExecutionOptions, MaxHeapBytes), GET_MEMBER_NAME_CHECKED(FUEmkaExecutionOptions, bResetSession)})
	{
		FProperty* Property = FUEmkaExecutionOptions::StaticStruct()->FindPropertyByName(Name);
		if (!TestNotNull(TEXT("Execution option remains reflected"), Property)) return false;
		TestEqual(TEXT("Execution options use the flat Umka category"), Property->GetMetaData(TEXT("Category")), FString(TEXT("Umka")));
		TestFalse(TEXT("Execution option comments generate tooltips"), Property->GetToolTipText().IsEmpty());
	}
	FProperty* StructProperty = UK2Node_UEmka::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, bNativeStructPins));
	FProperty* StatusProperty = UK2Node_UEmka::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, bExposeRuntimeStatus));
	if (!TestNotNull(TEXT("Custom struct option remains reflected"), StructProperty) || !TestNotNull(TEXT("Status option remains reflected"), StatusProperty)) return false;
	TestEqual(TEXT("Custom struct option has an explicit display name"), StructProperty->GetDisplayNameText().ToString(), FString(TEXT("Use Custom Struct Pins")));
	TestEqual(TEXT("Status option describes the visible pins"), StatusProperty->GetDisplayNameText().ToString(), FString(TEXT("Show Success and Error Pins")));
	TestFalse(TEXT("Custom struct comment generates its tooltip"), StructProperty->GetToolTipText().IsEmpty());
	TestFalse(TEXT("Status comment generates its tooltip"), StatusProperty->GetToolTipText().IsEmpty());

	UK2Node_UEmka* Node = MakeOptionsNode();
	FDetailsViewArgs Args;
	Args.bUpdatesFromSelection = false;
	Args.bAllowSearch = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	const TSharedRef<IDetailsView> View = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	View->SetObject(Node);
	FSlateApplication& Application = FSlateApplication::Get();
	const TSharedPtr<SWidget> PreviousFocus = Application.GetKeyboardFocusedWidget();
	const TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(FVector2D(800, 800));
	Window->SetContent(View);
	Window->SetIsFocusable(true);
	Application.RegisterVirtualWindow(Window);
	ON_SCOPE_EXIT
	{
		if (PreviousFocus.IsValid()) Application.SetKeyboardFocus(PreviousFocus);
		else Application.ClearKeyboardFocus();
		Application.UnregisterVirtualWindow(Window);
	};
	Application.Tick(ESlateTickType::TimeAndWidgets);
	View->SlatePrepass(1.f);
	FSlateWindowElementList Elements(Window);
	FHittestGrid HitTestGrid;
	const FPaintArgs PaintArgs(&Window.Get(), HitTestGrid, FVector2D::ZeroVector, Application.GetCurrentTime(), Application.GetDeltaTime());
	View->Paint(PaintArgs, FGeometry::MakeRoot(FVector2D(800, 800), FSlateLayoutTransform()),
		FSlateRect(0, 0, 800, 800), Elements, 0, FWidgetStyle(), true);
	for (const FString& Label : {FString(TEXT("Umka")), FString(TEXT("Preserve Script State")), FString(TEXT("Max Instructions")), FString(TEXT("Max Heap Bytes")),
		FString(TEXT("Use Custom Struct Pins")), FString(TEXT("Show Success and Error Pins"))})
	{
		TestTrue(*FString::Printf(TEXT("Actual Details view shows '%s'"), *Label), FindOptionsText(View, Label).IsValid());
	}
	TestFalse(TEXT("Actual Details view has no Execution subsection"), FindOptionsText(View, TEXT("Execution")).IsValid());
	TestFalse(TEXT("Actual Details view no longer shows Use Session"), FindOptionsText(View, TEXT("Use Session")).IsValid());
	TSharedPtr<SWidget> PreserveRow = FindOptionsText(View, TEXT("Preserve Script State"));
	while (PreserveRow.IsValid() && PreserveRow->GetType() != TEXT("SDetailSingleItemRow")) PreserveRow = PreserveRow->GetParentWidget();
	if (!TestTrue(TEXT("Preserve option belongs to a native Details row"), PreserveRow.IsValid())) return false;
	const TSharedPtr<SWidget> BoolEditor = FindOptionsWidget(PreserveRow.ToSharedRef(), TEXT("SPropertyEditorBool"));
	if (!TestTrue(TEXT("Preserve row contains its native boolean property editor"), BoolEditor.IsValid())) return false;
	const TSharedPtr<SWidget> FoundCheckbox = FindOptionsWidget(BoolEditor.ToSharedRef(), TEXT("SCheckBox"));
	if (!TestTrue(TEXT("Preserve row contains its actual checkbox"), FoundCheckbox.IsValid())) return false;
	const TSharedPtr<SCheckBox> Checkbox = StaticCastSharedPtr<SCheckBox>(FoundCheckbox);
	TestFalse(TEXT("Script state preservation defaults off"), Checkbox->IsChecked());
	TestNull(TEXT("Fresh-call node has no reset state pin"), Node->FindPin(TEXT("ResetSession")));
	Checkbox->ToggleCheckedState();
	TestTrue(TEXT("Actual Details checkbox updates the serialized option"), Node->ExecutionOptions.bUseSession);
	UEdGraphPin* ResetPin = Node->FindPin(TEXT("ResetSession"));
	if (!TestNotNull(TEXT("Enabling preservation reconstructs the reset pin"), ResetPin)) return false;
	TestEqual(TEXT("Reset pin retains its serialized internal name"), ResetPin->PinName, FName(TEXT("ResetSession")));
	TestEqual(TEXT("Reset pin shows the clearer label"), ResetPin->PinFriendlyName.ToString(), FString(TEXT("Reset Script State")));
	TestEqual(TEXT("Reset pin explains its node and instance scope"), ResetPin->PinToolTip, FString(TEXT("Clear this node's saved script state for this Blueprint instance before this call.")));
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
