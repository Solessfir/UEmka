// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "GraphEditorSettings.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MakeArray.h"
#include "KismetCompiler.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Internationalization/Regex.h"
#include "UEmkaFunctionLibrary.h"
#include "UEmkaRecordTypes.h"
#include "UObject/PropertyPortFlags.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(K2Node_UEmka)

#define LOCTEXT_NAMESPACE "K2Node_UEmka"

static const FName PIN_ReturnValue(TEXT("ReturnValue"));
static const FName PIN_Success(TEXT("Success"));
static const FName PIN_Error(TEXT("Error"));

// -------------------------------------------------------------------------
// Static helpers
// -------------------------------------------------------------------------

// Replaces comments and quoted literals with spaces while preserving string length and
// line breaks. Structural parsers can then safely use source indices without finding
// declarations inside comments/strings or counting delimiters contained by them.
static FString MakeUEmkaStructuralView(const FString& Source)
{
	enum class EState : uint8
	{
		Code,
		LineComment,
		BlockComment,
		String,
		RawString,
		Char,
	};

	FString View = Source;
	EState State = EState::Code;
	auto Mask = [&View](const int32 Index)
	{
		if (View[Index] != TEXT('\n') && View[Index] != TEXT('\r'))
		{
			View[Index] = TEXT(' ');
		}
	};

	for (int32 i = 0; i < View.Len(); ++i)
	{
		const TCHAR Ch = Source[i];
		const TCHAR Next = i + 1 < Source.Len() ? Source[i + 1] : TEXT('\0');

		switch (State)
		{
			case EState::Code:
				if (Ch == TEXT('/') && Next == TEXT('/'))
				{
					Mask(i);
					Mask(++i);
					State = EState::LineComment;
				}
				else if (Ch == TEXT('/') && Next == TEXT('*'))
				{
					Mask(i);
					Mask(++i);
					State = EState::BlockComment;
				}
				else if (Ch == TEXT('"'))
				{
					Mask(i);
					State = EState::String;
				}
				else if (Ch == TEXT('\''))
				{
					Mask(i);
					State = EState::Char;
				}
				else if (Ch == TEXT('`'))
				{
					Mask(i);
					State = EState::RawString;
				}
				break;

			case EState::RawString:
				Mask(i);
				if (Ch == TEXT('`'))
				{
					State = EState::Code;
				}
				break;

			case EState::LineComment:
				Mask(i);
				if (Ch == TEXT('\n') || Ch == TEXT('\r'))
				{
					State = EState::Code;
				}
				break;

			case EState::BlockComment:
				Mask(i);
				if (Ch == TEXT('*') && Next == TEXT('/'))
				{
					Mask(++i);
					State = EState::Code;
				}
				break;

			case EState::String:
			case EState::Char:
			{
				const EState QuotedState = State;
				Mask(i);
				if (Ch == TEXT('\\') && i + 1 < Source.Len())
				{
					Mask(++i);
				}
				else if ((QuotedState == EState::String && Ch == TEXT('"'))
					  || (QuotedState == EState::Char && Ch == TEXT('\'')))
				{
					State = EState::Code;
				}
				break;
			}
		}
	}

	return View;
}

static bool IsUEmkaIdentChar(const TCHAR Ch)
{
	return FChar::IsAlnum(Ch) || Ch == TEXT('_');
}

// Parses declared enum storage widths. Unspecified enum bases use Umka's default int (8 bytes).
static TMap<FString, int32> ParseUEmkaEnumByteSizes(const FString& Source, const FString& StructuralView, TMap<FString, FString>& BaseNames)
{
	TMap<FString, int32> Result;
	const int32 Len = StructuralView.Len();

	auto SkipWhitespace = [&StructuralView, Len](int32& Pos)
	{
		while (Pos < Len && FChar::IsWhitespace(StructuralView[Pos]))
		{
			++Pos;
		}
	};

	auto ReadIdent = [&Source, &StructuralView, Len, &SkipWhitespace](int32& Pos)
	{
		SkipWhitespace(Pos);
		const int32 Start = Pos;
		while (Pos < Len && IsUEmkaIdentChar(StructuralView[Pos]))
		{
			++Pos;
		}
		return Source.Mid(Start, Pos - Start);
	};

	auto ByteSizeForBase = [](const FString& Base)
	{
		if (Base == TEXT("int8") || Base == TEXT("uint8") || Base == TEXT("char") || Base == TEXT("bool")) return 1;
		if (Base == TEXT("int16") || Base == TEXT("uint16")) return 2;
		if (Base == TEXT("int32") || Base == TEXT("uint32")) return 4;
		return 8; // int, uint, or an invalid base that the Umka compiler will report
	};

	for (int32 Pos = 0; Pos + 4 <= Len; ++Pos)
	{
		if (StructuralView.Mid(Pos, 4) != TEXT("type")
			|| (Pos > 0 && IsUEmkaIdentChar(StructuralView[Pos - 1]))
			|| (Pos + 4 < Len && IsUEmkaIdentChar(StructuralView[Pos + 4])))
		{
			continue;
		}

		int32 P = Pos + 4;
		const FString TypeName = ReadIdent(P);
		if (TypeName.IsEmpty())
		{
			continue;
		}

		SkipWhitespace(P);
		if (P < Len && StructuralView[P] == TEXT('*'))
		{
			++P;
			SkipWhitespace(P);
		}
		if (P >= Len || StructuralView[P] != TEXT('='))
		{
			continue;
		}

		++P;
		const FString DeclarationKind = ReadIdent(P);
		if (DeclarationKind != TEXT("enum"))
		{
			continue;
		}

		int32 ByteSize = 8;
		FString BaseName = TEXT("int");
		SkipWhitespace(P);
		if (P < Len && StructuralView[P] == TEXT('('))
		{
			++P;
			BaseName = ReadIdent(P);
			ByteSize = ByteSizeForBase(BaseName);
		}
		Result.Add(TypeName, ByteSize);
		BaseNames.Add(TypeName, BaseName);
		Pos = P;
	}

	return Result;
}

TOptional<EUEmkaValueType> UK2Node_UEmka::ParseUmkaType(const FString& TypeName)
{
	if (TypeName == TEXT("int"))    return EUEmkaValueType::Int;
	if (TypeName == TEXT("int8"))   return EUEmkaValueType::Int8;
	if (TypeName == TEXT("int16"))  return EUEmkaValueType::Int16;
	if (TypeName == TEXT("int32"))  return EUEmkaValueType::Int32;
	if (TypeName == TEXT("uint8"))  return EUEmkaValueType::UInt8;
	if (TypeName == TEXT("uint16")) return EUEmkaValueType::UInt16;
	if (TypeName == TEXT("uint32")) return EUEmkaValueType::UInt32;
	if (TypeName == TEXT("uint"))   return EUEmkaValueType::UInt;
	if (TypeName == TEXT("bool"))   return EUEmkaValueType::Bool;
	if (TypeName == TEXT("char"))   return EUEmkaValueType::Char;
	if (TypeName == TEXT("real"))   return EUEmkaValueType::Real;
	if (TypeName == TEXT("real32")) return EUEmkaValueType::Real32;
	if (TypeName == TEXT("str"))    return EUEmkaValueType::Str;
	return {};
}

