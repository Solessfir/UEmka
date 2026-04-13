// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaSyntaxHighlighter.h"
#include "Framework/Text/IRun.h"
#include "Framework/Text/TextLayout.h"
#include "Framework/Text/SlateTextRun.h"

// -------------------------------------------------------------------------
// Token sets
// -------------------------------------------------------------------------

static const TSet<FString> UmkaKeywords =
{
	TEXT("break"), TEXT("case"), TEXT("const"), TEXT("continue"), TEXT("default"),
	TEXT("else"), TEXT("enum"), TEXT("fn"), TEXT("for"), TEXT("import"),
	TEXT("interface"), TEXT("if"), TEXT("in"), TEXT("map"), TEXT("return"),
	TEXT("struct"), TEXT("switch"), TEXT("type"), TEXT("var"), TEXT("weak")
};

// Longer names must be listed before their prefixes in tokenizer rules.
static const TArray<FString> UmkaTypeRules =
{
	TEXT("real32"), TEXT("real"),
	TEXT("int8"), TEXT("int16"), TEXT("int32"), TEXT("int"),
	TEXT("uint8"), TEXT("uint16"), TEXT("uint32"), TEXT("uint"),
	TEXT("bool"), TEXT("char"), TEXT("str"), TEXT("void"), TEXT("fiber"), TEXT("any")
};

static const TSet<FString> UmkaTypes(UmkaTypeRules);

static const TSet<FString> UmkaBuiltins = { TEXT("true"), TEXT("false"), TEXT("nil") };

// -------------------------------------------------------------------------
// Style factory
// -------------------------------------------------------------------------

static FTextBlockStyle MakeStyle(const FLinearColor& Color)
{
	FTextBlockStyle Style = FTextBlockStyle::GetDefault();
	Style.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9));
	Style.SetColorAndOpacity(FSlateColor(Color));
	return Style;
}

// -------------------------------------------------------------------------
// Create
// -------------------------------------------------------------------------

