// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "Framework/Text/SyntaxTokenizer.h"
#include "Framework/Text/SyntaxHighlighterTextLayoutMarshaller.h"
#include "Styling/SlateTypes.h"

// Syntax highlighter marshaller for Umka source code.
// Covers: keywords, built-in types, literals (true/false/nil), strings, numbers, comments, operators, function calls.
class UEMKAEDITOR_API FUEmkaSyntaxHighlighter : public FSyntaxHighlighterTextLayoutMarshaller
{
public:
	static TSharedRef<FUEmkaSyntaxHighlighter> Create();

	virtual ~FUEmkaSyntaxHighlighter() override = default;

	// Set the 1-based error line from the last Umka compile. Pass -1 to clear.
	void SetErrorLine(const int32 Line);

protected:
	explicit FUEmkaSyntaxHighlighter(TSharedPtr<FSyntaxTokenizer> InTokenizer);

	enum class EParserState : uint8
	{
		None,
		InSingleLineComment,
		InMultiLineComment,
		InString,
		InChar,
	};

	virtual void ParseTokens(const FString& Source, FTextLayout& Layout, TArray<FSyntaxTokenizer::FTokenizedLine> TokenizedLines) override;

private:
	static bool IsWordChar(const TCHAR C);

	static bool IsAtWordBoundary(const FString& Source, const FSyntaxTokenizer::FToken& Token);

	void ClassifyToken(const FString& Source, const FSyntaxTokenizer::FToken& Token, const FString& TokenText, EParserState& State, FRunInfo& OutInfo, FTextBlockStyle& OutStyle) const;

	int32 ErrorLine = -1;

	FTextBlockStyle NormalStyle;

	FTextBlockStyle KeywordStyle;

	FTextBlockStyle TypeStyle;

	FTextBlockStyle BuiltinStyle;

	FTextBlockStyle CommentStyle;

	FTextBlockStyle StringStyle;

	FTextBlockStyle NumberStyle;

	FTextBlockStyle FunctionStyle;

	FTextBlockStyle OperatorStyle;

	FTextBlockStyle ErrorStyle;
};
