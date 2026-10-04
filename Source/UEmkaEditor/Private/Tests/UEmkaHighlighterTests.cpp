// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaSyntaxHighlighter.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Text/IRun.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Input/HittestGrid.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"

namespace
{
FString HighlightStyleAt(const FSlateTextLayout& Layout, const int32 LineIndex, const int32 Offset)
{
	const TArray<FTextLayout::FLineModel>& Lines = Layout.GetLineModels();
	if (!Lines.IsValidIndex(LineIndex)) return FString();
	for (const FTextLayout::FRunModel& Run : Lines[LineIndex].Runs)
	{
		const FTextRange Range = Run.GetTextRange();
		if (Offset >= Range.BeginIndex && Offset < Range.EndIndex) return Run.GetRun()->GetRunInfo().Name;
	}
	return FString();
}

TArray<FLinearColor> PaintHighlightColors(FSlateTextLayout& Layout)
{
	Layout.UpdateIfNeeded();
	const TSharedRef<SWindow> Window = SNew(SWindow);
	FSlateWindowElementList Elements(Window);
	FHittestGrid HitTestGrid;
	const FPaintArgs PaintArgs(&SNullWidget::NullWidget.Get(), HitTestGrid, FVector2D::ZeroVector, 0.0, 0.f);
	Layout.OnPaint(PaintArgs, FGeometry::MakeRoot(FVector2D(800, 400), FSlateLayoutTransform()),
		FSlateRect(0, 0, 800, 400), Elements, 0, FWidgetStyle(), true);
	TArray<FLinearColor> Colors;
	for (const FSlateTextElement& Element : Elements.GetUncachedDrawElements().Get<static_cast<uint8>(EElementType::ET_Text)>())
	{
		Colors.Add(Element.GetTint());
	}
	for (const FSlateShapedTextElement& Element : Elements.GetUncachedDrawElements().Get<static_cast<uint8>(EElementType::ET_ShapedText)>())
	{
		Colors.Add(Element.GetTint());
	}
	return Colors;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaRawStringHighlightingTest, "UEmka.Editor.RawStringHighlighting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaRawStringHighlightingTest::RunTest(const FString& Parameters)
{
	const FString Source = TEXT("var Text = `\"\n// /* */ ' \\\n\ntail \\` var Value = 1\nfn Test*() {}");
	const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(
		&SNullWidget::NullWidget.Get(), FTextBlockStyle::GetDefault());
	FUEmkaSyntaxHighlighter::Create()->SetText(Source, *Layout);
	const TArray<FTextLayout::FLineModel>& Lines = Layout->GetLineModels();
	if (!TestEqual(TEXT("Multiline source retains every line"), Lines.Num(), 5)) return false;

	const auto StyleAt = [&Lines](const int32 LineIndex, const int32 Offset)
	{
		for (const FTextLayout::FRunModel& Run : Lines[LineIndex].Runs)
		{
			const FTextRange Range = Run.GetTextRange();
			if (Offset >= Range.BeginIndex && Offset < Range.EndIndex)
			{
				return Run.GetRun()->GetRunInfo().Name;
			}
		}
		return FString();
	};

	TestEqual(TEXT("Code before raw string retains keyword style"), StyleAt(0, 0), FString(TEXT("UEmka.Keyword")));
	for (const int32 LineIndex : {0, 1, 3})
	{
		const FString& Line = *Lines[LineIndex].Text;
		const int32 Begin = LineIndex == 0 ? Line.Find(TEXT("`")) : 0;
		const int32 End = LineIndex == 3 ? Line.Find(TEXT("`")) + 1 : Line.Len();
		for (int32 Offset = Begin; Offset < End; ++Offset)
		{
			TestEqual(FString::Printf(TEXT("Raw string style at line %d column %d"), LineIndex + 1, Offset + 1),
				StyleAt(LineIndex, Offset), FString(TEXT("UEmka.String")));
		}
	}

	const FString& ClosingLine = *Lines[3].Text;
	TestEqual(TEXT("Backslash does not escape closing backtick"),
		StyleAt(3, ClosingLine.Find(TEXT("var"))), FString(TEXT("UEmka.Keyword")));
	TestEqual(TEXT("Number after raw string retains number style"),
		StyleAt(3, ClosingLine.Find(TEXT("1"))), FString(TEXT("UEmka.Number")));
	TestEqual(TEXT("Next line resumes normal code highlighting"), StyleAt(4, 0), FString(TEXT("UEmka.Keyword")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaQuotedHighlightingTest, "UEmka.Editor.QuotedStringAndCharacterHighlighting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaQuotedHighlightingTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Sources =
	{
		TEXT("var Text = \"escaped \\\" // fn fake() \\\" quote\" var Count = 1"),
		TEXT("var Text = \"backslash \\\\\" var Count = 1"),
		TEXT("var Text = \"odd \\\\\\\" // still quoted\" var Count = 1"),
		TEXT("var Character = '\\'' var Count = 1"),
		TEXT("var Character = '\\\\' var Count = 1"),
	};
	for (const FString& Source : Sources)
	{
		const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(&SNullWidget::NullWidget.Get(), FTextBlockStyle::GetDefault());
		FUEmkaSyntaxHighlighter::Create()->SetText(Source, *Layout);
		const int32 CountOffset = Source.Find(TEXT("var Count"));
		TestEqual(TEXT("Escaped quote parity preserves following keyword"), HighlightStyleAt(*Layout, 0, CountOffset), FString(TEXT("UEmka.Keyword")));
		TestEqual(TEXT("Number following quoted value highlighted"), HighlightStyleAt(*Layout, 0, Source.Len() - 1), FString(TEXT("UEmka.Number")));
		const int32 FakeComment = Source.Find(TEXT("//"));
		if (FakeComment != INDEX_NONE)
		{
			TestEqual(TEXT("Comment markers inside escaped string stay string"), HighlightStyleAt(*Layout, 0, FakeComment), FString(TEXT("UEmka.String")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaTokenHighlightingTest, "UEmka.Editor.TokenAndMultilineCommentHighlighting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaTokenHighlightingTest::RunTest(const FString& Parameters)
{
	const FString Source = TEXT("/* var fake = \"fn\"\nreturn false */ var Value: real32 = 12.5\n")
		TEXT("var variant: uint16 = 42 // return true\n")
		TEXT("print(1) += true && false || nil\n")
		TEXT("if_flag str_value true_value falsehood nil_value returnValue integer");
	const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(&SNullWidget::NullWidget.Get(), FTextBlockStyle::GetDefault());
	FUEmkaSyntaxHighlighter::Create()->SetText(Source, *Layout);
	const TArray<FTextLayout::FLineModel>& Lines = Layout->GetLineModels();
	if (!TestEqual(TEXT("Every source line retained"), Lines.Num(), 5)) return false;
	TestEqual(TEXT("Multiline comment starts before fake tokens"), HighlightStyleAt(*Layout, 0, 3), FString(TEXT("UEmka.Comment")));
	TestEqual(TEXT("Multiline comment state carries across newline"), HighlightStyleAt(*Layout, 1, 0), FString(TEXT("UEmka.Comment")));
	TestEqual(TEXT("Code resumes after block terminator"), HighlightStyleAt(*Layout, 1, Lines[1].Text->Find(TEXT("var"))), FString(TEXT("UEmka.Keyword")));
	TestEqual(TEXT("Long type token retains type style"), HighlightStyleAt(*Layout, 1, Lines[1].Text->Find(TEXT("real32"))), FString(TEXT("UEmka.Type")));
	TestEqual(TEXT("Numeric literal retains number style"), HighlightStyleAt(*Layout, 1, Lines[1].Text->Find(TEXT("12"))), FString(TEXT("UEmka.Number")));
	TestEqual(TEXT("Keyword prefix in identifier stays normal"), HighlightStyleAt(*Layout, 2, Lines[2].Text->Find(TEXT("variant"))), FString(TEXT("UEmka.Normal")));
	TestEqual(TEXT("Line comment masks trailing keywords"), HighlightStyleAt(*Layout, 2, Lines[2].Text->Find(TEXT("return"))), FString(TEXT("UEmka.Comment")));
	TestEqual(TEXT("Line comment state resets on next line"), HighlightStyleAt(*Layout, 3, 0), FString(TEXT("UEmka.Function")));
	for (const FString& Token : {FString(TEXT("true")), FString(TEXT("false")), FString(TEXT("nil"))})
	{
		TestEqual(Token + TEXT(" builtin style"), HighlightStyleAt(*Layout, 3, Lines[3].Text->Find(Token)), FString(TEXT("UEmka.Builtin")));
	}
	for (const FString& Token : {FString(TEXT("+=")), FString(TEXT("&&")), FString(TEXT("||"))})
	{
		TestEqual(Token + TEXT(" operator style"), HighlightStyleAt(*Layout, 3, Lines[3].Text->Find(Token)), FString(TEXT("UEmka.Operator")));
	}
	for (int32 Offset = 0; Offset < Lines[4].Text->Len(); ++Offset)
	{
		TestEqual(TEXT("Keywords types and literals require complete word boundaries"), HighlightStyleAt(*Layout, 4, Offset), FString(TEXT("UEmka.Normal")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaErrorHighlightingTest, "UEmka.Editor.ErrorHighlightingSetAndClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaErrorHighlightingTest::RunTest(const FString& Parameters)
{
	const FString Source = TEXT("var First = 1\nvar Second = 2 // comment");
	const TSharedRef<FSlateTextLayout> Layout = FSlateTextLayout::Create(&SNullWidget::NullWidget.Get(), FTextBlockStyle::GetDefault());
	const TSharedRef<FUEmkaSyntaxHighlighter> Highlighter = FUEmkaSyntaxHighlighter::Create();
	Highlighter->SetText(Source, *Layout);
	const TArray<FLinearColor> OriginalColors = PaintHighlightColors(*Layout);
	TestFalse(TEXT("Actual Slate layout paints text"), OriginalColors.IsEmpty());
	Highlighter->ClearDirty();
	Highlighter->SetErrorLine(2);
	TestTrue(TEXT("Setting an error invalidates marshaller"), Highlighter->IsDirty());
	Layout->ClearLines();
	Highlighter->SetText(Source, *Layout);
	const TArray<FLinearColor> ErrorColors = PaintHighlightColors(*Layout);
	TestTrue(TEXT("Error source line paints red"), ErrorColors.Contains(FLinearColor(1.f, 0.1f, 0.1f)));
	TestTrue(TEXT("Comments retain their own color on error line"), ErrorColors.Contains(FLinearColor(0.47f, 0.62f, 0.42f)));
	TestTrue(TEXT("Other source line keeps keyword color"), ErrorColors.Contains(FLinearColor(0.86f, 0.45f, 0.18f)));
	Highlighter->ClearDirty();
	Highlighter->SetErrorLine(2);
	TestFalse(TEXT("Same error line does not invalidate again"), Highlighter->IsDirty());
	Highlighter->SetErrorLine(-1);
	TestTrue(TEXT("Clearing error invalidates marshaller"), Highlighter->IsDirty());
	Layout->ClearLines();
	Highlighter->SetText(Source, *Layout);
	TestTrue(TEXT("Clearing error restores original painted colors"), PaintHighlightColors(*Layout) == OriginalColors);
	return true;
}

#endif