FEdGraphPinType UK2Node_UEmka::GetPinTypeFor(EUEmkaValueType ValueType)
{
	FEdGraphPinType PinType;
	switch (ValueType)
	{
		case EUEmkaValueType::Int:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
			break;
		case EUEmkaValueType::Int8:
		case EUEmkaValueType::Int16:
		case EUEmkaValueType::Int32:
		case EUEmkaValueType::UInt16:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			break;
		case EUEmkaValueType::UInt8:
		case EUEmkaValueType::Char:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			break;
		case EUEmkaValueType::UInt:
		case EUEmkaValueType::UInt32:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int64; // closest BP has to uint64
			break;
		case EUEmkaValueType::Bool:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			break;
		case EUEmkaValueType::Real:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			break;
		case EUEmkaValueType::Real32:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			break;
		case EUEmkaValueType::Str:
			PinType.PinCategory = UEdGraphSchema_K2::PC_String;
			break;
		case EUEmkaValueType::Enum:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			break;
		default:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	return PinType;
}

// -------------------------------------------------------------------------
// Struct declaration parser
// -------------------------------------------------------------------------

TArray<FUEmkaStructDef> UK2Node_UEmka::ParseStructDefs(const FString& InScript, const TMap<FString, int32>& EnumByteSizes)
{
	TArray<FUEmkaStructDef> Structs;
	const int32 Len = InScript.Len();

	// Pass 1: locate "type <Name> = struct { <body> }" declarations and capture name + body range
	struct FRawStruct { FString Name; FString Body; };
	TArray<FRawStruct> Raw;

	int32 Pos = 0;
	while (Pos < Len)
	{
		// Find "type" keyword at a word boundary
		if (Pos + 4 < Len
			&& InScript[Pos] == 't' && InScript[Pos + 1] == 'y' && InScript[Pos + 2] == 'p' && InScript[Pos + 3] == 'e'
			&& FChar::IsWhitespace(InScript[Pos + 4])
			&& (Pos == 0 || !(FChar::IsAlnum(InScript[Pos - 1]) || InScript[Pos - 1] == '_')))
		{
			int32 P = Pos + 4;
			auto SkipWs = [&]() { while (P < Len && FChar::IsWhitespace(InScript[P])) ++P; };

			SkipWs();
			const int32 NameStart = P;
			while (P < Len && (FChar::IsAlnum(InScript[P]) || InScript[P] == '_')) ++P;
			const FString Name = InScript.Mid(NameStart, P - NameStart);

			SkipWs();
			// Optional export marker on the type itself (type Vec2* = struct { ... })
			if (P < Len && InScript[P] == '*')
			{
				++P;
				SkipWs();
			}
			if (!Name.IsEmpty() && P < Len && InScript[P] == '=')
			{
				++P;
				SkipWs();
				// Require "struct" keyword followed by '{'
				if (P + 6 <= Len && InScript.Mid(P, 6) == TEXT("struct"))
				{
					P += 6;
					SkipWs();
					if (P < Len && InScript[P] == '{')
					{
						++P;
						const int32 BodyStart = P;
						int32 Depth = 1;
						while (P < Len && Depth > 0)
						{
							if (InScript[P] == '{') ++Depth;
							else if (InScript[P] == '}') --Depth;
							if (Depth > 0) ++P;
						}
						if (Depth == 0)
						{
							Raw.Add({Name, InScript.Mid(BodyStart, P - BodyStart)});
							Pos = P;
						}
					}
				}
			}
		}
		++Pos;
	}

	// Pass 2: parse fields, with the full struct name set known (catches nested struct references)
	for (const FRawStruct& R : Raw)
	{
		FUEmkaStructDef Def;
		Def.Name = R.Name;

		TArray<FString> Chunks;
		R.Body.Replace(TEXT(";"), TEXT("\n")).ParseIntoArray(Chunks, TEXT("\n"), true);
		for (FString Chunk : Chunks)
		{
			// Strip line comments
			int32 CommentIdx;
			if (Chunk.FindChar(TEXT('/'), CommentIdx) && Chunk.Mid(CommentIdx, 2) == TEXT("//"))
			{
				Chunk.LeftInline(CommentIdx);
			}
			Chunk.TrimStartAndEndInline();
			if (Chunk.IsEmpty())
			{
				continue;
			}

			int32 ColonIdx;
			if (!Chunk.FindChar(TEXT(':'), ColonIdx))
			{
				// Not a "names: type" field (e.g. stray token) - can't flatten this struct
				Def.bPinSafe = false;
				continue;
			}

			const FString TypeText = Chunk.Mid(ColonIdx + 1).TrimStartAndEnd();

			// Only plain primitive/str/declared-enum field types can cross the pin boundary.
			// Unknown identifiers include aliases and are deliberately not guessed to be enums.
			const bool bComplexType = TypeText.IsEmpty()
				|| TypeText.Contains(TEXT("["))
				|| TypeText.Contains(TEXT("^"))
				|| TypeText.Contains(TEXT("{"))
				|| TypeText.StartsWith(TEXT("map"))
				|| TypeText.StartsWith(TEXT("fn"))
				|| TypeText.StartsWith(TEXT("weak"))
				|| TypeText.StartsWith(TEXT("interface"))
				|| Raw.ContainsByPredicate([&TypeText](const FRawStruct& S) { return S.Name == TypeText; })
				|| (!ParseUmkaType(TypeText).IsSet() && !EnumByteSizes.Contains(TypeText));
			if (bComplexType)
			{
				Def.bPinSafe = false;
			}

			TArray<FString> Names;
			Chunk.Left(ColonIdx).ParseIntoArray(Names, TEXT(","), true);
			for (FString FieldName : Names)
			{
				FieldName.TrimStartAndEndInline();
				if (!FieldName.IsEmpty())
				{
					Def.Fields.Add({FieldName, TypeText});
				}
			}
		}

		if (Def.Fields.IsEmpty())
		{
			Def.bPinSafe = false;
		}
		Structs.Add(MoveTemp(Def));
	}

	return Structs;
}

// -------------------------------------------------------------------------
// Signature parser
// -------------------------------------------------------------------------

static FUEmkaPinDef MakeCompiledPin(const FUEmkaCompiledValue& Value, const FString& Name, const FString& FriendlyName = {})
{
	FUEmkaPinDef Pin;
	Pin.Name = Name;
	Pin.Type = Value.Type;
	Pin.bIsArray = Value.bIsArray;
	Pin.bIsStaticArray = Value.bIsStaticArray;
	Pin.EnumByteSize = Value.EnumByteSize;
	Pin.FriendlyName = FriendlyName;
	Pin.CompiledValue = Value;
	if (Value.bIsEnum)
	{
		Pin.EnumTypeName = Value.TypeName;
		if (!Pin.FriendlyName.IsEmpty())
		{
			Pin.FriendlyName += FString::Printf(TEXT(" (%s)"), *Value.TypeName);
		}
	}
	return Pin;
}

static FUEmkaSignature MakeCompiledSignature(const FString& FunctionName, const FUEmkaCompiledSignature& Compiled, const bool bNativeStructPins)
{
	FUEmkaSignature Sig;
	Sig.FunctionName = FunctionName;
	Sig.bValid = true;
	TFunction<FString(const FUEmkaCompiledValue&, const FString&, const FString&)> FlattenInput;
	FlattenInput = [&Sig, &FlattenInput, bNativeStructPins](const FUEmkaCompiledValue& Value, const FString& Name, const FString& FriendlyName)
	{
		if (Value.bIsStruct && !bNativeStructPins && Value.NativeStructName.IsEmpty())
		{
			Sig.bNeedsShim = true;
			TArray<FString> Initializers;
			for (const FUEmkaCompiledValue& Field : Value.Fields)
			{
				Initializers.Add(Field.Name + TEXT(": ") + FlattenInput(Field, Name + TEXT("_") + Field.Name, FriendlyName + TEXT(".") + Field.Name));
			}
			return Value.TypeName + TEXT("{") + FString::Join(Initializers, TEXT(", ")) + TEXT("}");
		}
		Sig.Params.Add(MakeCompiledPin(Value, Name, FriendlyName == Name ? FString() : FriendlyName));
		FUEmkaShimParam& Shim = Sig.ShimParams.AddDefaulted_GetRef();
		Shim.Name = Name;
		Shim.TypeText = Value.TypeName;
		return Name;
	};
	for (const FUEmkaCompiledValue& Param : Compiled.Params)
	{
		if (!Param.bSupported || (Param.bIsStruct && Param.Fields.IsEmpty()))
		{
			Sig.UnsupportedReason = FString::Printf(TEXT("parameter '%s' has a type that cannot cross Blueprint pins"), *Param.Name);
			break;
		}
		Sig.ShimCallArgs.Add(FlattenInput(Param, Param.Name, Param.Name));
	}

	const FUEmkaCompiledValue& Result = Compiled.Result;
	Sig.CompiledReturn = Result;
	if (!Result.bSupported || (Result.bIsStruct && Result.Fields.IsEmpty()))
	{
		Sig.UnsupportedReason = TEXT("return value has a type that cannot cross Blueprint pins");
	}
	else if (Result.bIsStruct || Result.bIsTuple)
	{
		TArray<FString> ReturnExpressions;
		TArray<FString> ReturnTypes;
		TFunction<void(const FUEmkaCompiledValue&, const FString&, const FString&, const FString&)> FlattenResult;
		FlattenResult = [&Sig, &ReturnExpressions, &ReturnTypes, &FlattenResult, bNativeStructPins](const FUEmkaCompiledValue& Value, const FString& Name, const FString& Friendly, const FString& Expression)
		{
			if (Value.bIsStruct && !bNativeStructPins && Value.NativeStructName.IsEmpty())
			{
				Sig.bNeedsShim = true;
				for (const FUEmkaCompiledValue& Field : Value.Fields)
				{
					FlattenResult(Field, Name.IsEmpty() ? Field.Name : Name + TEXT("_") + Field.Name,
						Friendly.IsEmpty() ? Field.Name : Friendly + TEXT(".") + Field.Name, Expression + TEXT(".") + Field.Name);
				}
				return;
			}
			Sig.ReturnParams.Add(MakeCompiledPin(Value, Name, Friendly));
			ReturnTypes.Add(Value.TypeName);
			ReturnExpressions.Add(Expression);
		};
		FString Assignment;
		TSet<FString> UsedNames;
		for (const FUEmkaPinDef& Pin : Sig.Params) UsedNames.Add(Pin.Name);
		auto MakeResultName = [&UsedNames](FString Name)
		{
			while (UsedNames.Contains(Name)) Name += TEXT("_");
			UsedNames.Add(Name);
			return Name;
		};
		if (Result.bIsStruct)
		{
			const FString Variable = MakeResultName(TEXT("__r"));
			FlattenResult(Result, TEXT(""), TEXT(""), Variable);
			Assignment = Variable + TEXT(" := ");
		}
		else
		{
			TArray<FString> Variables;
			for (int32 Index = 0; Index < Result.Fields.Num(); ++Index)
			{
				const FString Variable = MakeResultName(FString::Printf(TEXT("__r%d"), Index));
				Variables.Add(Variable);
				FlattenResult(Result.Fields[Index], FString::Printf(TEXT("item%d"), Index),
					Result.Fields[Index].bIsStruct ? FString::Printf(TEXT("ReturnValue%d"), Index + 1) : FString(), Variable);
			}
			Assignment = FString::Join(Variables, TEXT(", ")) + TEXT(" := ");
		}
		Sig.ReturnTypeText = ReturnTypes.Num() == 1 ? ReturnTypes[0] : TEXT("(") + FString::Join(ReturnTypes, TEXT(", ")) + TEXT(")");
		Sig.ShimBody = Assignment + TEXT("$CALL$\n    return ") + FString::Join(ReturnExpressions, TEXT(", "));
		if (Sig.ReturnParams.Num() == 1)
		{
			const FUEmkaPinDef& Pin = Sig.ReturnParams[0];
			Sig.ReturnType = Pin.Type;
			Sig.bReturnIsArray = Pin.bIsArray;
			Sig.bReturnIsStaticArray = Pin.bIsStaticArray;
			Sig.ReturnEnumTypeName = Pin.EnumTypeName;
			Sig.CompiledReturn = Pin.CompiledValue;
			Sig.ReturnParams.Empty();
		}
	}
	if (!Result.bIsStruct && !Result.bIsTuple && Result.Type != EUEmkaValueType::Void)
	{
		Sig.ReturnTypeText = Result.TypeName;
		Sig.ReturnType = Result.Type;
		Sig.bReturnIsArray = Result.bIsArray;
		Sig.bReturnIsStaticArray = Result.bIsStaticArray;
		Sig.ReturnEnumTypeName = Result.bIsEnum ? Result.TypeName : FString();
	}
	TSet<FName> PinNames;
	PinNames.Add(UEdGraphSchema_K2::PN_Execute);
	for (const FUEmkaPinDef& Pin : Sig.Params)
	{
		if (PinNames.Contains(FName(*Pin.Name))) Sig.UnsupportedReason = TEXT("flattened parameter names overlap");
		PinNames.Add(FName(*Pin.Name));
	}
	const bool bStructuredResult = Sig.ReturnParams.Num() >= 2 || Sig.bReturnIsArray || (Sig.ReturnType.IsSet() && Sig.ReturnType.GetValue() == EUEmkaValueType::Composite);
	if (Sig.bNeedsShim && Sig.Params.Num() > (bStructuredResult ? 14 : 15))
	{
		Sig.UnsupportedReason = TEXT("flattened parameters exceed Umka's function parameter limit");
	}
	if (!Sig.UnsupportedReason.IsEmpty())
	{
		const FString Reason = Sig.UnsupportedReason;
		Sig = {};
		Sig.FunctionName = FunctionName;
		Sig.bValid = true;
		Sig.UnsupportedReason = Reason;
	}
	return Sig;
}

TArray<FString> UK2Node_UEmka::GetExportedFunctions(const FString& Source)
{
	TArray<FString> Functions;
	const FString StructuralSource = MakeUEmkaStructuralView(Source);
	const FRegexPattern Pattern(TEXT("\\bfn\\s+([A-Za-z_][A-Za-z_0-9]*)\\s*\\*\\s*\\("));
	FRegexMatcher Matcher(Pattern, StructuralSource);
	while (Matcher.FindNext())
	{
		const FString Name = Matcher.GetCaptureGroup(1);
		if (!Functions.ContainsByPredicate([&Name](const FString& Existing) { return Existing.Equals(Name, ESearchCase::CaseSensitive); })) Functions.Add(Name);
	}
	return Functions;
}

FUEmkaSignature UK2Node_UEmka::ParseScript(const FString& InScript, const FString& SelectedFunction, const bool bNativeStructPins,
	const TArray<FUEmkaModuleSource>& Modules, const FString& FileName, const bool bAllowFileImports)
{
	FUEmkaSignature Sig;
	const FString StructuralScript = MakeUEmkaStructuralView(InScript);
	TMap<FString, FString> EnumBaseNames;
	const TMap<FString, int32> EnumByteSizes = ParseUEmkaEnumByteSizes(InScript, StructuralScript, EnumBaseNames);

	const TArray<FUEmkaStructDef> Structs = ParseStructDefs(StructuralScript, EnumByteSizes);
	auto FindStruct = [&Structs](const FString& TypeName) -> const FUEmkaStructDef*
	{
		return Structs.FindByPredicate([&TypeName](const FUEmkaStructDef& S) { return S.Name == TypeName; });
	};

	// Find first exported function: fn <name>*(...)
	int32 Pos = 0;
	const int32 Len = InScript.Len();

	auto SkipWhitespace = [&]()
	{
		while (Pos < Len && FChar::IsWhitespace(StructuralScript[Pos])) ++Pos;
	};

	auto ReadIdent = [&]() -> FString
	{
		SkipWhitespace();
		const int32 Start = Pos;
		while (Pos < Len && IsUEmkaIdentChar(StructuralScript[Pos])) ++Pos;
		return InScript.Mid(Start, Pos - Start);
	};

	// Scan for the first exported function. Private helpers are skipped, and the structural
	// view prevents "fn" text inside comments or quoted literals from being considered.
	FString FuncName;
	bool bFoundExportedFunction = false;
	while (Pos < Len)
	{
		if (Pos + 2 < Len
			&& StructuralScript[Pos] == 'f'
			&& StructuralScript[Pos + 1] == 'n'
			&& FChar::IsWhitespace(StructuralScript[Pos + 2]))
		{
			const bool bAtWordBoundary = Pos == 0 || !IsUEmkaIdentChar(StructuralScript[Pos - 1]);
			if (bAtWordBoundary)
			{
				const int32 CandidateStart = Pos;
				Pos += 2;
				const FString CandidateName = ReadIdent();
				SkipWhitespace();
				if (!CandidateName.IsEmpty() && Pos < Len && StructuralScript[Pos] == TEXT('*'))
				{
					++Pos;
					SkipWhitespace();
					if (Pos < Len && StructuralScript[Pos] == TEXT('(') && (SelectedFunction.IsEmpty() || SelectedFunction.Equals(CandidateName, ESearchCase::CaseSensitive)))
					{
						FuncName = CandidateName;
						++Pos;
						bFoundExportedFunction = true;
						break;
					}
				}
				Pos = CandidateStart + 2;
				continue;
			}
		}
		++Pos;
	}

	if (!bFoundExportedFunction)
	{
		if (!SelectedFunction.IsEmpty())
		{
			Sig.bValid = true;
			Sig.FunctionName = SelectedFunction;
			Sig.UnsupportedReason = FString::Printf(TEXT("exported function '%s' was not found"), *SelectedFunction);
		}
		return Sig;
	}

	// Use compiler-resolved types when available; the source parser keeps previews useful during edits.
	FUEmkaCompiledSignature Compiled;
	if (UUEmkaFunctionLibrary::InspectScriptFunction(InScript, FuncName, Compiled, Modules, FileName, bAllowFileImports))
	{
		return MakeCompiledSignature(FuncName, Compiled, bNativeStructPins);
	}

	// Collect everything inside the parameter parens (handle nested parens)
	const int32 ParamStart = Pos;
	int32 Depth = 1;
	while (Pos < Len && Depth > 0)
	{
		if (StructuralScript[Pos] == '(') ++Depth;
		else if (StructuralScript[Pos] == ')') --Depth;
		if (Depth > 0) ++Pos;
	}
	if (Depth != 0) return Sig; // unmatched parens

	const FString ParamStr = StructuralScript.Mid(ParamStart, Pos - ParamStart).TrimStartAndEnd();
	++Pos; // skip closing ')'

	// Parse parameter list: handles grouped "a, b: int, c: real"
	if (!ParamStr.IsEmpty())
	{
		TArray<FString> NameGroup;
		FString Token;

		auto FlushGroup = [&](const FString& TypeName, const bool bIsArray, const bool bIsStaticArray, const FString& ArrayPrefix)
		{
			const FString TrimmedTypeName = TypeName.TrimStartAndEnd();
			const TOptional<EUEmkaValueType> PrimitiveType = ParseUmkaType(TrimmedTypeName);
			const bool bIsDeclaredEnum = EnumByteSizes.Contains(TrimmedTypeName);
			const FUEmkaStructDef* StructDef = FindStruct(TrimmedTypeName);
			const bool bIsSupportedScalar = PrimitiveType.IsSet() || bIsDeclaredEnum;
			const EUEmkaValueType VType = PrimitiveType.IsSet() ? PrimitiveType.GetValue() : EUEmkaValueType::Enum;

			for (const FString& PName : NameGroup)
			{
				FString TrimmedName = PName.TrimStartAndEnd();
				if (TrimmedName.IsEmpty())
				{
					continue;
				}

				if (StructDef)
				{
					if (bIsArray)
					{
						Sig.UnsupportedReason = FString::Printf(TEXT("struct arrays ([]%s) cannot be passed as pins"), *TrimmedTypeName);
						continue;
					}
					if (!StructDef->bPinSafe)
					{
						Sig.UnsupportedReason = FString::Printf(TEXT("struct '%s' has fields that cannot be passed as pins (arrays, maps, nested structs, pointers)"), *TrimmedTypeName);
						continue;
					}

					// Flatten: one pin per field, named <param>_<field>, displayed as <param>.<field>
					FUEmkaShimParam& Shim = Sig.ShimParams.AddDefaulted_GetRef();
					Shim.Name = TrimmedName;
					Shim.StructName = StructDef->Name;
					Shim.Fields = StructDef->Fields;
					Sig.bNeedsShim = true;

					for (const FUEmkaStructField& Field : StructDef->Fields)
					{
						FUEmkaPinDef Def;
						Def.Name = FString::Printf(TEXT("%s_%s"), *TrimmedName, *Field.Name);
						const TOptional<EUEmkaValueType> FieldPrimitiveType = ParseUmkaType(Field.TypeText);
						Def.Type = FieldPrimitiveType.IsSet() ? FieldPrimitiveType.GetValue() : EUEmkaValueType::Enum;
						Def.FriendlyName = FString::Printf(TEXT("%s.%s"), *TrimmedName, *Field.Name);
						if (Def.Type == EUEmkaValueType::Enum)
						{
							Def.EnumTypeName = Field.TypeText;
							if (const int32* ByteSize = EnumByteSizes.Find(Field.TypeText))
							{
								Def.EnumByteSize = *ByteSize;
							}
							Def.FriendlyName += FString::Printf(TEXT(" (%s)"), *Field.TypeText);
						}
						Sig.Params.Add(Def);
					}
					continue;
				}

				if (!bIsSupportedScalar)
				{
					Sig.UnsupportedReason = FString::Printf(
						TEXT("parameter '%s' has unsupported type '%s'; only primitives, declared enums, supported structs, and arrays of supported scalars can be passed as pins"),
						*TrimmedName,
						*TrimmedTypeName);
					continue;
				}

				FUEmkaShimParam& Shim = Sig.ShimParams.AddDefaulted_GetRef();
				Shim.Name = TrimmedName;
				Shim.TypeText = ArrayPrefix + TrimmedTypeName;

				FUEmkaPinDef Def;
				Def.Name = TrimmedName;
				Def.Type = VType;
				Def.bIsArray = bIsArray;
				Def.bIsStaticArray = bIsStaticArray;
				if (VType == EUEmkaValueType::Enum)
				{
					Def.EnumTypeName = TrimmedTypeName;
					if (const int32* ByteSize = EnumByteSizes.Find(TrimmedTypeName))
					{
						Def.EnumByteSize = *ByteSize;
					}
				}
				Sig.Params.Add(Def);
			}
			NameGroup.Empty();
		};

		for (int32 i = 0; i < ParamStr.Len(); ++i)
		{
			const TCHAR Ch = ParamStr[i];
			if (Ch == ',')
			{
				FString Trimmed = Token.TrimStartAndEnd();
				if (!Trimmed.IsEmpty())
				{
					NameGroup.Add(Trimmed);
				}
				Token.Empty();
			}
			else if (Ch == ':')
			{
				FString Trimmed = Token.TrimStartAndEnd();
				if (!Trimmed.IsEmpty())
				{
					NameGroup.Add(Trimmed);
				}
				Token.Empty();

				++i;
				// Skip whitespace
				while (i < ParamStr.Len() && FChar::IsWhitespace(ParamStr[i])) ++i;

				// Blueprint represents both Umka array kinds as array pins, but fixed [N]T
				// values use different ABI storage and are tracked separately.
				bool bIsArray = false;
				bool bIsStaticArray = false;
				FString ArrayPrefix;
				if (i < ParamStr.Len() && ParamStr[i] == '[')
				{
					bIsArray = true;
					const int32 PrefixStart = i;
					++i;
					const int32 LengthExprStart = i;
					while (i < ParamStr.Len() && ParamStr[i] != ']') ++i;
					bIsStaticArray = !ParamStr.Mid(LengthExprStart, i - LengthExprStart).TrimStartAndEnd().IsEmpty();
					if (i < ParamStr.Len()) ++i; // skip ']'
					ArrayPrefix = ParamStr.Mid(PrefixStart, i - PrefixStart);
					while (i < ParamStr.Len() && FChar::IsWhitespace(ParamStr[i])) ++i;
				}

				// Collect type name up to next comma
				FString TypeName;
				while (i < ParamStr.Len() && ParamStr[i] != ',')
				{
					TypeName += ParamStr[i];
					++i;
				}
				FlushGroup(TypeName, bIsArray, bIsStaticArray, ArrayPrefix);
				--i; // outer loop will increment past the comma
			}
			else
			{
				Token += Ch;
			}
		}
		// Flush any leftover (shouldn't happen in valid Umka, but be safe)
		FString Leftover = Token.TrimStartAndEnd();
		if (!Leftover.IsEmpty())
		{
			NameGroup.Add(Leftover);
		}
	}

	// Capture raw return type text (between ')' and the body '{') for shim forwarding
	{
		int32 BodyBrace = Pos;
		while (BodyBrace < Len && StructuralScript[BodyBrace] != '{') ++BodyBrace;
		FString RetText = InScript.Mid(Pos, BodyBrace - Pos).TrimStartAndEnd();
		if (RetText.StartsWith(TEXT(":")))
		{
			RetText.RightChopInline(1);
			RetText.TrimStartAndEndInline();
		}
		Sig.ReturnTypeText = RetText;
	}

	// Parse return type after ')'
	SkipWhitespace();
	if (Pos < Len && StructuralScript[Pos] == ':')
	{
		++Pos;
		SkipWhitespace();

		if (Pos < Len && StructuralScript[Pos] == '(')
		{
			++Pos; // skip '('
			int32 RetIdx = 0;
			while (Pos < Len && StructuralScript[Pos] != ')')
			{
				SkipWhitespace();
				if (Pos >= Len || StructuralScript[Pos] == ')') break;

				bool bIsArray = false;
				bool bIsStaticArray = false;
				if (StructuralScript[Pos] == '[')
				{
					bIsArray = true;
					++Pos;
					const int32 LengthExprStart = Pos;
					while (Pos < Len && StructuralScript[Pos] != ']') ++Pos;
					bIsStaticArray = !StructuralScript.Mid(LengthExprStart, Pos - LengthExprStart).TrimStartAndEnd().IsEmpty();
					if (Pos < Len) ++Pos; // skip ']'
					SkipWhitespace();
				}

				const FString TypeName = ReadIdent();
				if (!TypeName.IsEmpty() && TypeName != TEXT("void"))
				{
					const TOptional<EUEmkaValueType> PrimitiveType = ParseUmkaType(TypeName);
					const bool bIsDeclaredEnum = EnumByteSizes.Contains(TypeName);
					if (FindStruct(TypeName))
					{
						Sig.UnsupportedReason = FString::Printf(TEXT("struct '%s' inside a multi-return tuple cannot be passed as pins"), *TypeName);
					}
					else if (!PrimitiveType.IsSet() && !bIsDeclaredEnum)
					{
						Sig.UnsupportedReason = FString::Printf(
							TEXT("multi-return item has unsupported type '%s'; only primitives, declared enums, and arrays of supported scalars can be returned as pins"),
							*TypeName);
					}
					else
					{
						FUEmkaPinDef Def;
						Def.Name = FString::Printf(TEXT("item%d"), RetIdx++);
						Def.Type = PrimitiveType.IsSet() ? PrimitiveType.GetValue() : EUEmkaValueType::Enum;
						Def.bIsArray = bIsArray;
						Def.bIsStaticArray = bIsStaticArray;
						if (Def.Type == EUEmkaValueType::Enum)
						{
							Def.EnumTypeName = TypeName;
							if (const int32* ByteSize = EnumByteSizes.Find(TypeName))
							{
								Def.EnumByteSize = *ByteSize;
							}
						}
						Sig.ReturnParams.Add(Def);
					}
				}
				else if (TypeName.IsEmpty())
				{
					Sig.UnsupportedReason = TEXT("multi-return item has an unsupported non-scalar type");
					break;
				}
				SkipWhitespace();
				if (Pos < Len && StructuralScript[Pos] == ',') ++Pos;
			}
			if (Pos < Len && StructuralScript[Pos] == ')') ++Pos;
		}
		else
		{
			// Track []T and [N]T separately; both are represented by Blueprint array pins.
			if (Pos < Len && StructuralScript[Pos] == '[')
			{
				Sig.bReturnIsArray = true;
				++Pos;
				const int32 LengthExprStart = Pos;
				while (Pos < Len && StructuralScript[Pos] != ']') ++Pos;
				Sig.bReturnIsStaticArray = !StructuralScript.Mid(LengthExprStart, Pos - LengthExprStart).TrimStartAndEnd().IsEmpty();
				if (Pos < Len) ++Pos; // skip ']'
				SkipWhitespace();
			}

			const FString RetTypeName = ReadIdent();
			if (!RetTypeName.IsEmpty() && RetTypeName != TEXT("void"))
			{
				const FUEmkaStructDef* StructDef = FindStruct(RetTypeName);
				const TOptional<EUEmkaValueType> PrimitiveType = ParseUmkaType(RetTypeName);
				const bool bIsDeclaredEnum = EnumByteSizes.Contains(RetTypeName);
				if (StructDef && Sig.bReturnIsArray)
				{
					Sig.UnsupportedReason = FString::Printf(TEXT("struct arrays ([]%s) cannot be returned as pins"), *RetTypeName);
				}
				else if (StructDef && !StructDef->bPinSafe)
				{
					Sig.UnsupportedReason = FString::Printf(TEXT("struct '%s' has fields that cannot be passed as pins (arrays, maps, nested structs, pointers)"), *RetTypeName);
				}
				else if (StructDef)
				{
					// Flatten the struct return: one output pin per field, displayed by field name
					Sig.ReturnStructName = StructDef->Name;
					Sig.ReturnFields = StructDef->Fields;
					Sig.bNeedsShim = true;

					for (const FUEmkaStructField& Field : StructDef->Fields)
					{
						FUEmkaPinDef Def;
						Def.Name = Field.Name;
						const TOptional<EUEmkaValueType> FieldPrimitiveType = ParseUmkaType(Field.TypeText);
						Def.Type = FieldPrimitiveType.IsSet() ? FieldPrimitiveType.GetValue() : EUEmkaValueType::Enum;
						Def.FriendlyName = Field.Name;
						if (Def.Type == EUEmkaValueType::Enum)
						{
							Def.EnumTypeName = Field.TypeText;
							if (const int32* ByteSize = EnumByteSizes.Find(Field.TypeText))
							{
								Def.EnumByteSize = *ByteSize;
							}
							Def.FriendlyName += FString::Printf(TEXT(" (%s)"), *Field.TypeText);
						}
						Sig.ReturnParams.Add(Def);
					}
				}
				else if (PrimitiveType.IsSet() || bIsDeclaredEnum)
				{
					Sig.ReturnType = PrimitiveType.IsSet() ? PrimitiveType.GetValue() : EUEmkaValueType::Enum;
					if (Sig.ReturnType.GetValue() == EUEmkaValueType::Enum)
					{
						Sig.ReturnEnumTypeName = RetTypeName;
					}
				}
				else
				{
					Sig.UnsupportedReason = FString::Printf(
						TEXT("return value has unsupported type '%s'; only primitives, declared enums, supported structs, and arrays of supported scalars can be returned as pins"),
						*RetTypeName);
				}
			}
			else if (RetTypeName.IsEmpty())
			{
				Sig.UnsupportedReason = TEXT("return value has an unsupported non-scalar type");
			}
		}

		// Single-element tuple or single-field struct is just a single return - unwrap it
		if (Sig.ReturnParams.Num() == 1)
		{
			Sig.ReturnType = Sig.ReturnParams[0].Type;
			Sig.bReturnIsArray = Sig.ReturnParams[0].bIsArray;
			Sig.bReturnIsStaticArray = Sig.ReturnParams[0].bIsStaticArray;
			Sig.ReturnEnumTypeName = Sig.ReturnParams[0].EnumTypeName;
			Sig.ReturnParams.Empty();
		}
	}

	Sig.FunctionName = FuncName;
	Sig.bValid = true;

	// Keep declared enum pins stable while a function body is incomplete.
	auto ResolveEnumBase = [&EnumBaseNames](const FString& Name)
	{
		return ParseUmkaType(EnumBaseNames.FindRef(Name)).Get(EUEmkaValueType::Int);
	};
	for (FUEmkaPinDef& Param : Sig.Params)
	{
		if (Param.Type == EUEmkaValueType::Enum) Param.Type = ResolveEnumBase(Param.EnumTypeName);
	}
	for (FUEmkaPinDef& Result : Sig.ReturnParams)
	{
		if (Result.Type == EUEmkaValueType::Enum) Result.Type = ResolveEnumBase(Result.EnumTypeName);
	}
	if (Sig.ReturnType.IsSet() && Sig.ReturnType.GetValue() == EUEmkaValueType::Enum)
	{
		Sig.ReturnType = ResolveEnumBase(Sig.ReturnEnumTypeName);
	}

	// Unsupported constructs: keep the signature valid (function exists) but expose no data
	// pins - ValidateNodeDuringCompilation reports UnsupportedReason as a compile error.
	if (!Sig.UnsupportedReason.IsEmpty())
	{
		Sig.Params.Empty();
		Sig.ReturnParams.Empty();
		Sig.ReturnType.Reset();
		Sig.bReturnIsArray = false;
		Sig.bReturnIsStaticArray = false;
		Sig.ReturnEnumTypeName.Empty();
		Sig.ShimParams.Empty();
		Sig.ReturnStructName.Empty();
		Sig.ReturnFields.Empty();
		Sig.bNeedsShim = false;
	}

	return Sig;
}

// -------------------------------------------------------------------------
// Shim codegen - wraps struct-using functions in a flat-signature caller
// -------------------------------------------------------------------------

FString UK2Node_UEmka::BuildShimFunction(const FUEmkaSignature& Sig)
{
	// Shim parameters: structs flattened to <param>_<field>, everything else passed through
	TArray<FString> ParamDecls;
	TArray<FString> CallArgs;
	for (const FUEmkaShimParam& Param : Sig.ShimParams)
	{
		if (Param.StructName.IsEmpty())
		{
			ParamDecls.Add(FString::Printf(TEXT("%s: %s"), *Param.Name, *Param.TypeText));
			CallArgs.Add(Param.Name);
		}
		else
		{
			TArray<FString> FieldInits;
			for (const FUEmkaStructField& Field : Param.Fields)
			{
				ParamDecls.Add(FString::Printf(TEXT("%s_%s: %s"), *Param.Name, *Field.Name, *Field.TypeText));
				FieldInits.Add(FString::Printf(TEXT("%s: %s_%s"), *Field.Name, *Param.Name, *Field.Name));
			}
			CallArgs.Add(FString::Printf(TEXT("%s{%s}"), *Param.StructName, *FString::Join(FieldInits, TEXT(", "))));
		}
	}

	if (!Sig.ShimCallArgs.IsEmpty()) CallArgs = Sig.ShimCallArgs;
	const FString Call = FString::Printf(TEXT("%s(%s)"), *Sig.FunctionName, *FString::Join(CallArgs, TEXT(", ")));

	FString RetDecl;
	FString Body;
	if (!Sig.ShimBody.IsEmpty())
	{
		RetDecl = FString::Printf(TEXT(": %s"), *Sig.ReturnTypeText);
		Body = TEXT("    ") + Sig.ShimBody.Replace(TEXT("$CALL$"), *Call);
	}
	else if (!Sig.ReturnStructName.IsEmpty())
	{
		// Struct return: call into a local, then return its fields as a tuple
		TArray<FString> RetTypes;
		TArray<FString> RetFields;
		for (const FUEmkaStructField& Field : Sig.ReturnFields)
		{
			RetTypes.Add(Field.TypeText);
			RetFields.Add(FString::Printf(TEXT("__r.%s"), *Field.Name));
		}
		RetDecl = (RetTypes.Num() == 1)
			? FString::Printf(TEXT(": %s"), *RetTypes[0])
			: FString::Printf(TEXT(": (%s)"), *FString::Join(RetTypes, TEXT(", ")));
		Body = FString::Printf(TEXT("    __r := %s\n    return %s"), *Call, *FString::Join(RetFields, TEXT(", ")));
	}
	else if (!Sig.ReturnTypeText.IsEmpty())
	{
		// Non-struct return (scalar, array, or tuple): forward it verbatim
		RetDecl = FString::Printf(TEXT(": %s"), *Sig.ReturnTypeText);
		Body = FString::Printf(TEXT("    return %s"), *Call);
	}
	else
	{
		Body = FString::Printf(TEXT("    %s"), *Call);
	}

	return FString::Printf(TEXT("fn __uemka_call*(%s)%s {\n%s\n}"), *FString::Join(ParamDecls, TEXT(", ")), *RetDecl, *Body);
}

FString UK2Node_UEmka::GetEffectiveScript(const FString& InScript, const FUEmkaSignature& Sig)
{
	if (!Sig.bValid || !Sig.bNeedsShim)
	{
		return InScript;
	}
	return InScript + TEXT("\n\n") + BuildShimFunction(Sig);
}

FString UK2Node_UEmka::GetEffectiveFunctionName(const FUEmkaSignature& Sig)
{
	return Sig.bNeedsShim ? TEXT("__uemka_call") : Sig.FunctionName;
}

// -------------------------------------------------------------------------
// K2Node overrides
// -------------------------------------------------------------------------

void UK2Node_UEmka::PostLoad()
{
	PreloadRequiredAssets();
	Super::PostLoad();
	ParsedSignature = ParseCurrentScript();
	CompileCurrentScript(GetEffectiveScript(GetScriptSource(), ParsedSignature), LastErrorMessage, LastErrorLine);
}

void UK2Node_UEmka::PreloadRequiredAssets()
{
	TArray<UUEmkaScriptAsset*> Pending;
	if (ScriptAsset) Pending.Add(ScriptAsset);
	TSet<UUEmkaScriptAsset*> Visited;
	while (!Pending.IsEmpty())
	{
		UUEmkaScriptAsset* Asset = Pending.Pop();
		if (!Asset || Visited.Contains(Asset)) continue;
		Visited.Add(Asset);
		PreloadObject(Asset);
		for (UUEmkaScriptAsset* Import : Asset->Imports) Pending.Add(Import);
	}
	Super::PreloadRequiredAssets();
}

void UK2Node_UEmka::PostEditUndo()
{
	// Script has been restored by the transaction system - re-derive everything that isn't a UPROPERTY.
	ParsedSignature = ParseCurrentScript();
	if (CompileCurrentScript(GetEffectiveScript(GetScriptSource(), ParsedSignature), LastErrorMessage, LastErrorLine))
	{
		LastErrorLine = -1;
		LastErrorMessage.Empty();
	}

	// Super resolves the pin references restored by the transaction.
	Super::PostEditUndo();
}

void UK2Node_UEmka::AllocateDefaultPins()
{
	// Always re-parse here so pins are correct regardless of how AllocateDefaultPins is invoked
	// (e.g., during Blueprint compiler's internal reconstruction where ParsedSignature may not be pre-populated)
	ParsedSignature = ParseCurrentScript();

	// Exec in/out
	CreatePin(EGPD_Input,  UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	if (bExposeRuntimeStatus)
	{
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, GetRuntimeStatusPinName(PIN_Success));
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, GetRuntimeStatusPinName(PIN_Error));
	}
	TArray<FEdGraphPinType> InputTypes;
	TArray<FEdGraphPinType> OutputTypes;
	TArray<FString> InputDefaults;
	auto ResolveType = [this](const FUEmkaCompiledValue& Value, const EUEmkaValueType Type, const bool bIsArray)
	{
		FString TypeError;
		FEdGraphPinType PinType = Value.bSupported ? UEmkaRecordTypes::GetPinType(Value, this, TypeError) : GetPinTypeFor(Type);
		if (!TypeError.IsEmpty()) ParsedSignature.UnsupportedReason = TypeError;
		if (bIsArray && !Value.bSupported) PinType.ContainerType = EPinContainerType::Array;
		return PinType;
	};
	for (const FUEmkaPinDef& Param : ParsedSignature.Params)
	{
		const FEdGraphPinType Type = ResolveType(Param.CompiledValue, Param.Type, Param.bIsArray);
		InputTypes.Add(Type);
		FString Default = Param.CompiledValue.DefaultValue;
		if (Param.CompiledValue.bIsStruct && Param.CompiledValue.bHasDefault && ParsedSignature.UnsupportedReason.IsEmpty())
		{
			FString Error;
			if (!UEmkaRecordTypes::GetDefaultValue(Param.CompiledValue, Type, Default, Error)) ParsedSignature.UnsupportedReason = Error;
		}
		InputDefaults.Add(Default);
	}
	for (const FUEmkaPinDef& Result : ParsedSignature.ReturnParams)
	{
		OutputTypes.Add(ResolveType(Result.CompiledValue, Result.Type, Result.bIsArray));
	}
	if (ParsedSignature.ReturnType.IsSet())
	{
		OutputTypes.Add(ResolveType(ParsedSignature.CompiledReturn, ParsedSignature.ReturnType.GetValue(), ParsedSignature.bReturnIsArray));
	}
	if (!ParsedSignature.UnsupportedReason.IsEmpty())
	{
		Super::AllocateDefaultPins();
		return;
	}

	// Typed input pins from parsed signature
	for (int32 Index = 0; Index < ParsedSignature.Params.Num(); ++Index)
	{
		const FUEmkaPinDef& Param = ParsedSignature.Params[Index];
		const FEdGraphPinType& PinType = InputTypes[Index];
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, PinType.PinCategory, FName(*Param.Name));
		NewPin->PinType = PinType;
		if (Param.CompiledValue.bHasDefault && (Param.CompiledValue.DefaultCompositeValue.IsEmpty() || Param.CompiledValue.bIsStruct))
		{
			GetDefault<UEdGraphSchema_K2>()->SetPinAutogeneratedDefaultValue(NewPin, InputDefaults[Index]);
		}
		if (!Param.FriendlyName.IsEmpty())
		{
			NewPin->PinFriendlyName = FText::FromString(Param.FriendlyName);
		}
		else if (!Param.EnumTypeName.IsEmpty())
		{
			NewPin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("%s (%s)"), *Param.Name, *Param.EnumTypeName));
		}
	}

	// Typed output pins for return values
	if (ParsedSignature.bValid && ParsedSignature.ReturnParams.Num() >= 2)
	{
		// Multi-return: one output pin per value (item0, item1, ...)
		for (int32 i = 0; i < ParsedSignature.ReturnParams.Num(); ++i)
		{
			const FUEmkaPinDef& Def = ParsedSignature.ReturnParams[i];
			const FEdGraphPinType& PinType = OutputTypes[i];
			FName PinName = *FString::Printf(TEXT("ReturnValue%d"), i + 1);
			UEdGraphPin* RetPin = CreatePin(EGPD_Output, PinType.PinCategory, PinName);
			RetPin->PinType = PinType;
			if (!Def.FriendlyName.IsEmpty())
			{
				RetPin->PinFriendlyName = FText::FromString(Def.FriendlyName);
			}
			else if (!Def.EnumTypeName.IsEmpty())
			{
				RetPin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("ReturnValue%d (%s)"), i + 1, *Def.EnumTypeName));
			}
		}
	}
	else if (ParsedSignature.bValid && ParsedSignature.ReturnType.IsSet())
	{
		const FEdGraphPinType& RetPinType = OutputTypes[0];
		UEdGraphPin* RetPin = CreatePin(EGPD_Output, RetPinType.PinCategory, PIN_ReturnValue);
		RetPin->PinType = RetPinType;
		if (!ParsedSignature.ReturnEnumTypeName.IsEmpty())
		{
			RetPin->PinFriendlyName = FText::FromString(ParsedSignature.ReturnEnumTypeName);
		}
	}

	Super::AllocateDefaultPins();
}