TSharedRef<FUEmkaSyntaxHighlighter> FUEmkaSyntaxHighlighter::Create()
{
	TArray<FSyntaxTokenizer::FRule> Rules;

	// Comment and string delimiters - must precede operator sub-strings
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("//")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("/*")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("*/")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("\"")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("'")));

	// Multi-char operators (longer before shorter to avoid partial matches)
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("...")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("<<=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT(">>=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT(":=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("+=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("-=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("*=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("/=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("%=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("&=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("|=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("^=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("&&")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("||")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("++")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("--")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("==")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("!=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("<=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT(">=")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("::")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT("<<")));
	Rules.Emplace(FSyntaxTokenizer::FRule(TEXT(">>")));

	// Types (longer before shorter - same prefix problem as operators)
	for (const FString& T : UmkaTypeRules)
	{
		Rules.Emplace(FSyntaxTokenizer::FRule(T));
	}

	// Keywords
	for (const FString& KW : UmkaKeywords)
	{
		Rules.Emplace(FSyntaxTokenizer::FRule(KW));
	}

	// Built-in literals
	for (const FString& B : UmkaBuiltins)
	{
		Rules.Emplace(FSyntaxTokenizer::FRule(B));
	}

	// Single-char operators (includes parens so identifiers and '(' tokenize separately)
	for (const TCHAR* Op : { TEXT("("), TEXT(")"),
	                         TEXT("+"), TEXT("-"), TEXT("*"), TEXT("/"), TEXT("%"),
	                         TEXT("&"), TEXT("|"), TEXT("^"), TEXT("~"), TEXT("<"),
	                         TEXT(">"), TEXT("="), TEXT("!"), TEXT("?"), TEXT(":") })
	{
		Rules.Emplace(FSyntaxTokenizer::FRule(Op));
	}

	return MakeShareable(new FUEmkaSyntaxHighlighter(FSyntaxTokenizer::Create(Rules)));
}

// -------------------------------------------------------------------------
// Constructor
// -------------------------------------------------------------------------

FUEmkaSyntaxHighlighter::FUEmkaSyntaxHighlighter(TSharedPtr<FSyntaxTokenizer> InTokenizer)
	: FSyntaxHighlighterTextLayoutMarshaller(MoveTemp(InTokenizer))
	, NormalStyle(MakeStyle(FLinearColor(0.85f, 0.85f, 0.85f)))
	, KeywordStyle(MakeStyle(FLinearColor(0.86f, 0.45f, 0.18f)))
	, TypeStyle(MakeStyle(FLinearColor(0.29f, 0.76f, 0.93f)))
	, BuiltinStyle(MakeStyle(FLinearColor(0.72f, 0.53f, 0.95f)))
	, CommentStyle(MakeStyle(FLinearColor(0.47f, 0.62f, 0.42f)))
	, StringStyle(MakeStyle(FLinearColor(0.82f, 0.70f, 0.47f)))
	, NumberStyle(MakeStyle(FLinearColor(0.88f, 0.72f, 0.40f)))
	, FunctionStyle(MakeStyle(FLinearColor(0.86f, 0.86f, 0.55f)))
	, OperatorStyle(MakeStyle(FLinearColor(0.78f, 0.78f, 0.78f)))
	, ErrorStyle(MakeStyle(FLinearColor(1.0f, 0.1f, 0.1f)))
{}

void FUEmkaSyntaxHighlighter::SetErrorLine(const int32 Line)
{
	if (ErrorLine != Line)
	{
		ErrorLine = Line;
		MakeDirty();
	}
}

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

bool FUEmkaSyntaxHighlighter::IsWordChar(const TCHAR C)
{
	return FChar::IsAlnum(C) || C == TEXT('_');
}

bool FUEmkaSyntaxHighlighter::IsAtWordBoundary(const FString& Source, const FSyntaxTokenizer::FToken& Token)
{
	const int32 Start = Token.Range.BeginIndex;
	const int32 End = Start + Token.Range.Len();
	const bool bPrevOk = (Start == 0) || !IsWordChar(Source[Start - 1]);
	const bool bNextOk = (End >= Source.Len()) || !IsWordChar(Source[End]);
	return bPrevOk && bNextOk;
}

// -------------------------------------------------------------------------
// Token classification
// -------------------------------------------------------------------------

void FUEmkaSyntaxHighlighter::ClassifyToken(const FString& Source, const FSyntaxTokenizer::FToken& Token, const FString& TokenText, EParserState& State, FRunInfo& OutInfo, FTextBlockStyle& OutStyle) const
{
	OutInfo.Name = TEXT("UEmka.Normal");
	OutStyle = NormalStyle;

	if (TokenText.IsEmpty()) return;

	// ---- Inside multi-line comment ----
	if (State == EParserState::InMultiLineComment)
	{
		OutInfo.Name = TEXT("UEmka.Comment");
		OutStyle = CommentStyle;
		if (TokenText == TEXT("*/")) State = EParserState::None;
		return;
	}

	// ---- Inside single-line comment ----
	if (State == EParserState::InSingleLineComment)
	{
		OutInfo.Name = TEXT("UEmka.Comment");
		OutStyle = CommentStyle;
		return;
	}

	// ---- Inside string ----
	if (State == EParserState::InString)
	{
		OutInfo.Name = TEXT("UEmka.String");
		OutStyle = StringStyle;
		if (TokenText == TEXT("\"")) State = EParserState::None;
		return;
	}

	// ---- Inside char literal ----
	if (State == EParserState::InChar)
	{
		OutInfo.Name = TEXT("UEmka.String");
		OutStyle = StringStyle;
		if (TokenText == TEXT("'")) State = EParserState::None;
		return;
	}

	// ---- Normal state: classify syntax tokens ----
	if (Token.Type == FSyntaxTokenizer::ETokenType::Syntax)
	{
		// Comment start
		if (TokenText == TEXT("//"))
		{
			State = EParserState::InSingleLineComment;
			OutInfo.Name = TEXT("UEmka.Comment");
			OutStyle = CommentStyle;
			return;
		}
		if (TokenText == TEXT("/*"))
		{
			State = EParserState::InMultiLineComment;
			OutInfo.Name = TEXT("UEmka.Comment");
			OutStyle = CommentStyle;
			return;
		}

		// String / char start
		if (TokenText == TEXT("\""))
		{
			State = EParserState::InString;
			OutInfo.Name = TEXT("UEmka.String");
			OutStyle = StringStyle;
			return;
		}
		if (TokenText == TEXT("'"))
		{
			State = EParserState::InChar;
			OutInfo.Name = TEXT("UEmka.String");
			OutStyle = StringStyle;
			return;
		}

		// Word-based tokens (keywords / types / builtins) - require word boundary
		if (FChar::IsAlpha(TokenText[0]) || TokenText[0] == TEXT('_'))
		{
			if (IsAtWordBoundary(Source, Token))
			{
				if (UmkaKeywords.Contains(TokenText))
				{
					OutInfo.Name = TEXT("UEmka.Keyword");
					OutStyle = KeywordStyle;
					return;
				}
				if (UmkaTypes.Contains(TokenText))
				{
					OutInfo.Name = TEXT("UEmka.Type");
					OutStyle = TypeStyle;
					return;
				}
				if (UmkaBuiltins.Contains(TokenText))
				{
					OutInfo.Name = TEXT("UEmka.Builtin");
					OutStyle = BuiltinStyle;
					return;
				}
			}
			return; // Part of a longer identifier - leave normal
		}

		// Operator / punctuation
		OutInfo.Name = TEXT("UEmka.Operator");
		OutStyle = OperatorStyle;
		return;
	}

	// ---- Literal token: identifier or number ----
	if (Token.Type == FSyntaxTokenizer::ETokenType::Literal)
	{
		if (FChar::IsDigit(TokenText[0]))
		{
			OutInfo.Name = TEXT("UEmka.Number");
			OutStyle = NumberStyle;
			return;
		}

		// Function call: pure word-boundary identifier whose next source character is '('
		if (FChar::IsAlpha(TokenText[0]) || TokenText[0] == TEXT('_'))
		{
			// TrimEnd handles the rare case of trailing whitespace before '(' (e.g. "foo (")
			const FString TrimmedText = TokenText.TrimEnd();
			bool bIsIdentifier = !TrimmedText.IsEmpty();
			for (int32 i = 0; i < TrimmedText.Len() && bIsIdentifier; ++i)
			{
				if (!IsWordChar(TrimmedText[i]))
				{
					bIsIdentifier = false;
				}
			}

			if (bIsIdentifier && IsAtWordBoundary(Source, Token))
			{
				const int32 NextPos = Token.Range.BeginIndex + Token.Range.Len();
				if (NextPos < Source.Len() && Source[NextPos] == TEXT('('))
				{
					OutInfo.Name = TEXT("UEmka.Function");
					OutStyle = FunctionStyle;
				}
			}
		}
	}
}

// -------------------------------------------------------------------------
// ParseTokens
// -------------------------------------------------------------------------

void FUEmkaSyntaxHighlighter::ParseTokens(const FString& Source, FTextLayout& Layout, TArray<FSyntaxTokenizer::FTokenizedLine> TokenizedLines)
{
	TArray<FTextLayout::FNewLineData> ParsedLines;
	ParsedLines.Reserve(TokenizedLines.Num());

	EParserState State = EParserState::None;
	int32 LineIndex = 0;

	for (const FSyntaxTokenizer::FTokenizedLine& Line : TokenizedLines)
	{
		// Single-line comments don't carry across lines
		if (State == EParserState::InSingleLineComment)
		{
			State = EParserState::None;
		}

		const bool bIsErrorLine = ErrorLine > 0 && LineIndex == ErrorLine - 1;

		TSharedRef<FString> LineText = MakeShareable(new FString());
		TArray<TSharedRef<IRun>> Runs;

		for (const FSyntaxTokenizer::FToken& Token : Line.Tokens)
		{
			const FString TokenText = Source.Mid(Token.Range.BeginIndex, Token.Range.Len());
			const FTextRange RunRange(LineText->Len(), LineText->Len() + TokenText.Len());
			LineText->Append(TokenText);

			FRunInfo RunInfo(TEXT("UEmka.Normal"));
			FTextBlockStyle Style = NormalStyle;
			ClassifyToken(Source, Token, TokenText, State, RunInfo, Style);

			if (bIsErrorLine && RunInfo.Name != TEXT("UEmka.Comment"))
			{
				Style = ErrorStyle;
			}

			Runs.Add(FSlateTextRun::Create(RunInfo, LineText, Style, RunRange));
		}

		ParsedLines.Emplace(MoveTemp(LineText), MoveTemp(Runs));
		++LineIndex;
	}

	Layout.AddLines(ParsedLines);
}
