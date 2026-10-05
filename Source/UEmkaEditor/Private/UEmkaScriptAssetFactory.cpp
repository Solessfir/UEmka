// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaScriptAssetFactory.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UEmkaScriptAsset.h"

UUEmkaScriptAssetFactory::UUEmkaScriptAssetFactory()
{
	SupportedClass = UUEmkaScriptAsset::StaticClass();
	Formats.Add(TEXT("um;Umka Script"));
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = true;
	bText = true;
}

bool UUEmkaScriptAssetFactory::CanCreateNew() const
{
	// UFactory::ImportObject checks this before considering the supplied filename.
	return Super::CanCreateNew() && GetCurrentFilename().IsEmpty();
}

UObject* UUEmkaScriptAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UUEmkaScriptAsset>(InParent, Class, Name, Flags);
}

UObject* UUEmkaScriptAssetFactory::FactoryCreateText(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn)
{
	UUEmkaScriptAsset* Asset = NewObject<UUEmkaScriptAsset>(InParent, Class, Name, Flags);
	Asset->Source = FString(static_cast<int32>(BufferEnd - Buffer), Buffer);
	if (!GetCurrentFilename().IsEmpty())
	{
		Asset->ModulePath = FPaths::GetBaseFilename(GetCurrentFilename()) + TEXT(".um");
		Asset->AssetImportData->Update(GetCurrentFilename());
	}
	return Asset;
}

bool UUEmkaScriptAssetFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	const UUEmkaScriptAsset* Asset = Cast<UUEmkaScriptAsset>(Obj);
	if (!Asset || !Asset->IsFileBacked()) return false;
	Asset->AssetImportData->ExtractFilenames(OutFilenames);
	return true;
}

void UUEmkaScriptAssetFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UUEmkaScriptAsset* Asset = Cast<UUEmkaScriptAsset>(Obj);
	if (Asset && Asset->AssetImportData && NewReimportPaths.Num() == 1) Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
}

EReimportResult::Type UUEmkaScriptAssetFactory::Reimport(UObject* Obj)
{
	UUEmkaScriptAsset* Asset = Cast<UUEmkaScriptAsset>(Obj);
	if (!Asset || !Asset->IsFileBacked()) return EReimportResult::Failed;
	const FString Filename = Asset->AssetImportData->GetFirstFilename();
	FString Source;
	if (!FFileHelper::LoadFileToString(Source, *Filename)) return EReimportResult::Failed;
	const FScopedTransaction Transaction(NSLOCTEXT("UEmka", "ReimportScriptAsset", "Reimport Umka Script"));
	Asset->Modify();
	Asset->AssetImportData->Modify();
	Asset->Source = MoveTemp(Source);
	Asset->AssetImportData->Update(Filename);
	Asset->PostEditChange();
	Asset->MarkPackageDirty();
	return EReimportResult::Succeeded;
}

int32 UUEmkaScriptAssetFactory::GetPriority() const
{
	return ImportPriority;
}
