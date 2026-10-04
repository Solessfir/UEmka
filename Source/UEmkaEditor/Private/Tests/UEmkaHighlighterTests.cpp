// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaSyntaxHighlighter.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Text/IRun.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SNullWidget.h"

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

#endif