FName UK2Node_UEmka::GetRuntimeStatusPinName(const FName Name) const
{
	TSet<FName> ReturnNames = {UEdGraphSchema_K2::PN_Then, PIN_ReturnValue};
	for (int32 Index = 0; Index < ParsedSignature.ReturnParams.Num(); ++Index)
	{
		const FUEmkaPinDef& Result = ParsedSignature.ReturnParams[Index];
		ReturnNames.Add(FName(*Result.Name));
		ReturnNames.Add(FName(*Result.FriendlyName));
		ReturnNames.Add(FName(*FString::Printf(TEXT("ReturnValue%d"), Index + 1)));
	}
	if (!ParsedSignature.ReturnEnumTypeName.IsEmpty()) ReturnNames.Add(FName(*ParsedSignature.ReturnEnumTypeName));
	if (!ReturnNames.Contains(Name)) return Name;
	const FString Base = TEXT("Runtime") + Name.ToString();
	FName Candidate(*Base);
	for (int32 Index = 1; ReturnNames.Contains(Candidate); ++Index)
	{
		Candidate = FName(*(Base + FString::FromInt(Index)));
	}
	return Candidate;
}

FText UK2Node_UEmka::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("NodeMenuTitle", "Umka Script");
	}
	if (ParsedSignature.bValid && !ParsedSignature.FunctionName.IsEmpty())
	{
		return FText::FromString(FString::Printf(TEXT("Umka: %s"), *ParsedSignature.FunctionName));
	}
	return LOCTEXT("NodeTitle", "Umka Script");
}

