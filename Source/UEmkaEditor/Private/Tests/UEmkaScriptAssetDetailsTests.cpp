// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaScriptAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PropertyEditorModule.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/SMultiLineEditableText.h"

namespace
{
TSharedPtr<SWidget> FindScriptDetailsWidget(const TSharedRef<SWidget>& Root, const FName Name, const bool bMatchTag)
{
	if ((bMatchTag ? Root->GetTag() : Root->GetType()) == Name) return Root;
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<SWidget> Match = FindScriptDetailsWidget(Children->GetChildAt(Index), Name, bMatchTag)) return Match;
	}
	return nullptr;
}

TSharedPtr<SEditableText> FindScriptDetailsFilePath(const TSharedRef<SWidget>& Root, const FString& Filename)
{
	if (Root->GetType() == TEXT("SEditableText"))
	{
		const TSharedRef<SEditableText> Text = StaticCastSharedRef<SEditableText>(Root);
		if (Text->GetText().ToString().EndsWith(Filename)) return Text;
	}
	FChildren* Children = Root->GetChildren();
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<SEditableText> Match = FindScriptDetailsFilePath(Children->GetChildAt(Index), Filename)) return Match;
	}
	return nullptr;
}

class FScopedScriptDetailsWindow
{
public:
	explicit FScopedScriptDetailsWindow(const TSharedRef<SWidget>& Content)
		: PreviousFocus(FSlateApplication::Get().GetKeyboardFocusedWidget())
		, Window(SNew(SVirtualWindow).Size(FVector2D(800, 600)))
	{
		Window->SetContent(Content);
		Window->SetIsFocusable(true);
		FSlateApplication::Get().RegisterVirtualWindow(Window);
	}

