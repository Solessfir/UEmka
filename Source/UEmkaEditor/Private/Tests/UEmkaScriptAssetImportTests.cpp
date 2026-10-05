// Copyright Solessfir 2026. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UEmkaScriptAsset.h"
#include "UEmkaScriptAssetFactory.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaScriptAssetImportTest, "UEmka.Editor.Assets.Import",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaScriptAssetImportTest::RunTest(const FString& Parameters)
{
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/UEmkaImport") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Filename = Directory / TEXT("preserved-name.UM");
	const FString ReplacementFilename = Directory / TEXT("replacement.um");
	IFileManager::Get().MakeDirectory(*Directory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Filename);
		IFileManager::Get().Delete(*ReplacementFilename);
		IFileManager::Get().DeleteDirectory(*Directory);
	};
	const FString Source = TEXT("// \u017c\u00f3\u0142\u0107 \u4e16\u754c \U0001f642\r\n\tfn Incomplete*(Value: int) {\r\n\t  return Value +\r\n\r\n");
	if (!TestTrue(TEXT("UTF-8 source file is written"), FFileHelper::SaveStringToFile(Source, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	UUEmkaScriptAssetFactory* Factory = NewObject<UUEmkaScriptAssetFactory>();
	TestTrue(TEXT("Uppercase Umka extension is importable"), Factory->FactoryCanImport(Filename));
	TestFalse(TEXT("Other source extensions are rejected"), Factory->FactoryCanImport(Directory / TEXT("script.txt")));
	bool bCanceled = false;
	const FName AssetName = MakeUniqueObjectName(GetTransientPackage(), UUEmkaScriptAsset::StaticClass(), TEXT("RenamedScript"));
	UUEmkaScriptAsset* Asset = Cast<UUEmkaScriptAsset>(Factory->ImportObject(UUEmkaScriptAsset::StaticClass(), GetTransientPackage(), AssetName, RF_Transient | RF_Transactional, Filename, nullptr, bCanceled));
	if (!TestNotNull(TEXT("File import creates a script asset"), Asset)) return false;
	TestFalse(TEXT("Import is not canceled"), bCanceled);
	TestEqual(TEXT("Unicode, line endings, whitespace and incomplete source are preserved"), Asset->Source, Source);
	TestEqual(TEXT("Module filename keeps source basename independently of asset name"), Asset->ModulePath, FString(TEXT("preserved-name.um")));
	TestTrue(TEXT("Imported dependencies are assigned explicitly"), Asset->Imports.IsEmpty());
	TestTrue(TEXT("New asset creation remains available after file import"), Factory->CanCreateNew());
	TestTrue(TEXT("Imported assets are file backed"), Asset->IsFileBacked());
	TestEqual(TEXT("Import metadata resolves the original source filename"), Asset->AssetImportData->GetFirstFilename(), FPaths::ConvertRelativePathToFull(Filename));
	const FProperty* SourceProperty = FindFProperty<FProperty>(UUEmkaScriptAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UUEmkaScriptAsset, Source));
	TestFalse(TEXT("Imported source is read only"), Asset->CanEditChange(SourceProperty));
	FAssetRegistryTagsContextData TagData(Asset);
	Asset->GetAssetRegistryTags(FAssetRegistryTagsContext(TagData));
	const UObject::FAssetRegistryTag* SourceFileTag = TagData.Tags.Find(UObject::SourceFileTagName());
	if (TestNotNull(TEXT("Native source file registry tag is present"), SourceFileTag))
	{
		TestEqual(TEXT("Source file tag contains native import metadata JSON"), SourceFileTag->Value, Asset->AssetImportData->GetSourceData().ToJson());
		TestTrue(TEXT("Source file tag is hidden"), SourceFileTag->Type == UObject::FAssetRegistryTag::TT_Hidden);
	}
	TArray<FString> ReimportFiles;
	TestTrue(TEXT("Native reimport manager recognizes the imported asset"), FReimportManager::Instance()->CanReimport(Asset, &ReimportFiles));
	TestEqual(TEXT("Reimport exposes one source file"), ReimportFiles.Num(), 1);
	Asset->ModulePath = TEXT("lib/stable.um");
	UUEmkaScriptAsset* Dependency = NewObject<UUEmkaScriptAsset>();
	Asset->Imports.Add(Dependency);
	const FString ChangedSource = TEXT("fn Updated*(): int { return 42 }\n");
	bool bUpdatedNotification = false;
	const FDelegateHandle Handle = UUEmkaScriptAsset::OnScriptAssetChanged.AddLambda([&](UUEmkaScriptAsset* ChangedAsset)
	{
		if (ChangedAsset == Asset) bUpdatedNotification = ChangedAsset->Source == ChangedSource;
	});
	ON_SCOPE_EXIT { UUEmkaScriptAsset::OnScriptAssetChanged.Remove(Handle); };
	if (!TestTrue(TEXT("Changed source file is written"), FFileHelper::SaveStringToFile(ChangedSource, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	TestTrue(TEXT("Native reimport reloads changed source"), FReimportManager::Instance()->Reimport(Asset, false, false, FString(), Factory));
	TestEqual(TEXT("Reimport updates stored source"), Asset->Source, ChangedSource);
	TestTrue(TEXT("Change listeners see updated source"), bUpdatedNotification);
	TestEqual(TEXT("Reimport preserves module path"), Asset->ModulePath, FString(TEXT("lib/stable.um")));
	TestTrue(TEXT("Reimport preserves dependency references"), Asset->Imports.Num() == 1 && Asset->Imports[0] == Dependency);
	Factory->SetReimportPaths(Asset, {Directory / TEXT("missing.um")});
	const FString MissingMetadata = Asset->AssetImportData->GetSourceData().ToJson();
	TestTrue(TEXT("Missing source file fails reimport"), Factory->Reimport(Asset) == EReimportResult::Failed);
	TestEqual(TEXT("Failed reimport preserves stored source"), Asset->Source, ChangedSource);
	TestEqual(TEXT("Failed reimport preserves import metadata"), Asset->AssetImportData->GetSourceData().ToJson(), MissingMetadata);
	TestEqual(TEXT("Failed reimport preserves module path"), Asset->ModulePath, FString(TEXT("lib/stable.um")));
	TestTrue(TEXT("Failed reimport preserves dependencies and source lock"), Asset->Imports.Num() == 1 && Asset->Imports[0] == Dependency && Asset->IsFileBacked() && !Asset->CanEditChange(SourceProperty));
	const FString ReplacementSource = TEXT("fn Replaced*(): int { return 7 }\n");
	if (!TestTrue(TEXT("Replacement source file is written"), FFileHelper::SaveStringToFile(ReplacementSource, *ReplacementFilename, FFileHelper::EEncodingOptions::ForceUTF8))) return false;
	FReimportManager::Instance()->UpdateReimportPaths(Asset, {ReplacementFilename});
	TestEqual(TEXT("Native path change updates source binding"), Asset->AssetImportData->GetFirstFilename(), FPaths::ConvertRelativePathToFull(ReplacementFilename));
	TestTrue(TEXT("Replacement file reimports successfully"), Factory->Reimport(Asset) == EReimportResult::Succeeded);
	TestEqual(TEXT("UTF-8 BOM is not included in replacement source"), Asset->Source, ReplacementSource);
	TestEqual(TEXT("Replacement file keeps virtual module path"), Asset->ModulePath, FString(TEXT("lib/stable.um")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaScriptAssetNewTest, "UEmka.Editor.Assets.New",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaScriptAssetNewTest::RunTest(const FString& Parameters)
{
	UUEmkaScriptAssetFactory* Factory = NewObject<UUEmkaScriptAssetFactory>();
	TestTrue(TEXT("Factory remains in the new asset menu"), Factory->ShouldShowInNewMenu());
	TestTrue(TEXT("New assets open for editing"), Factory->bEditAfterNew);
	bool bCanceled = false;
	const FName AssetName = MakeUniqueObjectName(GetTransientPackage(), UUEmkaScriptAsset::StaticClass(), TEXT("NewScript"));
	UUEmkaScriptAsset* Asset = Cast<UUEmkaScriptAsset>(Factory->ImportObject(UUEmkaScriptAsset::StaticClass(), GetTransientPackage(), AssetName, RF_Transient, FString(), nullptr, bCanceled));
	if (!TestNotNull(TEXT("New asset creation creates a script asset"), Asset)) return false;
	TestTrue(TEXT("New source starts empty"), Asset->Source.IsEmpty());
	TestTrue(TEXT("New module path uses the asset name by default"), Asset->ModulePath.IsEmpty());
	TestTrue(TEXT("New assets have no imports"), Asset->Imports.IsEmpty());
	TestNotNull(TEXT("New assets have import metadata ready for binding"), Asset->AssetImportData.Get());
	TestFalse(TEXT("New assets are not file backed"), Asset->IsFileBacked());
	const FProperty* SourceProperty = FindFProperty<FProperty>(UUEmkaScriptAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UUEmkaScriptAsset, Source));
	TestTrue(TEXT("New source remains editable"), Asset->CanEditChange(SourceProperty));
	TArray<FString> ReimportFiles;
	TestFalse(TEXT("New assets have no reimport binding"), Factory->CanReimport(Asset, ReimportFiles));
	return true;
}

#endif