FText UK2Node_UEmka::GetTooltipText() const
{
	if (!LastErrorMessage.IsEmpty())
	{
		return FText::FromString(LastErrorMessage);
	}
	return LOCTEXT("NodeTooltip", "Execute an inline Umka script. Write an exported function (fn name*(...)) to define the node's pins.");
}

FLinearColor UK2Node_UEmka::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->PureFunctionCallNodeTitleColor;
}

FSlateIcon UK2Node_UEmka::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	static const FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

FText UK2Node_UEmka::GetMenuCategory() const
{
	return LOCTEXT("NodeCategory", "UEmka");
}

void UK2Node_UEmka::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_UEmka::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
	if (ExecutionOptions.MaxInstructions < 0)
	{
		MessageLog.Error(*LOCTEXT("InvalidInstructionLimit", "UEmka node: Max Instructions must be zero or greater @@").ToString(), this);
	}

	if (GetScriptSource().IsEmpty() && !ScriptAsset)
	{
		MessageLog.Warning(*LOCTEXT("EmptyScript", "UEmka node has empty script: @@").ToString(), this);
		return;
	}

	if (!ParsedSignature.bValid)
	{
		MessageLog.Error(*LOCTEXT("InvalidSignature", "UEmka node: no exported function found in script (expected 'fn name*(...)') @@").ToString(), this);
		return;
	}

	if (!ParsedSignature.UnsupportedReason.IsEmpty())
	{
		MessageLog.Error(*FText::Format(LOCTEXT("UnsupportedSignature", "UEmka node: {0} @@"), FText::FromString(ParsedSignature.UnsupportedReason)).ToString(), this);
		return;
	}

	// Run a full Umka compile (including any generated shim) to catch type errors, undefined symbols, etc.
	FString CompileError;
	int32 ErrorLine = -1;
	if (!CompileCurrentScript(GetEffectiveScript(GetScriptSource(), ParsedSignature), CompileError, ErrorLine))
	{
		LastErrorLine = ErrorLine;
		LastErrorMessage = CompileError;
		MessageLog.Error(*FText::Format(LOCTEXT("UmkaCompileError", "Umka script error - {0} @@"), FText::FromString(CompileError)).ToString(), this);
	}
	else
	{
		LastErrorLine = -1;
		LastErrorMessage.Empty();
	}
}