	~FScopedScriptDetailsWindow()
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
		Content->Paint(PaintArgs, FGeometry::MakeRoot(FVector2D(800, 600), FSlateLayoutTransform()),
			FSlateRect(0, 0, 800, 600), Elements, 0, FWidgetStyle(), true);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaScriptAssetDetailsTest, "UEmka.Editor.Assets.ScriptDetails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaScriptAssetDetailsTest::RunTest(const FString& Parameters)
{
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/UEmkaScriptDetails_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>(Package, NAME_None, RF_Transactional);
	const FString Original = TEXT("fn Test*(Value: int): int { return Value + 1 }");
	Asset->Source = Original;
	Package->SetDirtyFlag(false);

	FDetailsViewArgs Args;
	Args.bUpdatesFromSelection = false;
	Args.bAllowSearch = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	const TSharedRef<IDetailsView> View = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	View->SetObject(Asset);
	FScopedScriptDetailsWindow Window(View);
	Window.Paint(View);
	const TSharedPtr<SWidget> FoundEditor = FindScriptDetailsWidget(View, TEXT("UEmka.AssetSourceEditor"), true);
	if (!TestTrue(TEXT("Asset Details constructs the highlighted Source editor"), FoundEditor.IsValid())) return false;
	TSharedPtr<SMultiLineEditableTextBox> Editor = StaticCastSharedPtr<SMultiLineEditableTextBox>(FoundEditor);
	TSharedPtr<SWidget> Input = FindScriptDetailsWidget(Editor.ToSharedRef(), TEXT("SMultiLineEditableText"), false);
	if (!TestTrue(TEXT("Source contains a real editable text widget"), Input.IsValid())) return false;
	const TArray<FLinearColor> Colors = Window.Paint(Editor.ToSharedRef());
	TestTrue(TEXT("Source paints existing keyword style"), Colors.Contains(FLinearColor(0.86f, 0.45f, 0.18f)));
	TestTrue(TEXT("Source paints existing type style"), Colors.Contains(FLinearColor(0.29f, 0.76f, 0.93f)));
	TestEqual(TEXT("Source initially displays the asset property"), Editor->GetPlainText().ToString(), Original);

	int32 AssetChanges = 0;
	int32 FinishedChanges = 0;
	const FDelegateHandle AssetChangedHandle = UUEmkaScriptAsset::OnScriptAssetChanged.AddLambda([&](UUEmkaScriptAsset* ChangedAsset)
	{
		if (ChangedAsset == Asset) ++AssetChanges;
	});
	const FDelegateHandle FinishedHandle = View->OnFinishedChangingProperties().AddLambda([&](const FPropertyChangedEvent& Event)
	{
		if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UUEmkaScriptAsset, Source)) ++FinishedChanges;
	});
	ON_SCOPE_EXIT
	{
		UUEmkaScriptAsset::OnScriptAssetChanged.Remove(AssetChangedHandle);
		View->OnFinishedChangingProperties().Remove(FinishedHandle);
	};

	Editor->GoTo(FTextLocation(0, 0));
	TestTrue(TEXT("Tab inserts code indentation"), Input->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Tab, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled());
	const FString Edited = TEXT("    ") + Original;
	TestEqual(TEXT("Tab inserts four spaces"), Editor->GetPlainText().ToString(), Edited);
	TestEqual(TEXT("Editing remains local until commit"), Asset->Source, Original);
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("Focus loss commits through the property handle"), Asset->Source, Edited);
	TestEqual(TEXT("Commit invokes the asset property callback"), AssetChanges, 1);
	TestEqual(TEXT("Commit invokes the Details finished callback"), FinishedChanges, 1);
	TestTrue(TEXT("Commit marks the asset package dirty"), Package->IsDirty());
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("An unchanged commit does not notify again"), AssetChanges, 1);

	TestTrue(TEXT("Property edit has an editor undo transaction"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores source"), Asset->Source, Original);
	Window.Paint(View);
	TestEqual(TEXT("Undo refreshes displayed source"), Editor->GetPlainText().ToString(), Original);
	TestTrue(TEXT("Property edit redoes"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores edited source"), Asset->Source, Edited);
	Window.Paint(View);
	TestEqual(TEXT("Redo refreshes displayed source"), Editor->GetPlainText().ToString(), Edited);

	Asset->Source = TEXT("fn External*() {}");
	Window.Paint(View);
	TestEqual(TEXT("External property changes refresh displayed source"), Editor->GetPlainText().ToString(), Asset->Source);

	UUEmkaScriptAsset* OtherAsset = NewObject<UUEmkaScriptAsset>(Package, NAME_None, RF_Transactional);
	OtherAsset->Source = Original;
	View->SetObjects(TArray<UObject*>{Asset, OtherAsset});
	Window.Paint(View);
	const TSharedPtr<SWidget> MultiEditor = FindScriptDetailsWidget(View, TEXT("UEmka.AssetSourceEditor"), true);
	if (!TestTrue(TEXT("Multiple assets retain the Source editor"), MultiEditor.IsValid())) return false;
	Editor = StaticCastSharedPtr<SMultiLineEditableTextBox>(MultiEditor);
	Input = FindScriptDetailsWidget(Editor.ToSharedRef(), TEXT("SMultiLineEditableText"), false);
	if (!TestTrue(TEXT("Multiple-value Source remains editable"), Input.IsValid())) return false;
	const FString External = Asset->Source;
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("Unedited multiple-value commit preserves first source"), Asset->Source, External);
	TestEqual(TEXT("Unedited multiple-value commit preserves second source"), OtherAsset->Source, Original);
	Editor->SelectAllText();
	Editor->InsertTextAtCursor(TEXT("fn Shared*() {}"));
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("An explicit edit applies to both sources"), Asset->Source, FString(TEXT("fn Shared*() {}")));
	TestEqual(TEXT("Multiple selection shares the edited source"), OtherAsset->Source, Asset->Source);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaFileBackedScriptDetailsTest, "UEmka.Editor.Assets.FileBackedScriptDetails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaFileBackedScriptDetailsTest::RunTest(const FString& Parameters)
{
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
	const FString Original = TEXT("fn Imported*(Value: int): int { return Value }");
	Asset->Source = Original;
	if (!TestNotNull(TEXT("A new asset owns import metadata"), Asset->AssetImportData.Get())) return false;
	TestFalse(TEXT("A new menu asset is not file backed"), Asset->IsFileBacked());

	FDetailsViewArgs Args;
	Args.bUpdatesFromSelection = false;
	Args.bAllowSearch = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	const TSharedRef<IDetailsView> View = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	View->SetObject(Asset);
	FScopedScriptDetailsWindow Window(View);
	Window.Paint(View);
	TSharedPtr<SWidget> FoundEditor = FindScriptDetailsWidget(View, TEXT("UEmka.AssetSourceEditor"), true);
	if (!TestTrue(TEXT("A normal asset retains the Source editor"), FoundEditor.IsValid())) return false;
	TSharedPtr<SMultiLineEditableTextBox> Editor = StaticCastSharedPtr<SMultiLineEditableTextBox>(FoundEditor);
	TSharedPtr<SWidget> FoundInput = FindScriptDetailsWidget(Editor.ToSharedRef(), TEXT("SMultiLineEditableText"), false);
	if (!TestTrue(TEXT("A normal asset has editable input"), FoundInput.IsValid())) return false;
	TestFalse(TEXT("A normal asset's source remains editable"), StaticCastSharedPtr<SMultiLineEditableText>(FoundInput)->IsTextReadOnly());

	Asset->AssetImportData->UpdateFilenameOnly(TEXT("UEmkaDetailsSource.um"));
	TestTrue(TEXT("A source filename binds the asset to an external file"), Asset->IsFileBacked());
	View->SetObject(Asset, true);
	Window.Paint(View);
	const TSharedPtr<SEditableText> SourceFile = FindScriptDetailsFilePath(View, TEXT("UEmkaDetailsSource.um"));
	TestTrue(TEXT("Native import metadata displays the linked source filename"), SourceFile.IsValid());
	FoundEditor = FindScriptDetailsWidget(View, TEXT("UEmka.AssetSourceEditor"), true);
	if (!TestTrue(TEXT("A file-backed asset retains highlighted Source"), FoundEditor.IsValid())) return false;
	Editor = StaticCastSharedPtr<SMultiLineEditableTextBox>(FoundEditor);
	FoundInput = FindScriptDetailsWidget(Editor.ToSharedRef(), TEXT("SMultiLineEditableText"), false);
	if (!TestTrue(TEXT("A file-backed asset retains text input for selection"), FoundInput.IsValid())) return false;
	const TSharedPtr<SMultiLineEditableText> Input = StaticCastSharedPtr<SMultiLineEditableText>(FoundInput);
	TestTrue(TEXT("File-backed Source is read only"), Input->IsTextReadOnly());
	FWidgetPath SourcePath;
	if (!TestTrue(TEXT("Read-only Source has a live path through the Details view"), FSlateApplication::Get().GeneratePathToWidgetUnchecked(FoundInput.ToSharedRef(), SourcePath))) return false;
	for (int32 Index = 0; Index < SourcePath.Widgets.Num(); ++Index)
	{
		const FArrangedWidget& Widget = SourcePath.Widgets[Index];
		TestTrue(*FString::Printf(TEXT("Source ancestor %s is enabled for selection and copy"), *Widget.Widget->GetType().ToString()), Widget.Widget->IsEnabled());
	}
	TestTrue(TEXT("Read-only Source keeps syntax colors through the full Details view"), Window.Paint(View).Contains(FLinearColor(0.86f, 0.45f, 0.18f)));
	TestTrue(TEXT("Read-only Source accepts real keyboard focus"), FSlateApplication::Get().SetKeyboardFocus(FoundInput));
	TestTrue(TEXT("Read-only Source has keyboard focus for copying"), FoundInput->HasKeyboardFocus());
	Editor->SelectAllText();
	TestEqual(TEXT("Read-only source can be selected for copying"), Editor->GetSelectedText().ToString(), Original);
	Editor->GoTo(FTextLocation(0, 0));
	FoundInput->OnKeyDown(FGeometry(), FKeyEvent(EKeys::Tab, FModifierKeysState(), 0, false, 0, 0));
	FoundInput->OnKeyChar(FGeometry(), FCharacterEvent(TEXT('x'), FModifierKeysState(), 0, false));
	TestEqual(TEXT("Typing and Tab cannot alter file-backed text"), Editor->GetPlainText().ToString(), Original);
	FoundInput->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("Read-only focus loss preserves the imported source"), Asset->Source, Original);

	Editor->SetIsReadOnly(false);
	Editor->SelectAllText();
	Editor->InsertTextAtCursor(TEXT("fn Rejected*() {}"));
	FoundInput->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("The property guard rejects commits even from an editable widget"), Asset->Source, Original);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaModulePathDetailsTest, "UEmka.Editor.Assets.ModulePathDetails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaModulePathDetailsTest::RunTest(const FString& Parameters)
{
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
	Asset->ModulePath = TEXT("Original.um");
	Asset->AssetImportData->UpdateFilenameOnly(TEXT("UEmkaModulePathSource.um"));
	FDetailsViewArgs Args;
	Args.bUpdatesFromSelection = false;
	Args.bAllowSearch = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	const TSharedRef<IDetailsView> View = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
	View->SetObject(Asset);
	FScopedScriptDetailsWindow Window(View);
	Window.Paint(View);
	TSharedPtr<SWidget> FoundEditor = FindScriptDetailsWidget(View, TEXT("UEmka.AssetModulePathEditor"), true);
	if (!TestTrue(TEXT("Details constructs the validated module path editor"), FoundEditor.IsValid())) return false;
	TSharedPtr<SEditableTextBox> Editor = StaticCastSharedPtr<SEditableTextBox>(FoundEditor);
	TSharedPtr<SWidget> Input = FindScriptDetailsWidget(Editor.ToSharedRef(), TEXT("SEditableText"), false);
	if (!TestTrue(TEXT("Module path has real editable input"), Input.IsValid())) return false;
	TestFalse(TEXT("A file-backed asset's virtual module path remains editable"), Editor->IsReadOnly());
	const auto TypePath = [&Editor, &Input, &Window, &View](const FString& Path)
	{
		Editor->SelectAllText();
		if (Path.IsEmpty()) Input->OnKeyDown(FGeometry(), FKeyEvent(EKeys::BackSpace, FModifierKeysState(), 0, false, 0, 0));
		for (const TCHAR Character : Path)
		{
			Input->OnKeyChar(FGeometry(), FCharacterEvent(Character, FModifierKeysState(), 0, false));
		}
		Window.Paint(View);
	};
	int32 FinishedChanges = 0;
	const FDelegateHandle FinishedHandle = View->OnFinishedChangingProperties().AddLambda([&](const FPropertyChangedEvent& Event)
	{
		if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UUEmkaScriptAsset, ModulePath)) ++FinishedChanges;
	});
	ON_SCOPE_EXIT { View->OnFinishedChangingProperties().Remove(FinishedHandle); };
	for (const FString& Invalid : {FString(TEXT("garbage")), FString(TEXT("../Escape.um")), FString(TEXT("/Absolute.um")), FString(TEXT("C:/Absolute.um")),
		FString(TEXT("Wrong.txt")), FString(TEXT("Empty//Segment.um")), FString(TEXT("Dot/./Segment.um")), FString(TEXT("Bad?.um")), FString::ChrN(253, TEXT('a')) + TEXT(".um")})
	{
		TypePath(Invalid);
		TestTrue(*FString::Printf(TEXT("Invalid path '%s' shows a live error"), *Invalid), Editor->HasError());
		Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
		TestEqual(TEXT("Invalid commit preserves the stored module path"), Asset->ModulePath, FString(TEXT("Original.um")));
	}
	TestEqual(TEXT("Invalid paths do not fire property notifications"), FinishedChanges, 0);
	TypePath(TEXT("Modules/Nested.um"));
	TestFalse(TEXT("A valid relative .um path clears the live error"), Editor->HasError());
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("A valid nested path commits"), Asset->ModulePath, FString(TEXT("Modules/Nested.um")));
	TestEqual(TEXT("A valid commit uses property notifications"), FinishedChanges, 1);
	TestTrue(TEXT("A valid path edit undoes"), GEditor->UndoTransaction());
	Window.Paint(View);
	TestEqual(TEXT("Undo restores the original path"), Asset->ModulePath, FString(TEXT("Original.um")));
	TestEqual(TEXT("Undo refreshes the path editor"), Editor->GetText().ToString(), Asset->ModulePath);
	TestTrue(TEXT("A valid path edit redoes"), GEditor->RedoTransaction());
	Window.Paint(View);
	TestEqual(TEXT("Redo restores the nested path"), Asset->ModulePath, FString(TEXT("Modules/Nested.um")));
	TypePath(TEXT(""));
	TestFalse(TEXT("Blank path is valid for asset-name fallback"), Editor->HasError());
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestTrue(TEXT("Blank fallback path commits"), Asset->ModulePath.IsEmpty());

	UUEmkaScriptAsset* OtherAsset = NewObject<UUEmkaScriptAsset>(GetTransientPackage(), NAME_None, RF_Transactional);
	OtherAsset->ModulePath = TEXT("Other.um");
	View->SetObjects(TArray<UObject*>{Asset, OtherAsset});
	Window.Paint(View);
	FoundEditor = FindScriptDetailsWidget(View, TEXT("UEmka.AssetModulePathEditor"), true);
	if (!TestTrue(TEXT("Multiple assets retain the validated path editor"), FoundEditor.IsValid())) return false;
	Editor = StaticCastSharedPtr<SEditableTextBox>(FoundEditor);
	Input = FindScriptDetailsWidget(Editor.ToSharedRef(), TEXT("SEditableText"), false);
	if (!TestTrue(TEXT("Multiple module paths remain editable"), Input.IsValid())) return false;
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestTrue(TEXT("An unedited multiple-value commit keeps the fallback path"), Asset->ModulePath.IsEmpty());
	TestEqual(TEXT("An unedited multiple-value commit keeps the other path"), OtherAsset->ModulePath, FString(TEXT("Other.um")));
	TypePath(TEXT("Shared/Module.um"));
	Input->OnFocusLost(FFocusEvent(EFocusCause::Navigation, 0));
	TestEqual(TEXT("A valid multiple-value edit commits to the first asset"), Asset->ModulePath, FString(TEXT("Shared/Module.um")));
	TestEqual(TEXT("A valid multiple-value edit commits to the second asset"), OtherAsset->ModulePath, Asset->ModulePath);
	return true;
}

#endif
