// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Input/HittestGrid.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "SGraphNode_UEmka.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Types/PaintArgs.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Text/SMultiLineEditableText.h"

namespace
{
UBlueprint* MakeCoverageBlueprint(UPackage* Package = GetTransientPackage())
{
	return FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("UEmkaEditorCoverage")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
}

UK2Node_UEmka* AddCoverageNode(UBlueprint* Blueprint, const FString& Script)
{
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph, NAME_None, RF_Transactional);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script = Script;
	Node->AllocateDefaultPins();
	return Node;
}

TSharedPtr<SWidget> FindCoverageWidget(const TSharedRef<SWidget>& Root, const FName Type)
{
	if (Root->GetType() == Type) return Root;
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<SWidget> Match = FindCoverageWidget(Children->GetChildAt(Index), Type)) return Match;
	}
	return nullptr;
}

class FScopedCoverageVirtualWindow
{
public:
	explicit FScopedCoverageVirtualWindow(const TSharedRef<SWidget>& Content)
		: PreviousFocus(FSlateApplication::Get().GetKeyboardFocusedWidget())
		, Window(SNew(SVirtualWindow).Size(FVector2D(800, 400)))
	{
		Window->SetContent(Content);
		Window->SetIsFocusable(true);
		FSlateApplication::Get().RegisterVirtualWindow(Window);
	}

	~FScopedCoverageVirtualWindow()
	{
		if (PreviousFocus.IsValid()) FSlateApplication::Get().SetKeyboardFocus(PreviousFocus);
		else FSlateApplication::Get().ClearKeyboardFocus();
		FSlateApplication::Get().UnregisterVirtualWindow(Window);
	}