void UK2Node_UEmka::OnScriptChanged(const FString& NewScript)
{
	if (!ScriptAsset) Script = NewScript;
	RefreshScript();
}

FString UK2Node_UEmka::GetScriptSource() const
{
	if (ScriptAsset) ScriptAsset->ConditionalPreload();
	return ScriptAsset ? ScriptAsset->Source : Script;
}

bool UK2Node_UEmka::ResolveCurrentSource(FString& OutSource, FString& OutFileName, TArray<FUEmkaModuleSource>& OutModules, FString& OutError) const
{
	if (ScriptAsset) return UUEmkaScriptAsset::ResolveAsset(ScriptAsset, OutSource, OutFileName, OutModules, OutError);
	OutSource = Script;
	OutFileName = TEXT("script.um");
	OutModules.Reset();
	OutError.Reset();
	return true;
}

FUEmkaSignature UK2Node_UEmka::ParseCurrentScript() const
{
	FString Source, FileName, Error;
	TArray<FUEmkaModuleSource> Modules;
	if (!ResolveCurrentSource(Source, FileName, Modules, Error))
	{
		FUEmkaSignature Signature;
		Signature.bValid = true;
		Signature.FunctionName = SelectedFunction;
		Signature.UnsupportedReason = Error;
		return Signature;
	}
	return ParseScript(Source, SelectedFunction, bNativeStructPins, Modules, FileName, !ScriptAsset);
}

