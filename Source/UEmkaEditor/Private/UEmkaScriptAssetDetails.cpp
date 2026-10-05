// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaScriptAssetDetails.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "InputCoreTypes.h"
#include "PropertyHandle.h"
#include "Styling/CoreStyle.h"
#include "UEmkaScriptAsset.h"
#include "UEmkaSyntaxHighlighter.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "UEmkaScriptAssetDetails"

TSharedRef<IDetailCustomization> FUEmkaScriptAssetDetails::MakeInstance()
{
	return MakeShared<FUEmkaScriptAssetDetails>();
}

void FUEmkaScriptAssetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	if (IDetailPropertyRow* ImportDataRow = DetailBuilder.EditDefaultProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUEmkaScriptAsset, AssetImportData))))
	{
		ImportDataRow->ShouldAutoExpand(true);
	}
	ModulePathHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUEmkaScriptAsset, ModulePath));
	if (IDetailPropertyRow* ModulePathRow = DetailBuilder.EditDefaultProperty(ModulePathHandle))
	{
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> ValueWidget;
		ModulePathRow->GetDefaultWidgets(NameWidget, ValueWidget);
		ModulePathRow->CustomWidget()
			.NameContent()[NameWidget.ToSharedRef()]
			.ValueContent()
			.MinDesiredWidth(280.f)
			.MaxDesiredWidth(600.f)
			[
				SNew(SEditableTextBox)
				.Tag(TEXT("UEmka.AssetModulePathEditor"))
				.Text(this, &FUEmkaScriptAssetDetails::GetModulePathText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.IsReadOnly_Lambda([Handle = ModulePathHandle] { return Handle->IsEditConst(); })
				.ToolTipText(ModulePathHandle->GetToolTipText())
				.OnVerifyTextChanged(this, &FUEmkaScriptAssetDetails::VerifyModulePathText)
				.OnTextCommitted(this, &FUEmkaScriptAssetDetails::OnModulePathCommitted)
			];
	}
	SourceHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUEmkaScriptAsset, Source));
	IDetailPropertyRow* SourceRow = DetailBuilder.EditDefaultProperty(SourceHandle);
	if (!SourceRow) return;

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;
	SourceRow->GetDefaultWidgets(NameWidget, ValueWidget);
	SourceRow->CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		.MinDesiredWidth(280.f)
		.MaxDesiredWidth(600.f)
		[
			SAssignNew(SourceEditor, SMultiLineEditableTextBox)
			.Tag(TEXT("UEmka.AssetSourceEditor"))
			.Text(this, &FUEmkaScriptAssetDetails::GetSourceText)
			.IsReadOnly_Lambda([Handle = SourceHandle] { return Handle->IsEditConst(); })
			.ToolTipText_Lambda([Handle = SourceHandle]
			{
				TArray<UObject*> Objects;
				Handle->GetOuterObjects(Objects);
				for (const UObject* Object : Objects)
				{
					const UUEmkaScriptAsset* Asset = Cast<UUEmkaScriptAsset>(Object);
					if (Asset && Asset->IsFileBacked()) return LOCTEXT("FileBackedSource", "Edit the external .um file, then reimport this asset to update its source.");
				}
				return Handle->GetToolTipText();
			})
			.OnTextCommitted(this, &FUEmkaScriptAssetDetails::OnSourceTextCommitted)
			.OnKeyDownHandler(this, &FUEmkaScriptAssetDetails::OnSourceKeyDown)
			.Marshaller(FUEmkaSyntaxHighlighter::Create())
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.AutoWrapText(false)
			.AllowMultiLine(true)
			.SelectAllTextWhenFocused(false)
			.SelectAllTextOnCommit(false)
			.ClearKeyboardFocusOnCommit(false)
			.BackgroundColor(FLinearColor::Transparent)
			.ForegroundColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.f))
			.Padding(FMargin(6.f, 4.f))
		];
}

FText FUEmkaScriptAssetDetails::GetSourceText() const
{
	FString Source;
	if (SourceHandle->GetValue(Source) == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}
	return FText::FromString(Source);
}

void FUEmkaScriptAssetDetails::OnSourceTextCommitted(const FText& NewText, ETextCommit::Type CommitType) const
{
	FString Source;
	const FPropertyAccess::Result Result = SourceHandle->GetValue(Source);
	const FString NewSource = NewText.ToString();
	if (Result == FPropertyAccess::Fail || SourceHandle->IsEditConst()) return;
	if (Result == FPropertyAccess::MultipleValues && NewText.EqualTo(LOCTEXT("MultipleValues", "Multiple Values"))) return;
	if (Result == FPropertyAccess::Success && NewSource.Equals(Source, ESearchCase::CaseSensitive)) return;
	SourceHandle->SetValue(NewSource);
}

FReply FUEmkaScriptAssetDetails::OnSourceKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) const
{
	if (KeyEvent.GetKey() == EKeys::Tab && !KeyEvent.IsShiftDown() && !KeyEvent.IsControlDown() && !KeyEvent.IsAltDown() && !SourceHandle->IsEditConst())
	{
		SourceEditor->InsertTextAtCursor(TEXT("    "));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FText FUEmkaScriptAssetDetails::GetModulePathText() const
{
	FString Path;
	if (ModulePathHandle->GetValue(Path) == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}
	return FText::FromString(Path);
}

bool FUEmkaScriptAssetDetails::VerifyModulePathText(const FText& NewText, FText& OutError) const
{
	FString CurrentPath;
	if (ModulePathHandle->GetValue(CurrentPath) == FPropertyAccess::MultipleValues && NewText.EqualTo(LOCTEXT("MultipleValues", "Multiple Values"))) return true;
	const FString Path = NewText.ToString();
	if (Path.IsEmpty() || UUEmkaScriptAsset::IsValidModulePath(Path)) return true;
	OutError = LOCTEXT("InvalidModulePath", "Use a relative .um path without empty, '.' or '..' segments, or leave this blank to use the asset name.");
	return false;
}

void FUEmkaScriptAssetDetails::OnModulePathCommitted(const FText& NewText, ETextCommit::Type CommitType) const
{
	FString Path;
	const FPropertyAccess::Result Result = ModulePathHandle->GetValue(Path);
	if (Result == FPropertyAccess::Fail || ModulePathHandle->IsEditConst()) return;
	if (Result == FPropertyAccess::MultipleValues && NewText.EqualTo(LOCTEXT("MultipleValues", "Multiple Values"))) return;
	FText Error;
	if (!VerifyModulePathText(NewText, Error)) return;
	const FString NewPath = NewText.ToString();
	if (Result == FPropertyAccess::Success && NewPath.Equals(Path, ESearchCase::CaseSensitive)) return;
	ModulePathHandle->SetValue(NewPath);
}

#undef LOCTEXT_NAMESPACE