	TArray<FLinearColor> Paint(const TSharedRef<SWidget>& Content) const
	{
		FSlateApplication& Application = FSlateApplication::Get();
		Application.Tick(ESlateTickType::TimeAndWidgets);
		Content->SlatePrepass(1.f);
		FSlateWindowElementList Elements(Window);
		FHittestGrid HitTestGrid;
		const FPaintArgs PaintArgs(&Window.Get(), HitTestGrid, FVector2D::ZeroVector, Application.GetCurrentTime(), Application.GetDeltaTime());
		Content->Paint(PaintArgs, FGeometry::MakeRoot(FVector2D(800, 400), FSlateLayoutTransform()),
			FSlateRect(0, 0, 800, 400), Elements, 0, FWidgetStyle(), true);
		TArray<FLinearColor> Colors;
		for (const FSlateShapedTextElement& Element : Elements.GetUncachedDrawElements().Get<static_cast<uint8>(EElementType::ET_ShapedText)>())
		{
			Colors.Add(Element.GetTint());
		}
		for (const FSlateTextElement& Element : Elements.GetUncachedDrawElements().Get<static_cast<uint8>(EElementType::ET_Text)>())
		{
			Colors.Add(Element.GetTint());
		}
		return Colors;
	}

private:
	TSharedPtr<SWidget> PreviousFocus;
	TSharedRef<SVirtualWindow> Window;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaExportSelectionTest, "UEmka.Editor.FirstExportAndQuotedDeclarations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaExportSelectionTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("// fn Comment*(Bad: ^int) {}\n")
		TEXT("/* fn Block*(Bad: map[int]int) {} */\n")
		TEXT("const Note = \"fn String*(Bad: ^int) {} \\\"still quoted\\\"\"\n")
		TEXT("const Quote = '\\''\n")
		TEXT("fn Helper(Value: int): int { return Value }\n")
		TEXT("fn First*(Left, Right: int, Scale: real = 2.0): real { return real(Helper(Left) + Right) * Scale }\n")
		TEXT("fn Second*(Text: str): str { return Text }");
	FString Error;
	int32 ErrorLine;
	if (!TestTrue(TEXT("Selection fixture compiles"), UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, ErrorLine)))
	{
		AddError(Error);
		return false;
	}
	const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Script);
	TestTrue(TEXT("First exported signature supported"), Signature.bValid && Signature.UnsupportedReason.IsEmpty());
	TestEqual(TEXT("Private helper and fake declarations skipped"), Signature.FunctionName, FString(TEXT("First")));
	if (TestEqual(TEXT("Grouped and default parameters retained"), Signature.Params.Num(), 3))
	{
		TestEqual(TEXT("First grouped name"), Signature.Params[0].Name, FString(TEXT("Left")));
		TestEqual(TEXT("Second grouped name"), Signature.Params[1].Name, FString(TEXT("Right")));
		TestEqual(TEXT("Default parameter type"), Signature.Params[2].Type, EUEmkaValueType::Real);
	}
	TestTrue(TEXT("Return type resolved"), Signature.ReturnType.IsSet() && Signature.ReturnType.GetValue() == EUEmkaValueType::Real);
	TestFalse(TEXT("Private functions alone have no signature"), UK2Node_UEmka::ParseScript(TEXT("fn Helper() {} // fn Fake*() {}" )).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaUnsupportedCompiledShapesTest, "UEmka.Editor.CompiledUnsupportedShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaUnsupportedCompiledShapesTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Scripts =
	{
		TEXT("type Item = struct { Value: ^int }\nfn Test*(Values: []Item) {}"),
		TEXT("type Item = struct { Value: ^int }\nfn Test*(Values: [2]Item) {}"),
		TEXT("type Item = struct { Values: [][]int }\nfn Test*(Value: Item) {}"),
		TEXT("type Inner = struct { Value: ^int }\ntype Outer = struct { Value: Inner }\nfn Test*(Value: Outer) {}"),
		TEXT("type Item = struct { Value: ^int }\nfn Test*(Value: Item): (Item, int) { return Value, 1 }"),
		TEXT("fn Test*(Value: map[real]int) {}"),
		TEXT("fn Test*(Value: map[int][]int) {}"),
		TEXT("fn Test*(Value: [][]int) {}"),
		TEXT("fn Test*(Value: any) {}"),
		TEXT("fn Test*(Value: fn(Argument: int): int) {}"),
		TEXT("fn Test*(Value: ^int) {}"),
		TEXT("fn Test*(Value: weak ^int) {}"),
		TEXT("fn Test*(Value: enum { First; Second }) {}"),
	};
	AddExpectedError(TEXT("has a type that cannot cross Blueprint pins"), EAutomationExpectedErrorFlags::Contains, Scripts.Num());
	for (int32 Index = 0; Index < Scripts.Num(); ++Index)
	{
		FString Error;
		int32 ErrorLine;
		if (!TestTrue(FString::Printf(TEXT("Unsupported fixture %d is valid Umka"), Index),
			UUEmkaFunctionLibrary::CompileCheckScript(Scripts[Index], Error, ErrorLine)))
		{
			AddError(Error);
			continue;
		}
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Scripts[Index]);
		TestTrue(TEXT("Export exists"), Signature.bValid);
		TestFalse(TEXT("Unsupported shape has a diagnostic"), Signature.UnsupportedReason.IsEmpty());
		TestTrue(TEXT("Unsupported signature exposes no data"), Signature.Params.IsEmpty()
			&& Signature.ReturnParams.IsEmpty() && !Signature.ReturnType.IsSet() && !Signature.bNeedsShim);
		UK2Node_UEmka* Node = AddCoverageNode(MakeCoverageBlueprint(), Scripts[Index]);
		TestEqual(TEXT("Unsupported node retains execution pins only"), Node->Pins.Num(), 2);
		FCompilerResultsLog Log;
		Node->ValidateNodeDuringCompilation(Log);
		TestEqual(TEXT("Blueprint validation reports unsupported signature"), Log.NumErrors, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaIncompleteSignatureTest, "UEmka.Editor.IncompleteEditFallbackFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaIncompleteSignatureTest::RunTest(const FString& Parameters)
{
	for (const FString& Script : {
		FString(TEXT("fn Test*(Value: Undeclared): int { return 1 }")),
		FString(TEXT("type Count = int16\nfn Test*(Value: Count): Count { return Value")),
		FString(TEXT("type Count = Undeclared\nfn Test*(Value: Count): Count { return Value }")),
		FString(TEXT("type Count = int\ntype Item = struct { Value: Count }\nfn Test*(Value: Item): int { return Value.Value"))})
	{
		FString Error;
		int32 ErrorLine;
		TestFalse(TEXT("Fallback fixture cannot compile"), UUEmkaFunctionLibrary::CompileCheckScript(Script, Error, ErrorLine));
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Script);
		TestTrue(TEXT("Incomplete edit still identifies export"), Signature.bValid);
		TestFalse(TEXT("Fallback does not guess unknown or unresolved aliases"), Signature.UnsupportedReason.IsEmpty());
		TestTrue(TEXT("Unsafe fallback clears all data pins"), Signature.Params.IsEmpty()
			&& Signature.ReturnParams.IsEmpty() && !Signature.ReturnType.IsSet());
	}
	const FUEmkaSignature Primitive = UK2Node_UEmka::ParseScript(TEXT("fn Preview*(Left, Right: int): real { return"));
	TestTrue(TEXT("Known primitive signature survives incomplete body"), Primitive.bValid && Primitive.UnsupportedReason.IsEmpty());
	TestEqual(TEXT("Grouped primitive fallback keeps both pins"), Primitive.Params.Num(), 2);
	TestFalse(TEXT("Unclosed parameter list rejected"), UK2Node_UEmka::ParseScript(TEXT("fn Preview*(Value: int")).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaDiagnosticLifecycleTest, "UEmka.Editor.DiagnosticLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaDiagnosticLifecycleTest::RunTest(const FString& Parameters)
{
	UK2Node_UEmka* Node = AddCoverageNode(MakeCoverageBlueprint(), TEXT("fn Test*(Value: int): int { return Value }"));
	const FString Invalid = TEXT("fn Test*(Value: int): int {\n return Missing\n}");
	Node->OnScriptChanged(Invalid);
	TestEqual(TEXT("Live diagnostic records source line"), Node->LastErrorLine, 2);
	TestTrue(TEXT("Live diagnostic names undefined symbol"), Node->LastErrorMessage.Contains(TEXT("Missing")));
	AddExpectedError(TEXT("Umka script error - Line 2: Unknown identifier Missing"), EAutomationExpectedErrorFlags::Contains, 1);
	FCompilerResultsLog InvalidLog;
	Node->ValidateNodeDuringCompilation(InvalidLog);
	TestEqual(TEXT("Blueprint validation reports compiler error"), InvalidLog.NumErrors, 1);
	TestTrue(TEXT("Blueprint message includes Umka diagnostic"), InvalidLog.Messages.ContainsByPredicate([](const TSharedRef<FTokenizedMessage>& Message)
	{
		return Message->ToText().ToString().Contains(TEXT("Missing"));
	}));
	Node->OnScriptChanged(TEXT("fn Test*(Value: int): int { return Value + 1 }"));
	TestEqual(TEXT("Valid edit clears diagnostic line"), Node->LastErrorLine, -1);
	TestTrue(TEXT("Valid edit clears diagnostic message"), Node->LastErrorMessage.IsEmpty());
	Node->Script = Invalid;
	Node->PostLoad();
	TestEqual(TEXT("PostLoad refreshes stored invalid script"), Node->LastErrorLine, 2);
	Node->Script = TEXT("fn Restored*(Value: int): int { return Value }");
	Node->PostEditUndo();
	TestTrue(TEXT("PostEditUndo clears stale diagnostics"), Node->LastErrorLine == -1 && Node->LastErrorMessage.IsEmpty());
	const TArray<UEdGraphPin*> RestoredPins = Node->Pins;
	Node->OnScriptChanged(Node->Script);
	TestTrue(TEXT("PostEditUndo refreshed signature cache without redundant reconstruction"), Node->Pins == RestoredPins);
	FCompilerResultsLog ValidLog;
	Node->ValidateNodeDuringCompilation(ValidLog);
	TestEqual(TEXT("Restored valid script validates"), ValidLog.NumErrors, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaSignatureReconstructionTest, "UEmka.Editor.SignatureReconstructionLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaSignatureReconstructionTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = MakeCoverageBlueprint();
	UK2Node_UEmka* Node = AddCoverageNode(Blueprint, TEXT("fn Test*(Keep, Removed: int): int { return Keep + Removed }"));
	UEdGraphNode* Source = NewObject<UEdGraphNode>(Node->GetGraph());
	Node->GetGraph()->AddNode(Source);
	UEdGraphPin* KeepSource = Source->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Int64, TEXT("Keep"));
	UEdGraphPin* RemovedSource = Source->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Int64, TEXT("Removed"));
	KeepSource->MakeLinkTo(Node->FindPinChecked(TEXT("Keep")));
	RemovedSource->MakeLinkTo(Node->FindPinChecked(TEXT("Removed")));
	Blueprint->Status = BS_UpToDate;
	Node->OnScriptChanged(TEXT("fn Changed*(Keep: int, Added: str): str { return Added }"));
	TestEqual(TEXT("Signature edit marks Blueprint dirty"), Blueprint->Status, BS_Dirty);
	TestTrue(TEXT("Compatible named input keeps connection"), Node->FindPinChecked(TEXT("Keep"))->LinkedTo.Contains(KeepSource));
	TestTrue(TEXT("Reverse compatible link points to reconstructed pin"), KeepSource->LinkedTo.Contains(Node->FindPinChecked(TEXT("Keep"))));
	TestNull(TEXT("Removed input has no phantom pin"), Node->FindPin(TEXT("Removed")));
	TestTrue(TEXT("Removed input connection is broken"), RemovedSource->LinkedTo.IsEmpty());
	TestFalse(TEXT("Reconstruction removes every orphan"), Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin) { return Pin->bOrphanedPin; }));
	TestEqual(TEXT("New input uses string type"), Node->FindPinChecked(TEXT("Added"))->PinType.PinCategory, UEdGraphSchema_K2::PC_String);
	TestEqual(TEXT("Changed return uses string type"), Node->FindPinChecked(TEXT("ReturnValue"))->PinType.PinCategory, UEdGraphSchema_K2::PC_String);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaScriptTransactionTest, "UEmka.Editor.ScriptTransactionUndoRedo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaScriptTransactionTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Editor transaction system exists"), GEditor)) return false;
	const FString Before = TEXT("fn Before*(Value: int): int { return Value }");
	const FString After = TEXT("fn After*(Text: str): str { return Text }");
	UK2Node_UEmka* Node = AddCoverageNode(MakeCoverageBlueprint(), Before);
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("UEmka automation script edit")));
		Node->Modify();
		Node->OnScriptChanged(After);
	}
	TestTrue(TEXT("Actual editor transaction undoes"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores serialized script"), Node->Script, Before);
	TestNotNull(TEXT("Undo reconstructs original input"), Node->FindPin(TEXT("Value")));
	TestNull(TEXT("Undo removes edited input"), Node->FindPin(TEXT("Text")));
	TestTrue(TEXT("Undo refreshes diagnostics"), Node->LastErrorLine == -1 && Node->LastErrorMessage.IsEmpty());
	TestTrue(TEXT("Actual editor transaction redoes"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores edited script"), Node->Script, After);
	TestNotNull(TEXT("Redo reconstructs edited input"), Node->FindPin(TEXT("Text")));
	TestNull(TEXT("Redo removes original input"), Node->FindPin(TEXT("Value")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaEmbeddedEditorTest, "UEmka.Editor.EmbeddedSlateEditorInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaEmbeddedEditorTest::RunTest(const FString& Parameters)
{
	UK2Node_UEmka* Node = AddCoverageNode(MakeCoverageBlueprint(), TEXT("fn Test*() {}"));
	const TSharedRef<SGraphNode_UEmka> Widget = SNew(SGraphNode_UEmka, Node);
	const TSharedPtr<SWidget> FoundBox = FindCoverageWidget(Widget, TEXT("SMultiLineEditableTextBox"));
	const TSharedPtr<SWidget> FoundText = FindCoverageWidget(Widget, TEXT("SMultiLineEditableText"));
	if (!TestTrue(TEXT("Actual node contains embedded editable widgets"), FoundBox.IsValid() && FoundText.IsValid())) return false;
	const TSharedPtr<SMultiLineEditableTextBox> Editor = StaticCastSharedPtr<SMultiLineEditableTextBox>(FoundBox);
	const TSharedPtr<SMultiLineEditableText> Text = StaticCastSharedPtr<SMultiLineEditableText>(FoundText);
	Editor->GoTo(FTextLocation(0, 0));
	const FModifierKeysState NoModifiers;
	TestTrue(TEXT("Tab handled by actual node delegate"), static_cast<SWidget&>(*Text).OnKeyDown(FGeometry(), FKeyEvent(EKeys::Tab, NoModifiers, 0, false, 0, 0)).IsEventHandled());
	TestEqual(TEXT("Tab inserts four spaces"), Editor->GetPlainText().ToString(), FString(TEXT("    fn Test*() {}")));
	const FModifierKeysState Shift(true, false, false, false, false, false, false, false, false);
	TestTrue(TEXT("Shift Tab handled by actual node delegate"), static_cast<SWidget&>(*Text).OnKeyDown(FGeometry(), FKeyEvent(EKeys::Tab, Shift, 0, false, 0, 0)).IsEventHandled());
	TestEqual(TEXT("Shift Tab removes indentation"), Editor->GetPlainText().ToString(), FString(TEXT("fn Test*() {}")));
	TestEqual(TEXT("Uncommitted text does not mutate stored script"), Node->Script, FString(TEXT("fn Test*() {}")));
	Widget->UpdateGraphNode();
	const TSharedPtr<SWidget> RebuiltBox = FindCoverageWidget(Widget, TEXT("SMultiLineEditableTextBox"));
	const TSharedPtr<SWidget> RebuiltText = FindCoverageWidget(Widget, TEXT("SMultiLineEditableText"));
	if (!TestTrue(TEXT("Node rebuild retains embedded editor"), RebuiltBox.IsValid() && RebuiltText.IsValid())) return false;
	const TSharedPtr<SMultiLineEditableTextBox> RebuiltEditor = StaticCastSharedPtr<SMultiLineEditableTextBox>(RebuiltBox);
	const TSharedPtr<SMultiLineEditableText> RebuiltInput = StaticCastSharedPtr<SMultiLineEditableText>(RebuiltText);
	RebuiltEditor->GoTo(FTextLocation(0, 0));
	TestTrue(TEXT("Typing reaches actual editable widget"), static_cast<SWidget&>(*RebuiltInput).OnKeyChar(FGeometry(), FCharacterEvent(TEXT('x'), NoModifiers, 0, false)).IsEventHandled());
	TestEqual(TEXT("Typed character appears in local editor"), RebuiltEditor->GetPlainText().ToString(), FString(TEXT("xfn Test*() {}")));
	const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
	static_cast<SWidget&>(*RebuiltInput).OnKeyDown(FGeometry(), FKeyEvent(EKeys::Z, Control, 0, false, 0, 0));
	TestEqual(TEXT("Control Z undoes local text through actual widget"), RebuiltEditor->GetPlainText().ToString(), FString(TEXT("fn Test*() {}")));
	static_cast<SWidget&>(*RebuiltInput).OnKeyDown(FGeometry(), FKeyEvent(EKeys::Y, Control, 0, false, 0, 0));
	TestEqual(TEXT("Control Y redoes local text through actual widget"), RebuiltEditor->GetPlainText().ToString(), FString(TEXT("xfn Test*() {}")));
	TestEqual(TEXT("Typing and local undo redo leave node uncommitted"), Node->Script, FString(TEXT("fn Test*() {}")));
	const FString Committed = TEXT("fn Committed*(Text: str): str { return Text }");
	RebuiltEditor->SelectAllText();
	RebuiltEditor->InsertTextAtCursor(Committed);
	TestEqual(TEXT("Replacement edits actual local content"), RebuiltEditor->GetPlainText().ToString(), Committed);
	static_cast<SWidget&>(*RebuiltInput).OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("Focus loss invokes actual script commit delegate"), Node->Script, Committed);
	UEdGraphPin* CommittedInput = Node->FindPin(TEXT("Text"));
	if (TestNotNull(TEXT("Slate commit reconstructs changed signature"), CommittedInput))
	{
		TestEqual(TEXT("Slate commit generates typed input pin"), CommittedInput->PinType.PinCategory, UEdGraphSchema_K2::PC_String);
	}
	TestTrue(TEXT("Slate commit refreshes valid diagnostics"), Node->LastErrorLine == -1 && Node->LastErrorMessage.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaDeferredEditorDiagnosticTest, "UEmka.Editor.DeferredSlateUndoRedoDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaDeferredEditorDiagnosticTest::RunTest(const FString& Parameters)
{
	const FString Original = TEXT("fn Test*() {}");
	UK2Node_UEmka* Node = AddCoverageNode(MakeCoverageBlueprint(), Original);
	const TSharedRef<SGraphNode_UEmka> Widget = SNew(SGraphNode_UEmka, Node);
	FScopedCoverageVirtualWindow Window(Widget);
	const TSharedPtr<SWidget> FoundBox = FindCoverageWidget(Widget, TEXT("SMultiLineEditableTextBox"));
	const TSharedPtr<SWidget> FoundText = FindCoverageWidget(Widget, TEXT("SMultiLineEditableText"));
	if (!TestTrue(TEXT("Virtual graph window contains editor"), FoundBox.IsValid() && FoundText.IsValid())) return false;
	const TSharedPtr<SMultiLineEditableTextBox> Editor = StaticCastSharedPtr<SMultiLineEditableTextBox>(FoundBox);
	if (!TestTrue(TEXT("Real Slate focus reaches virtual window editor"), FSlateApplication::Get().SetKeyboardFocus(FoundText))) return false;
	TestTrue(TEXT("Editable widget has real keyboard focus"), FoundText->HasKeyboardFocus());
	const FLinearColor ErrorColor(1.f, 0.1f, 0.1f);
	const TArray<FLinearColor> OriginalColors = Window.Paint(Widget);
	TestFalse(TEXT("Virtual window paints actual graph text"), OriginalColors.IsEmpty());
	TestFalse(TEXT("Valid initial editor has no error tint"), OriginalColors.Contains(ErrorColor));
	Editor->GoTo(FTextLocation(0, 0));
	const FModifierKeysState NoModifiers;
	FoundText->OnKeyChar(FGeometry(), FCharacterEvent(TEXT('x'), NoModifiers, 0, false));
	TestEqual(TEXT("Invalid edit remains local"), Editor->GetPlainText().ToString(), FString(TEXT("xfn Test*() {}")));
	TestTrue(TEXT("Live invalid text paints error tint"), Window.Paint(Widget).Contains(ErrorColor));
	TestEqual(TEXT("Live diagnostics leave stored script unchanged"), Node->Script, Original);
	const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
	FoundText->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Z, Control, 0, false, 0, 0));
	TestTrue(TEXT("Undo registers deferred diagnostic timer"), Widget->HasActiveTimers());
	TestEqual(TEXT("Undo restores valid local source"), Editor->GetPlainText().ToString(), Original);
	TestFalse(TEXT("Deferred undo check clears error tint"), Window.Paint(Widget).Contains(ErrorColor));
	TestFalse(TEXT("Deferred undo timer retires after one paint"), Widget->HasActiveTimers());
	FoundText->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Y, Control, 0, false, 0, 0));
	TestTrue(TEXT("Redo registers deferred diagnostic timer"), Widget->HasActiveTimers());
	TestEqual(TEXT("Redo restores invalid local source"), Editor->GetPlainText().ToString(), FString(TEXT("xfn Test*() {}")));
	TestTrue(TEXT("Deferred redo checks live source and restores error tint"), Window.Paint(Widget).Contains(ErrorColor));
	TestFalse(TEXT("Deferred redo timer retires after one paint"), Widget->HasActiveTimers());
	TestEqual(TEXT("Deferred diagnostics leave stored script unchanged"), Node->Script, Original);
	FoundText->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Z, Control, 0, false, 0, 0));
	Window.Paint(Widget);
	TestEqual(TEXT("Cleanup restores original text before focus loss"), Editor->GetPlainText().ToString(), Original);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaGraphPersistenceTest, "UEmka.Editor.GraphPackageRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaGraphPersistenceTest::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Temp/UEmkaCoverage_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = MakeCoverageBlueprint(Package);
	Blueprint->SetFlags(RF_Public | RF_Standalone);
	const FString Script = TEXT("fn Saved*(Values: []int): (int, str) { return len(Values), \"saved\" }");
	UK2Node_UEmka* Node = AddCoverageNode(Blueprint, Script);
	const FGuid NodeGuid = Node->NodeGuid;
	const FName AssetName = Blueprint->GetFName();
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/UEmka");
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Filename = Directory / (FPaths::GetCleanFilename(PackageName) + TEXT(".uasset"));
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("Blueprint graph package saves"), UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))) return false;
	Package->Rename(*(PackageName + TEXT("_Original")), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
	UPackage* LoadedPackage = LoadPackage(nullptr, *Filename, LOAD_None);
	UBlueprint* LoadedBlueprint = LoadedPackage ? FindObject<UBlueprint>(LoadedPackage, *AssetName.ToString()) : nullptr;
	if (!TestNotNull(TEXT("Blueprint reloads from saved package"), LoadedBlueprint))
	{
		IFileManager::Get().Delete(*Filename);
		return false;
	}
	UEdGraph* LoadedGraph = FBlueprintEditorUtils::FindEventGraph(LoadedBlueprint);
	if (!TestNotNull(TEXT("Event graph reloads from saved package"), LoadedGraph))
	{
		LoadedBlueprint->ClearFlags(RF_Standalone);
		IFileManager::Get().Delete(*Filename);
		return false;
	}
	TArray<UK2Node_UEmka*> LoadedNodes;
	LoadedGraph->GetNodesOfClass(LoadedNodes);
	if (TestEqual(TEXT("Script node survives graph serialization"), LoadedNodes.Num(), 1))
	{
		TestEqual(TEXT("Inline script round trips"), LoadedNodes[0]->Script, Script);
		TestEqual(TEXT("Node identity round trips"), LoadedNodes[0]->NodeGuid, NodeGuid);
		UEdGraphPin* ValuesPin = LoadedNodes[0]->FindPin(TEXT("Values"));
		if (TestNotNull(TEXT("Array input pin reloads"), ValuesPin))
		{
			TestEqual(TEXT("Array pin round trips"), ValuesPin->PinType.ContainerType, EPinContainerType::Array);
		}
		TestNotNull(TEXT("Tuple return pins round trip"), LoadedNodes[0]->FindPin(TEXT("ReturnValue2")));
		FCompilerResultsLog Log;
		LoadedNodes[0]->ValidateNodeDuringCompilation(Log);
		TestEqual(TEXT("PostLoad rebuilt signature cache"), Log.NumErrors, 0);
		TestTrue(TEXT("PostLoad refreshed diagnostics"), LoadedNodes[0]->LastErrorLine == -1 && LoadedNodes[0]->LastErrorMessage.IsEmpty());
	}
	LoadedBlueprint->ClearFlags(RF_Standalone);
	IFileManager::Get().Delete(*Filename);
	return true;
}

#endif