bool UK2Node_UEmka::CompileCurrentScript(const FString& Source, FString& OutError, int32& OutLine) const
{
	FString ResolvedSource, FileName;
	TArray<FUEmkaModuleSource> Modules;
	OutLine = -1;
	if (!ResolveCurrentSource(ResolvedSource, FileName, Modules, OutError)) return false;
	return UUEmkaFunctionLibrary::CompileCheckScript(Source, OutError, OutLine, Modules, FileName, !ScriptAsset);
}

void UK2Node_UEmka::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, ScriptAsset)
		|| Name == GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, SelectedFunction)
		|| Name == GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, bNativeStructPins)
		|| Name == GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, ExecutionOptions)
		|| Name == GET_MEMBER_NAME_CHECKED(UK2Node_UEmka, bExposeRuntimeStatus)) RefreshScript();
}

void UK2Node_UEmka::OnScriptAssetChanged(UUEmkaScriptAsset* ChangedAsset)
{
	TArray<const UUEmkaScriptAsset*> Pending;
	if (ScriptAsset) Pending.Add(ScriptAsset);
	TSet<const UUEmkaScriptAsset*> Visited;
	while (!Pending.IsEmpty())
	{
		const UUEmkaScriptAsset* Asset = Pending.Pop(EAllowShrinking::No);
		if (!Asset || Visited.Contains(Asset)) continue;
		Visited.Add(Asset);
		if (Asset == ChangedAsset)
		{
			Modify();
			RefreshScript();
			return;
		}
		for (const UUEmkaScriptAsset* Dependency : Asset->Imports) Pending.Add(Dependency);
	}
}

void UK2Node_UEmka::RefreshScript()
{
	const FUEmkaSignature NewSig = ParseCurrentScript();

	// Live compile check - drives squiggly line highlighting in SGraphNode_UEmka.
	// Checks the effective script so generated shim errors surface immediately.
	if (CompileCurrentScript(GetEffectiveScript(GetScriptSource(), NewSig), LastErrorMessage, LastErrorLine))
	{
		LastErrorLine = -1;
		LastErrorMessage.Empty();
	}

	if (NewSig != ParsedSignature || bExposeRuntimeStatus != (FindPin(GetRuntimeStatusPinName(PIN_Success), EGPD_Output) != nullptr))
	{
		ParsedSignature = NewSig;
		ReconstructNode();

		// Remove orphaned pins left from the old signature - prevents phantom red pins when replacing a function
		bool bRemovedOrphans = false;
		for (int32 i = Pins.Num() - 1; i >= 0; --i)
		{
			if (Pins[i]->bOrphanedPin)
			{
				Pins[i]->BreakAllPinLinks();
				Pins.RemoveAt(i);
				bRemovedOrphans = true;
			}
		}

		if (bRemovedOrphans)
		{
			GetGraph()->NotifyGraphChanged();
		}
	}
	else
	{
		ParsedSignature = NewSig;
	}

	if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
	if (GetGraph()) GetGraph()->NotifyGraphChanged();
}

