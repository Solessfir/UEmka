// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaFunctionPropertyOptionsTest, "UEmka.Editor.Options.FunctionProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaFunctionPropertyOptionsTest::RunTest(const FString& Parameters)
{
	const FProperty* Property = FindFProperty<FProperty>(UK2Node_UEmka::StaticClass(), GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, SelectedFunction));
	if (!TestNotNull(TEXT("Selected function property exists"), Property)) return false;
	TestEqual(TEXT("Details uses the native options dropdown"), Property->GetMetaData(TEXT("GetOptions")), FString(TEXT("GetSelectedFunctionOptions")));
	TestNotNull(TEXT("Options getter is reflected"), UK2Node_UEmka::StaticClass()->FindFunctionByName(TEXT("GetSelectedFunctionOptions")));
	TestTrue(TEXT("Case-distinct Main export remains selectable"), UK2Node_UEmka::GetExportedFunctions(TEXT("fn Main*() {}")).Contains(TEXT("Main")));

	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>();
	Node->Script = TEXT("// fn Comment*() {}\nfn Private() {}\nfn main*() {}\nfn First*() {}\nfn Second*() {}");
	const TArray<FString> InlineOptions = Node->GetSelectedFunctionOptions();
	if (TestEqual(TEXT("Inline options contain only exports"), InlineOptions.Num(), 2))
	{
		TestEqual(TEXT("First inline export"), InlineOptions[0], FString(TEXT("First")));
		TestEqual(TEXT("Second inline export"), InlineOptions[1], FString(TEXT("Second")));
	}

	Node->ScriptAsset = NewObject<UUEmkaScriptAsset>();
	Node->ScriptAsset->Source = TEXT("fn AssetFunction*() {}");
	Node->SelectedFunction = TEXT("OldSerializedSelection");
	const TArray<FString> AssetOptions = Node->GetSelectedFunctionOptions();
	if (TestEqual(TEXT("Asset overrides inline function options"), AssetOptions.Num(), 1))
	{
		TestEqual(TEXT("Asset export is selectable"), AssetOptions[0], FString(TEXT("AssetFunction")));
	}
	TestEqual(TEXT("Reading options preserves invalid serialized selection"), Node->SelectedFunction, FString(TEXT("OldSerializedSelection")));
	TestFalse(TEXT("Invalid selection still fails signature lookup"), UK2Node_UEmka::ParseScript(Node->GetScriptSource(), Node->SelectedFunction).UnsupportedReason.IsEmpty());

	Node->ScriptAsset->Source = TEXT("fn Replacement*() {}");
	TestTrue(TEXT("Options follow asset source edits"), Node->GetSelectedFunctionOptions().Contains(TEXT("Replacement")));
	Node->ScriptAsset = nullptr;
	TestTrue(TEXT("Removing asset restores inline options"), Node->GetSelectedFunctionOptions() == InlineOptions);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaFunctionPropertyAutoSelectionTest, "UEmka.Editor.Options.FunctionAutoSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaFunctionPropertyAutoSelectionTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("UEmkaFunctionSelection")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	UK2Node_UEmka* Node = NewObject<UK2Node_UEmka>(Graph);
	Graph->AddNode(Node);
	Node->CreateNewGuid();
	Node->Script.Empty();
	Node->AllocateDefaultPins();
	Node->OnScriptChanged(TEXT("fn First*() {}\nfn Second*() {}"));
	TestEqual(TEXT("First inline script selects its first export"), Node->SelectedFunction, FString(TEXT("First")));
	Node->SelectedFunction = TEXT("Second");
	Node->OnScriptChanged(TEXT("fn First*() {}\nfn Second*() { value := 1 }"));
	TestEqual(TEXT("Inline body edits preserve explicit export"), Node->SelectedFunction, FString(TEXT("Second")));
	Node->OnScriptChanged(TEXT("fn Renamed*() {}"));
	TestEqual(TEXT("Renaming selected export selects first remaining export"), Node->SelectedFunction, FString(TEXT("Renamed")));

	FProperty* AssetProperty = FindFProperty<FProperty>(UK2Node_UEmka::StaticClass(), GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, ScriptAsset));
	FPropertyChangedEvent AssetEvent(AssetProperty);
	Node->ScriptAsset = NewObject<UUEmkaScriptAsset>();
	Node->ScriptAsset->Source = TEXT("fn AssetFirst*() {}\nfn Renamed*() {}");
	Node->PostEditChangeProperty(AssetEvent);
	TestEqual(TEXT("Asset assignment resets a still-present prior choice to first export"), Node->SelectedFunction, FString(TEXT("AssetFirst")));
	Node->ScriptAsset = NewObject<UUEmkaScriptAsset>();
	Node->ScriptAsset->Source = TEXT("fn AdvanceCooldown*() {}");
	Node->PostEditChangeProperty(AssetEvent);
	TestEqual(TEXT("Switching assets replaces stale selection"), Node->SelectedFunction, FString(TEXT("AdvanceCooldown")));
	Node->ScriptAsset->Source = TEXT("fn ChangedAsset*() {}");
	Node->OnScriptAssetChanged(Node->ScriptAsset);
	TestEqual(TEXT("Asset source rename refreshes selected function"), Node->SelectedFunction, FString(TEXT("ChangedAsset")));

	Node->SelectedFunction = TEXT("CorruptedSelection");
	Node->RefreshScript();
	TestEqual(TEXT("Ordinary refresh preserves invalid serialized selection"), Node->SelectedFunction, FString(TEXT("CorruptedSelection")));
	Node->ScriptAsset = nullptr;
	Node->PostEditChangeProperty(AssetEvent);
	TestEqual(TEXT("Removing asset selects first inline export"), Node->SelectedFunction, FString(TEXT("Renamed")));
	Node->SetFlags(RF_Transactional);
	const FString PreviousScript = Node->Script;
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UEmkaTests", "AutoSelectFunction", "Edit Umka script"));
		Node->Modify();
		Node->OnScriptChanged(TEXT("fn AfterEdit*() {}"));
	}
	TestEqual(TEXT("Source transaction selects its new export"), Node->SelectedFunction, FString(TEXT("AfterEdit")));
	TestTrue(TEXT("Source transaction undoes"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores source"), Node->Script, PreviousScript);
	TestEqual(TEXT("Undo restores selected export"), Node->SelectedFunction, FString(TEXT("Renamed")));
	TestTrue(TEXT("Source transaction redoes"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores automatically selected export"), Node->SelectedFunction, FString(TEXT("AfterEdit")));
	return true;
}

#endif
