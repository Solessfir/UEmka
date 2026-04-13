// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "KismetNodes/SGraphNodeK2Base.h"
#include "UEmkaSyntaxHighlighter.h"

class UK2Node_UEmka;
class SMultiLineEditableTextBox;

// Custom Blueprint graph node visual for UK2Node_UEmka.
// Renders an embedded multi-line code editor between the input and output pins.
// Committing text (Enter / focus loss) triggers signature re-parse and pin reconstruction.
class SGraphNode_UEmka : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_UEmka) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node_UEmka* InNode);

	// SGraphNode interface
	virtual void UpdateGraphNode() override;

private:
	FText GetScriptText() const;

	void OnScriptTextCommitted(const FText& NewText, ETextCommit::Type CommitType) const;

	void OnScriptTextChanged(const FText& NewText) const;

	// OnTextChanged does not fire for undo/redo in SMultiLineEditableTextBox.
	// Intercept Ctrl+Z/Y here and schedule a one-shot deferred compile check.
	FReply OnTextBoxKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);

	EActiveTimerReturnType RecheckAfterUndoRedo(double InCurrentTime, float InDeltaTime);

	TWeakObjectPtr<UK2Node_UEmka> UEmkaNode;

	TSharedPtr<SMultiLineEditableTextBox> CodeEditor;

	TSharedPtr<FUEmkaSyntaxHighlighter> SyntaxHighlighter;
};