// -------------------------------------------------------------------------
// ExpandNode helpers
// -------------------------------------------------------------------------

// Selects the Make*Param helper matching a pin's type - mirror of GetGetResultFuncName.
static FName GetMakeParamFuncName(const EUEmkaValueType Type, const bool bIsArray)
{
	if (bIsArray)
	{
		if (Type == EUEmkaValueType::Bool) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeBoolArrayParam);
		if (Type == EUEmkaValueType::Real)								   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeRealArrayParam);
		if (Type == EUEmkaValueType::Real32)							   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeReal32ArrayParam);
		if (Type == EUEmkaValueType::Str)								   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeStrArrayParam);
		if (Type == EUEmkaValueType::Int || Type == EUEmkaValueType::UInt || Type == EUEmkaValueType::UInt32) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeInt64ArrayParam);
		if (Type == EUEmkaValueType::UInt8
		 || Type == EUEmkaValueType::Char
		 || Type == EUEmkaValueType::Enum)								   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeByteArrayParam);
		return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeIntArrayParam);
	}

	if (Type == EUEmkaValueType::Real)   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeRealParam);
	if (Type == EUEmkaValueType::Real32) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeReal32Param);
	if (Type == EUEmkaValueType::Str)    return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeStrParam);
	if (Type == EUEmkaValueType::Bool)   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeBoolParam);
	return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeIntParam);
}

static FName GetGetResultFuncName(const EUEmkaValueType RetType, const bool bIsArray)
{
	if (bIsArray)
	{
		if (RetType == EUEmkaValueType::Bool) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetBoolArrayResult);
		if (RetType == EUEmkaValueType::Real)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetRealArrayResult);
		if (RetType == EUEmkaValueType::Real32)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetReal32ArrayResult);
		if (RetType == EUEmkaValueType::Str)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetStrArrayResult);
		if (RetType == EUEmkaValueType::Int || RetType == EUEmkaValueType::UInt || RetType == EUEmkaValueType::UInt32) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetIntArrayResult);
		if (RetType == EUEmkaValueType::UInt8
		 || RetType == EUEmkaValueType::Char
		 || RetType == EUEmkaValueType::Enum)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetByteArrayResult);
		return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetInt32ArrayResult);
	}

	if (RetType == EUEmkaValueType::Real)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetRealResult);
	if (RetType == EUEmkaValueType::Real32)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetReal32Result);
	if (RetType == EUEmkaValueType::Str)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetStrResult);
	if (RetType == EUEmkaValueType::Int || RetType == EUEmkaValueType::UInt || RetType == EUEmkaValueType::UInt32) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetIntResult);
	return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetInt32Result);
}

// -------------------------------------------------------------------------
// ExpandNode - generates the intermediate Blueprint graph at compile time
// -------------------------------------------------------------------------

