// Copyright Solessfir 2026. All Rights Reserved.

#include "SGraphNode_UEmka.h"
#include "K2Node_UEmka.h"
#include "UEmkaFunctionLibrary.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SGraphNode_UEmka"

void SGraphNode_UEmka::Construct(const FArguments& InArgs, UK2Node_UEmka* InNode)
{
	UEmkaNode = InNode;
	GraphNode = InNode;
	SyntaxHighlighter = FUEmkaSyntaxHighlighter::Create();
	SyntaxHighlighter->SetErrorLine(InNode->LastErrorLine);
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

FText SGraphNode_UEmka::GetScriptText() const
{
	return UEmkaNode.IsValid() ? FText::FromString(UEmkaNode->Script) : FText::GetEmpty();
}

void SGraphNode_UEmka::OnScriptTextCommitted(const FText& NewText, ETextCommit::Type CommitType) const
{
	if (!UEmkaNode.IsValid())
	{
		return;
	}

	const FString NewScript = NewText.ToString();
	if (UEmkaNode->Script == NewScript)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("EditScript", "Edit Umka Script"));
	UEmkaNode->Modify();
	UEmkaNode->OnScriptChanged(NewScript);

	// If signature didn't change, ReconstructNode wasn't called and UpdateGraphNode
	// wasn't re-run - apply error state directly to the existing highlighter.
	SyntaxHighlighter->SetErrorLine(UEmkaNode->LastErrorLine);
	if (CodeEditor.IsValid())
	{
		CodeEditor->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SGraphNode_UEmka::OnScriptTextChanged(const FText& NewText) const
{
	if (!UEmkaNode.IsValid())
	{
		return;
	}

	// Re-evaluate on every text change (including undo) so the highlight always
	// reflects the current text box content, not just the last committed value.
	FString CompileError;
	int32 ErrorLine = -1;
	UUEmkaFunctionLibrary::CompileCheckScript(NewText.ToString(), CompileError, ErrorLine);
	SyntaxHighlighter->SetErrorLine(ErrorLine);
	if (CodeEditor.IsValid())
	{
		CodeEditor->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FReply SGraphNode_UEmka::OnTextBoxKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.IsControlDown() && (KeyEvent.GetKey() == EKeys::Z || KeyEvent.GetKey() == EKeys::Y))
	{
		// Undo/redo doesn't fire OnTextChanged - schedule a one-shot deferred recheck so the error highlight is updated after the text reverts.
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SGraphNode_UEmka::RecheckAfterUndoRedo));
		return FReply::Unhandled();
	}

	if (KeyEvent.GetKey() == EKeys::Tab && CodeEditor.IsValid())
	{
		if (KeyEvent.IsShiftDown())
		{
			const FTextLocation CursorLoc = CodeEditor->GetCursorLocation();
			const int32 LineIndex = CursorLoc.GetLineIndex();

			TArray<FString> Lines;
			CodeEditor->GetText().ToString().ParseIntoArrayLines(Lines, false);

			if (Lines.IsValidIndex(LineIndex))
			{
				FString& Line = Lines[LineIndex];
				int32 SpacesRemoved = 0;
				while (SpacesRemoved < 4 && Line.Len() > 0 && Line[0] == TEXT(' '))
				{
					Line.RemoveAt(0);
					++SpacesRemoved;
				}

				if (SpacesRemoved > 0)
				{
					CodeEditor->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
					CodeEditor->GoTo(FTextLocation(LineIndex, FMath::Max(0, CursorLoc.GetOffset() - SpacesRemoved)));
				}
			}
			return FReply::Handled();
		}

		CodeEditor->InsertTextAtCursor(TEXT("    "));
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

EActiveTimerReturnType SGraphNode_UEmka::RecheckAfterUndoRedo(double InCurrentTime, float InDeltaTime)
{
	if (UEmkaNode.IsValid() && CodeEditor.IsValid())
	{
		FString CompileError;
		int32 ErrorLine = -1;
		UUEmkaFunctionLibrary::CompileCheckScript(CodeEditor->GetText().ToString(), CompileError, ErrorLine);
		SyntaxHighlighter->SetErrorLine(ErrorLine);
		CodeEditor->Invalidate(EInvalidateWidgetReason::Layout);
	}
	return EActiveTimerReturnType::Stop;
}

void SGraphNode_UEmka::UpdateGraphNode()
{
	// Sync error state before the text layout initializes - ParseTokens runs during
	// SMultiLineEditableTextBox construction and needs ErrorLine set beforehand.
	if (UEmkaNode.IsValid())
	{
		SyntaxHighlighter->SetErrorLine(UEmkaNode->LastErrorLine);
	}

	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	// Resolve icon - same guard UE uses in its own nodes
	IconColor = FLinearColor::White;
	const FSlateBrush* IconBrush = nullptr;
	if (GraphNode && GraphNode->ShowPaletteIconOnNode())
	{
		IconBrush = GraphNode->GetIconAndTint(IconColor).GetOptionalIcon();
	}

	const TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

	// Code editor widget
	const TSharedRef<SWidget> CodeWidget =
		SNew(SBox)
		.MinDesiredWidth(280.f)
		.MinDesiredHeight(110.f)
		.MaxDesiredHeight(400.f)
		[
			SAssignNew(CodeEditor, SMultiLineEditableTextBox)
			.Text(this, &SGraphNode_UEmka::GetScriptText)
			.OnTextCommitted(this, &SGraphNode_UEmka::OnScriptTextCommitted)
			.OnTextChanged(this, &SGraphNode_UEmka::OnScriptTextChanged)
			.OnKeyDownHandler(this, &SGraphNode_UEmka::OnTextBoxKeyDown)
			.Marshaller(SyntaxHighlighter)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.AutoWrapText(false)
			.AllowMultiLine(true)
			.BackgroundColor(FLinearColor::Transparent)
			.ForegroundColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.f))
			.Padding(FMargin(6.f, 4.f))
		];

	// Full node layout
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.Node.Body"))
			.Padding(0.f)
			[
				SNew(SVerticalBox)

				// Title bar - mirrors UE's standard K2 node title area
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("Graph.Node.ColorSpill"))
					.BorderBackgroundColor(this, &SGraphNode::GetNodeTitleColor)
					.Padding(FMargin(10.f, 4.f, 6.f, 4.f))
					[
						SNew(SHorizontalBox)

						// Icon
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SImage)
							.Image(IconBrush)
							.ColorAndOpacity(FLinearColor(0.5f, 1.0f, 0.5f))
						]

						// Title + subtitle (SNodeTitle handles both lines)
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(InlineEditableText, SInlineEditableTextBlock)
								.Style(FAppStyle::Get(), "Graph.Node.NodeTitleInlineEditableText")
								.Text(NodeTitle.Get(), &SNodeTitle::GetHeadTitle)
								.IsReadOnly(true)
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								NodeTitle.ToSharedRef()
							]
						]
					]
				]

				// Pins + code editor row
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					// Left: input pins
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Left)
					[
						SAssignNew(LeftNodeBox, SVerticalBox)
					]

					// Center: code editor
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.HAlign(HAlign_Fill)
					.Padding(4.f, 4.f)
					[
						CodeWidget
					]

					// Right: output pins
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					[
						SAssignNew(RightNodeBox, SVerticalBox)
					]
				]
			]
		];

	// Add pin widgets
	CreatePinWidgets();
}

#undef LOCTEXT_NAMESPACE
