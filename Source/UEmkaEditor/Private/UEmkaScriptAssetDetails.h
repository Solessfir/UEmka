// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Types/SlateEnums.h"

class IPropertyHandle;
class SMultiLineEditableTextBox;
struct FGeometry;
struct FKeyEvent;

class FUEmkaScriptAssetDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FText GetSourceText() const;
	void OnSourceTextCommitted(const FText& NewText, ETextCommit::Type CommitType) const;
	FReply OnSourceKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) const;
	FText GetModulePathText() const;
	bool VerifyModulePathText(const FText& NewText, FText& OutError) const;
	void OnModulePathCommitted(const FText& NewText, ETextCommit::Type CommitType) const;

	TSharedPtr<IPropertyHandle> SourceHandle;
	TSharedPtr<IPropertyHandle> ModulePathHandle;
	TSharedPtr<SMultiLineEditableTextBox> SourceEditor;
};