void UK2Node_UEmka::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	// UE split pins discard container defaults. Materialize them before MakeStruct expansion.
	TFunction<bool(UEdGraphPin*, const FString&)> RestoreSplitDefaults;
	RestoreSplitDefaults = [this, &CompilerContext, SourceGraph, &RestoreSplitDefaults](UEdGraphPin* Parent, const FString& ParentText)
	{
		for (UEdGraphPin* Child : Parent->SubPins)
		{
			if (!Child->LinkedTo.IsEmpty() || (!Child->PinType.IsContainer() && Child->SubPins.IsEmpty())) continue;
			FString FieldText;
			FString Error;
			const FName FieldName(*Child->PinName.ToString().RightChop(Parent->PinName.ToString().Len() + 1));
			if (!UEmkaRecordTypes::GetFieldDefaultValue(Parent->PinType, ParentText, FieldName, FieldText, Error))
			{
				CompilerContext.MessageLog.Error(*FString::Printf(TEXT("UEmka split default: %s @@"), *Error), this);
				return false;
			}
			if (!Child->SubPins.IsEmpty())
			{
				if (!RestoreSplitDefaults(Child, FieldText)) return false;
				continue;
			}
			TArray<uint8> Bytes;
			const FString& DefaultText = Child->DefaultValue.IsEmpty() ? FieldText : Child->DefaultValue;
			if (!UEmkaRecordTypes::EncodeDefaultValue(Child->PinType, DefaultText, Bytes, Error))
			{
				CompilerContext.MessageLog.Error(*FString::Printf(TEXT("UEmka split container default: %s @@"), *Error), this);
				return false;
			}
			UK2Node_CallFunction* DefaultNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			DefaultNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeCompositeDefaultParam), UUEmkaFunctionLibrary::StaticClass());
			DefaultNode->AllocateDefaultPins();
			DefaultNode->FindPinChecked(TEXT("Value"))->DefaultValue = BytesToHex(Bytes.GetData(), Bytes.Num());
			DefaultNode->FindPinChecked(TEXT("bIsArray"))->DefaultValue = Child->PinType.IsArray() ? TEXT("true") : TEXT("false");
			UK2Node_CallFunction* DecodeNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			DecodeNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetCompositeResult), UUEmkaFunctionLibrary::StaticClass());
			DecodeNode->AllocateDefaultPins();
			DefaultNode->FindPinChecked(TEXT("ReturnValue"))->MakeLinkTo(DecodeNode->FindPinChecked(TEXT("Result")));
			UEdGraphPin* Output = DecodeNode->FindPinChecked(TEXT("Value"), EGPD_Output);
			Output->PinType = Child->PinType;
			Output->MakeLinkTo(Child);
		}
		return true;
	};
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction == EGPD_Input && !Pin->ParentPin && !Pin->SubPins.IsEmpty() && !RestoreSplitDefaults(Pin, Pin->DefaultValue))
		{
			BreakAllNodeLinks();
			return;
		}
	}
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (!ParsedSignature.bValid)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("ExpandInvalidSig", "UEmka node has no valid exported function: @@").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	if (!ParsedSignature.UnsupportedReason.IsEmpty())
	{
		CompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("ExpandUnsupportedSig", "UEmka node: {0} @@"), FText::FromString(ParsedSignature.UnsupportedReason)).ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	const bool bMultiReturn = ParsedSignature.ReturnParams.Num() >= 2;

	// --- Spawn runner call ---
	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	if (bMultiReturn)
	{
		CallNode->FunctionReference.SetExternalMember(ScriptAsset ? GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, RunUmkaAssetMultiConfigured)
			: GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, RunUmkaInlineMultiConfigured), UUEmkaFunctionLibrary::StaticClass());
	}
	else
	{
		CallNode->FunctionReference.SetExternalMember(ScriptAsset ? GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, RunUmkaAssetConfigured)
			: GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, RunUmkaInlineConfigured), UUEmkaFunctionLibrary::StaticClass());
	}
	CallNode->AllocateDefaultPins();

	FString SessionIdText;
	NodeGuid.ExportTextItem(SessionIdText, FGuid(), this, PPF_None, nullptr);
	CallNode->FindPinChecked(TEXT("SessionId"), EGPD_Input)->DefaultValue = SessionIdText;
	FString OptionsText;
	FUEmkaExecutionOptions::StaticStruct()->ExportText(OptionsText, &ExecutionOptions, nullptr, this, PPF_None, nullptr);
	CallNode->FindPinChecked(TEXT("Options"), EGPD_Input)->DefaultValue = OptionsText;
	if (bExposeRuntimeStatus)
	{
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(GetRuntimeStatusPinName(PIN_Success), EGPD_Output), *CallNode->FindPinChecked(PIN_ReturnValue, EGPD_Output));
		CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(GetRuntimeStatusPinName(PIN_Error), EGPD_Output), *CallNode->FindPinChecked(TEXT("Error"), EGPD_Output));
	}

	// Wire exec in -> runner
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *CallNode->GetExecPin());

	// Wire exec out -> Then
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output), *CallNode->GetThenPin());

	// Set Script literal - includes the generated __uemka_call shim for struct signatures
	CallNode->FindPinChecked(TEXT("Script"))->DefaultValue = GetEffectiveScript(GetScriptSource(), ParsedSignature);
	if (ScriptAsset) CallNode->FindPinChecked(TEXT("Asset"))->DefaultObject = ScriptAsset;

	// Set FunctionName literal
	CallNode->FindPinChecked(TEXT("FunctionName"))->DefaultValue = GetEffectiveFunctionName(ParsedSignature);

	if (bMultiReturn)
	{
		// Set ResultTypes: "type:arrayKind:enumByteSize" triples. arrayKind is
		// 0 for scalar, 1 for []T, and 2 for [N]T.
		TArray<FString> TypeValues;
		for (const FUEmkaPinDef& Def : ParsedSignature.ReturnParams)
		{
			const int32 ArrayKind = Def.bIsStaticArray ? 2 : (Def.bIsArray ? 1 : 0);
			TypeValues.Add(FString::Printf(TEXT("%d:%d:%d"), static_cast<int32>(Def.Type), ArrayKind, Def.EnumByteSize));
		}
		CallNode->FindPinChecked(TEXT("ResultTypes"))->DefaultValue = FString::Join(TypeValues, TEXT(","));
	}
	else
	{
		// Set ResultType literal
		const EUEmkaValueType ResultType = ParsedSignature.ReturnType.IsSet() ? ParsedSignature.ReturnType.GetValue() : EUEmkaValueType::Void;
		CallNode->FindPinChecked(TEXT("ResultType"))->DefaultValue = UEnum::GetValueAsString(ResultType);

		// Set bResultIsArray literal
		if (UEdGraphPin* IsArrayPin = CallNode->FindPin(TEXT("bResultIsArray")))
		{
			IsArrayPin->DefaultValue = ParsedSignature.bReturnIsArray ? TEXT("true") : TEXT("false");
		}
		if (UEdGraphPin* IsStaticArrayPin = CallNode->FindPin(TEXT("bResultIsStaticArray")))
		{
			IsStaticArrayPin->DefaultValue = ParsedSignature.bReturnIsStaticArray ? TEXT("true") : TEXT("false");
		}
	}

	// --- Build TArray<FUEmkaScriptParam> via Make*Param helpers (avoids UK2Node_MakeStruct) ---
	// Always create the MakeArray node - even for zero params it must be wired to satisfy BP compiler
	UK2Node_MakeArray* MakeArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
	MakeArrayNode->AllocateDefaultPins();

	if (ParsedSignature.Params.Num() == 0)
	{
		// Remove the default [0] pin so MakeArray outputs an empty array
		if (UEdGraphPin* DefaultPin = MakeArrayNode->FindPin(TEXT("[0]"), EGPD_Input))
		{
			MakeArrayNode->Pins.Remove(DefaultPin);
		}
	}
	else
	{
		for (int32 i = 1; i < ParsedSignature.Params.Num(); ++i)
		{
			MakeArrayNode->AddInputPin();
		}

		// Pre-type all MakeArray element pins
		{
			FEdGraphPinType StructPin;
			StructPin.PinCategory = UEdGraphSchema_K2::PC_Struct;
			StructPin.PinSubCategoryObject = FUEmkaScriptParam::StaticStruct();
			for (UEdGraphPin* Pin : MakeArrayNode->Pins)
			{
				if (Pin->Direction == EGPD_Input) Pin->PinType = StructPin;
			}
		}

		for (int32 i = 0; i < ParsedSignature.Params.Num(); ++i)
		{
			const FUEmkaPinDef& Param = ParsedSignature.Params[i];

			// Choose the typed helper - array vs scalar, CallFunction avoids MakeStruct validation warnings
			UEdGraphPin* InputPin = FindPin(FName(*Param.Name), EGPD_Input);
			const bool bCompiledDefault = InputPin && InputPin->LinkedTo.IsEmpty() && Param.CompiledValue.bHasDefault
				&& !Param.CompiledValue.DefaultCompositeValue.IsEmpty()
				&& (!Param.CompiledValue.bIsStruct || InputPin->DefaultValue.Equals(InputPin->AutogeneratedDefaultValue, ESearchCase::CaseSensitive));
			const bool bLiteralComposite = InputPin && InputPin->LinkedTo.IsEmpty()
				&& (Param.Type == EUEmkaValueType::Composite || InputPin->PinType.IsContainer()) && !bCompiledDefault;
			TArray<uint8> DefaultBytes = Param.CompiledValue.DefaultCompositeValue;
			if (bLiteralComposite)
			{
				FString Error;
				if (!UEmkaRecordTypes::EncodeDefaultValue(InputPin->PinType, InputPin->DefaultValue, DefaultBytes, Error))
				{
					CompilerContext.MessageLog.Error(*FString::Printf(TEXT("UEmka composite default: %s @@"), *Error), this);
					BreakAllNodeLinks();
					return;
				}
			}
			const bool bCompositeDefault = bCompiledDefault || bLiteralComposite;
			const FName MakeFuncName = bCompositeDefault ? GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeCompositeDefaultParam)
				: (Param.Type == EUEmkaValueType::Composite ? GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeCompositeParam) : GetMakeParamFuncName(Param.Type, Param.bIsArray));

			UK2Node_CallFunction* MakeParamNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			MakeParamNode->FunctionReference.SetExternalMember(MakeFuncName, UUEmkaFunctionLibrary::StaticClass());
			MakeParamNode->AllocateDefaultPins();

			// MakeIntParam / MakeIntArrayParam / MakeInt64ArrayParam / MakeByteArrayParam take an explicit Type enum
			if (UEdGraphPin* TypePin = MakeParamNode->FindPin(TEXT("Type")))
			{
				TypePin->DefaultValue = UEnum::GetValueAsString(Param.Type);
			}
			if (UEdGraphPin* IsStaticArrayPin = MakeParamNode->FindPin(TEXT("bIsStaticArray")))
			{
				IsStaticArrayPin->DefaultValue = Param.bIsStaticArray ? TEXT("true") : TEXT("false");
			}

			// Connect our input pin -> helper Values/Value pin
			if (bCompositeDefault)
			{
				MakeParamNode->FindPinChecked(TEXT("Value"))->DefaultValue = BytesToHex(DefaultBytes.GetData(), DefaultBytes.Num());
				MakeParamNode->FindPinChecked(TEXT("bIsArray"))->DefaultValue = Param.bIsArray ? TEXT("true") : TEXT("false");
			}
			else if (InputPin)
			{
				const FName ValuePinName = Param.bIsArray && Param.Type != EUEmkaValueType::Composite ? TEXT("Values") : TEXT("Value");
				if (UEdGraphPin* ValuePin = MakeParamNode->FindPin(ValuePinName))
				{
					if (Param.Type == EUEmkaValueType::Composite) ValuePin->PinType = InputPin->PinType;
					CompilerContext.MovePinLinksToIntermediate(*InputPin, *ValuePin);
				}
			}

			// Helper ReturnValue -> MakeArray element [i]
			if (UEdGraphPin* ParamOutPin = MakeParamNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
			{
				if (UEdGraphPin* ArrayElemPin = MakeArrayNode->FindPin(*FString::Printf(TEXT("[%d]"), i)))
				{
					ParamOutPin->MakeLinkTo(ArrayElemPin);
				}
			}
		}
	}

	// Explicitly type the Array output pin before linking
	UEdGraphPin* ArrayOutPin = MakeArrayNode->FindPin(TEXT("Array"), EGPD_Output);
	if (ArrayOutPin)
	{
		ArrayOutPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		ArrayOutPin->PinType.PinSubCategoryObject = FUEmkaScriptParam::StaticStruct();
		ArrayOutPin->PinType.ContainerType = EPinContainerType::Array;
	}

	UEdGraphPin* ParamsPin = CallNode->FindPin(TEXT("Params"));
	if (ArrayOutPin && ParamsPin)
	{
		ArrayOutPin->MakeLinkTo(ParamsPin);
	}

	// --- Extract result(s) via Get*Result helpers ---
	if (bMultiReturn)
	{
		// Results is a TArray<FUEmkaScriptParam> output pin on RunUmkaInlineMulti
		UEdGraphPin* ResultsArrayPin = CallNode->FindPin(TEXT("Results"), EGPD_Output);

		for (int32 i = 0; i < ParsedSignature.ReturnParams.Num(); ++i)
		{
			const FUEmkaPinDef& Def = ParsedSignature.ReturnParams[i];

			// GetMultiResultAt(Results, Index) -> FUEmkaScriptParam
			UK2Node_CallFunction* GetAtNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			GetAtNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetMultiResultAt), UUEmkaFunctionLibrary::StaticClass());
			GetAtNode->AllocateDefaultPins();

			if (ResultsArrayPin)
			{
				if (UEdGraphPin* GetAtResultsPin = GetAtNode->FindPin(TEXT("Results")))
				{
					ResultsArrayPin->MakeLinkTo(GetAtResultsPin);
				}
			}

			if (UEdGraphPin* IndexPin = GetAtNode->FindPin(TEXT("Index")))
			{
				IndexPin->DefaultValue = FString::FromInt(i);
			}

			// GetXxxResult(FUEmkaScriptParam) -> typed value -> output pin
			UK2Node_CallFunction* GetResultNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			GetResultNode->FunctionReference.SetExternalMember(Def.Type == EUEmkaValueType::Composite
				? GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetCompositeResult) : GetGetResultFuncName(Def.Type, Def.bIsArray), UUEmkaFunctionLibrary::StaticClass());
			GetResultNode->AllocateDefaultPins();

			if (UEdGraphPin* GetAtOut = GetAtNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
			{
				if (UEdGraphPin* GetResultIn = GetResultNode->FindPin(TEXT("Result")))
				{
					GetAtOut->MakeLinkTo(GetResultIn);
				}
			}

			FName OutputPinName = *FString::Printf(TEXT("ReturnValue%d"), i + 1);
			UEdGraphPin* OutputPin = FindPin(OutputPinName, EGPD_Output);
			if (UEdGraphPin* GetOutPin = GetResultNode->FindPin(Def.Type == EUEmkaValueType::Composite ? TEXT("Value") : TEXT("ReturnValue"), EGPD_Output))
			{
				if (OutputPin && Def.Type == EUEmkaValueType::Composite) GetOutPin->PinType = OutputPin->PinType;
				if (OutputPin) CompilerContext.MovePinLinksToIntermediate(*OutputPin, *GetOutPin);
			}
		}
	}
	else if (ParsedSignature.ReturnType.IsSet())
	{
		UK2Node_CallFunction* GetResultNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		const bool bComposite = ParsedSignature.ReturnType.GetValue() == EUEmkaValueType::Composite;
		GetResultNode->FunctionReference.SetExternalMember(bComposite ? GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetCompositeResult)
			: GetGetResultFuncName(ParsedSignature.ReturnType.GetValue(), ParsedSignature.bReturnIsArray), UUEmkaFunctionLibrary::StaticClass());
		GetResultNode->AllocateDefaultPins();

		// RunUmkaInline Result out -> GetResult input
		UEdGraphPin* ResultStructPin = CallNode->FindPin(TEXT("Result"), EGPD_Output);
		if (UEdGraphPin* GetInputPin = GetResultNode->FindPin(TEXT("Result")))
		{
			if (ResultStructPin) ResultStructPin->MakeLinkTo(GetInputPin);
		}

		// GetResult ReturnValue -> our typed output pin
		UEdGraphPin* OutputPin = FindPin(PIN_ReturnValue, EGPD_Output);
		if (UEdGraphPin* GetOutPin = GetResultNode->FindPin(bComposite ? TEXT("Value") : TEXT("ReturnValue"), EGPD_Output))
		{
			if (OutputPin && bComposite) GetOutPin->PinType = OutputPin->PinType;
			if (OutputPin) CompilerContext.MovePinLinksToIntermediate(*OutputPin, *GetOutPin);
		}
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
