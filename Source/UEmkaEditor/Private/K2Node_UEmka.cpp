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
#include "UEmkaFunctionLibrary.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(K2Node_UEmka)

#define LOCTEXT_NAMESPACE "K2Node_UEmka"

static const FName PIN_ReturnValue(TEXT("ReturnValue"));

// -------------------------------------------------------------------------
// Static helpers
// -------------------------------------------------------------------------

EUEmkaValueType UK2Node_UEmka::ParseUmkaType(const FString& TypeName)
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
	return EUEmkaValueType::Enum; // unknown identifier = user-defined enum type
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
		case EUEmkaValueType::UInt32:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			break;
		case EUEmkaValueType::UInt8:
		case EUEmkaValueType::Char:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			break;
		case EUEmkaValueType::UInt:
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

TArray<FUEmkaStructDef> UK2Node_UEmka::ParseStructDefs(const FString& InScript)
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

			// Only plain primitive/str/enum field types can cross the pin boundary
			const bool bComplexType = TypeText.IsEmpty()
				|| TypeText.Contains(TEXT("["))
				|| TypeText.Contains(TEXT("^"))
				|| TypeText.Contains(TEXT("{"))
				|| TypeText.StartsWith(TEXT("map"))
				|| TypeText.StartsWith(TEXT("fn"))
				|| TypeText.StartsWith(TEXT("weak"))
				|| TypeText.StartsWith(TEXT("interface"))
				|| Raw.ContainsByPredicate([&TypeText](const FRawStruct& S) { return S.Name == TypeText; });
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

FUEmkaSignature UK2Node_UEmka::ParseScript(const FString& InScript)
{
	FUEmkaSignature Sig;

	const TArray<FUEmkaStructDef> Structs = ParseStructDefs(InScript);
	auto FindStruct = [&Structs](const FString& TypeName) -> const FUEmkaStructDef*
	{
		return Structs.FindByPredicate([&TypeName](const FUEmkaStructDef& S) { return S.Name == TypeName; });
	};

	// Find first exported function: fn <name>*(...)
	int32 Pos = 0;
	const int32 Len = InScript.Len();

	auto SkipWhitespace = [&]()
	{
		while (Pos < Len && FChar::IsWhitespace(InScript[Pos])) ++Pos;
	};

	auto ReadIdent = [&]() -> FString
	{
		SkipWhitespace();
		const int32 Start = Pos;
		while (Pos < Len && (FChar::IsAlnum(InScript[Pos]) || InScript[Pos] == '_')) ++Pos;
		return InScript.Mid(Start, Pos - Start);
	};

	// Scan for "fn" keyword followed by whitespace
	while (Pos < Len)
	{
		// Look for 'f' 'n' then whitespace
		if (Pos + 2 < Len
			&& InScript[Pos] == 'f'
			&& InScript[Pos + 1] == 'n'
			&& FChar::IsWhitespace(InScript[Pos + 2]))
		{
			// Make sure 'fn' is not part of a larger identifier
			const bool bAtWordBoundary = (Pos == 0) || !FChar::IsAlnum(InScript[Pos - 1]);
			if (bAtWordBoundary)
			{
				Pos += 2;
				break;
			}
		}
		++Pos;
	}

	if (Pos >= Len) return Sig;

	// Read function name
	FString FuncName = ReadIdent();
	if (FuncName.IsEmpty()) return Sig;

	// Require export marker '*'
	SkipWhitespace();
	if (Pos >= Len || InScript[Pos] != '*') return Sig;
	++Pos;

	// Require '('
	SkipWhitespace();
	if (Pos >= Len || InScript[Pos] != '(') return Sig;
	++Pos;

	// Collect everything inside the parameter parens (handle nested parens)
	const int32 ParamStart = Pos;
	int32 Depth = 1;
	while (Pos < Len && Depth > 0)
	{
		if (InScript[Pos] == '(') ++Depth;
		else if (InScript[Pos] == ')') --Depth;
		if (Depth > 0) ++Pos;
	}
	if (Depth != 0) return Sig; // unmatched parens

	const FString ParamStr = InScript.Mid(ParamStart, Pos - ParamStart).TrimStartAndEnd();
	++Pos; // skip closing ')'

	// Parse parameter list: handles grouped "a, b: int, c: real"
	if (!ParamStr.IsEmpty())
	{
		TArray<FString> NameGroup;
		FString Token;

		auto FlushGroup = [&](const FString& TypeName, bool bIsArray, const FString& ArrayPrefix)
		{
			const FString TrimmedTypeName = TypeName.TrimStartAndEnd();
			const EUEmkaValueType VType = ParseUmkaType(TrimmedTypeName);
			const FUEmkaStructDef* StructDef = (VType == EUEmkaValueType::Enum) ? FindStruct(TrimmedTypeName) : nullptr;

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
						Def.Type = ParseUmkaType(Field.TypeText);
						Def.FriendlyName = FString::Printf(TEXT("%s.%s"), *TrimmedName, *Field.Name);
						if (Def.Type == EUEmkaValueType::Enum)
						{
							Def.EnumTypeName = Field.TypeText;
							Def.FriendlyName += FString::Printf(TEXT(" (%s)"), *Field.TypeText);
						}
						Sig.Params.Add(Def);
					}
					continue;
				}

				FUEmkaShimParam& Shim = Sig.ShimParams.AddDefaulted_GetRef();
				Shim.Name = TrimmedName;
				Shim.TypeText = ArrayPrefix + TrimmedTypeName;

				FUEmkaPinDef Def;
				Def.Name = TrimmedName;
				Def.Type = VType;
				Def.bIsArray = bIsArray;
				if (VType == EUEmkaValueType::Enum)
				{
					Def.EnumTypeName = TrimmedTypeName;
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

				// Detect dynamic array prefix []  (static [N] also consumed, treated as dynamic)
				bool bIsArray = false;
				FString ArrayPrefix;
				if (i < ParamStr.Len() && ParamStr[i] == '[')
				{
					bIsArray = true;
					const int32 PrefixStart = i;
					++i;
					while (i < ParamStr.Len() && ParamStr[i] != ']') ++i;
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
				FlushGroup(TypeName, bIsArray, ArrayPrefix);
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
		while (BodyBrace < Len && InScript[BodyBrace] != '{') ++BodyBrace;
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
	if (Pos < Len && InScript[Pos] == ':')
	{
		++Pos;
		SkipWhitespace();

		if (Pos < Len && InScript[Pos] == '(')
		{
			++Pos; // skip '('
			int32 RetIdx = 0;
			while (Pos < Len && InScript[Pos] != ')')
			{
				SkipWhitespace();
				if (Pos >= Len || InScript[Pos] == ')') break;

				bool bIsArray = false;
				if (InScript[Pos] == '[')
				{
					bIsArray = true;
					++Pos;
					while (Pos < Len && InScript[Pos] != ']') ++Pos;
					if (Pos < Len) ++Pos; // skip ']'
					SkipWhitespace();
				}

				const FString TypeName = ReadIdent();
				if (!TypeName.IsEmpty() && TypeName != TEXT("void"))
				{
					if (FindStruct(TypeName))
					{
						Sig.UnsupportedReason = FString::Printf(TEXT("struct '%s' inside a multi-return tuple cannot be passed as pins"), *TypeName);
					}
					FUEmkaPinDef Def;
					Def.Name = FString::Printf(TEXT("item%d"), RetIdx++);
					Def.Type = ParseUmkaType(TypeName);
					Def.bIsArray = bIsArray;
					if (Def.Type == EUEmkaValueType::Enum)
					{
						Def.EnumTypeName = TypeName;
					}
					Sig.ReturnParams.Add(Def);
				}
				SkipWhitespace();
				if (Pos < Len && InScript[Pos] == ',') ++Pos;
			}
			if (Pos < Len && InScript[Pos] == ')') ++Pos;
		}
		else
		{
			// Detect dynamic array prefix [] (static [N] also consumed, treated as dynamic)
			if (Pos < Len && InScript[Pos] == '[')
			{
				Sig.bReturnIsArray = true;
				++Pos;
				while (Pos < Len && InScript[Pos] != ']') ++Pos;
				if (Pos < Len) ++Pos; // skip ']'
				SkipWhitespace();
			}

			const FString RetTypeName = ReadIdent();
			if (!RetTypeName.IsEmpty() && RetTypeName != TEXT("void"))
			{
				const FUEmkaStructDef* StructDef = FindStruct(RetTypeName);
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
						Def.Type = ParseUmkaType(Field.TypeText);
						Def.FriendlyName = Field.Name;
						if (Def.Type == EUEmkaValueType::Enum)
						{
							Def.EnumTypeName = Field.TypeText;
							Def.FriendlyName += FString::Printf(TEXT(" (%s)"), *Field.TypeText);
						}
						Sig.ReturnParams.Add(Def);
					}
				}
				else
				{
					Sig.ReturnType = ParseUmkaType(RetTypeName);
					if (Sig.ReturnType.GetValue() == EUEmkaValueType::Enum)
					{
						Sig.ReturnEnumTypeName = RetTypeName;
					}
				}
			}
		}

		// Single-element tuple or single-field struct is just a single return - unwrap it
		if (Sig.ReturnParams.Num() == 1)
		{
			Sig.ReturnType = Sig.ReturnParams[0].Type;
			Sig.bReturnIsArray = Sig.ReturnParams[0].bIsArray;
			Sig.ReturnEnumTypeName = Sig.ReturnParams[0].EnumTypeName;
			Sig.ReturnParams.Empty();
		}
	}

	Sig.FunctionName = FuncName;
	Sig.bValid = true;

	// Unsupported constructs: keep the signature valid (function exists) but expose no data
	// pins - ValidateNodeDuringCompilation reports UnsupportedReason as a compile error.
	if (!Sig.UnsupportedReason.IsEmpty())
	{
		Sig.Params.Empty();
		Sig.ReturnParams.Empty();
		Sig.ReturnType.Reset();
		Sig.bReturnIsArray = false;
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

	const FString Call = FString::Printf(TEXT("%s(%s)"), *Sig.FunctionName, *FString::Join(CallArgs, TEXT(", ")));

	FString RetDecl;
	FString Body;
	if (!Sig.ReturnStructName.IsEmpty())
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
	Super::PostLoad();
	ParsedSignature = ParseScript(Script);
	UUEmkaFunctionLibrary::CompileCheckScript(GetEffectiveScript(Script, ParsedSignature), LastErrorMessage, LastErrorLine);
}

void UK2Node_UEmka::PostEditUndo()
{
	// Script has been restored by the transaction system - re-derive everything that isn't a UPROPERTY.
	ParsedSignature = ParseScript(Script);
	if (UUEmkaFunctionLibrary::CompileCheckScript(GetEffectiveScript(Script, ParsedSignature), LastErrorMessage, LastErrorLine))
	{
		LastErrorLine = -1;
		LastErrorMessage.Empty();
	}

	// Super rebuilds the node (ReconstructNode -> AllocateDefaultPins -> UpdateGraphNode),
	// which picks up the freshly set LastErrorLine for the syntax highlighter.
	Super::PostEditUndo();
}

void UK2Node_UEmka::AllocateDefaultPins()
{
	// Always re-parse here so pins are correct regardless of how AllocateDefaultPins is invoked
	// (e.g., during Blueprint compiler's internal reconstruction where ParsedSignature may not be pre-populated)
	ParsedSignature = ParseScript(Script);

	// Exec in/out
	CreatePin(EGPD_Input,  UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// Typed input pins from parsed signature
	for (const FUEmkaPinDef& Param : ParsedSignature.Params)
	{
		FEdGraphPinType PinType = GetPinTypeFor(Param.Type);
		if (Param.bIsArray)
		{
			PinType.ContainerType = EPinContainerType::Array;
		}
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, PinType.PinCategory, FName(*Param.Name));
		NewPin->PinType = PinType;
		if (!Param.FriendlyName.IsEmpty())
		{
			NewPin->PinFriendlyName = FText::FromString(Param.FriendlyName);
		}
		else if (Param.Type == EUEmkaValueType::Enum && !Param.EnumTypeName.IsEmpty())
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
			FEdGraphPinType PinType = GetPinTypeFor(Def.Type);
			if (Def.bIsArray)
			{
				PinType.ContainerType = EPinContainerType::Array;
			}
			FName PinName = *FString::Printf(TEXT("ReturnValue%d"), i + 1);
			UEdGraphPin* RetPin = CreatePin(EGPD_Output, PinType.PinCategory, PinName);
			RetPin->PinType = PinType;
			if (!Def.FriendlyName.IsEmpty())
			{
				RetPin->PinFriendlyName = FText::FromString(Def.FriendlyName);
			}
			else if (Def.Type == EUEmkaValueType::Enum && !Def.EnumTypeName.IsEmpty())
			{
				RetPin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("ReturnValue%d (%s)"), i + 1, *Def.EnumTypeName));
			}
		}
	}
	else if (ParsedSignature.bValid && ParsedSignature.ReturnType.IsSet())
	{
		FEdGraphPinType RetPinType = GetPinTypeFor(ParsedSignature.ReturnType.GetValue());
		if (ParsedSignature.bReturnIsArray)
		{
			RetPinType.ContainerType = EPinContainerType::Array;
		}
		UEdGraphPin* RetPin = CreatePin(EGPD_Output, RetPinType.PinCategory, PIN_ReturnValue);
		RetPin->PinType = RetPinType;
		if (ParsedSignature.ReturnType.GetValue() == EUEmkaValueType::Enum && !ParsedSignature.ReturnEnumTypeName.IsEmpty())
		{
			RetPin->PinFriendlyName = FText::FromString(ParsedSignature.ReturnEnumTypeName);
		}
	}

Super::AllocateDefaultPins();
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

	if (Script.IsEmpty())
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
	if (!UUEmkaFunctionLibrary::CompileCheckScript(GetEffectiveScript(Script, ParsedSignature), CompileError, ErrorLine))
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
	Script = NewScript;
	const FUEmkaSignature NewSig = ParseScript(Script);

	// Live compile check - drives squiggly line highlighting in SGraphNode_UEmka.
	// Checks the effective script so generated shim errors surface immediately.
	if (UUEmkaFunctionLibrary::CompileCheckScript(GetEffectiveScript(Script, NewSig), LastErrorMessage, LastErrorLine))
	{
		LastErrorLine = -1;
		LastErrorMessage.Empty();
	}

	if (NewSig != ParsedSignature)
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

		if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(this))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		}
	}
	else
	{
		ParsedSignature = NewSig;
	}
}

// -------------------------------------------------------------------------
// ExpandNode helpers
// -------------------------------------------------------------------------

// Selects the Make*Param helper matching a pin's type - mirror of GetGetResultFuncName.
static FName GetMakeParamFuncName(const EUEmkaValueType Type, const bool bIsArray)
{
	if (bIsArray)
	{
		if (Type == EUEmkaValueType::Real)								   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeRealArrayParam);
		if (Type == EUEmkaValueType::Real32)							   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeReal32ArrayParam);
		if (Type == EUEmkaValueType::Str)								   return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeStrArrayParam);
		if (Type == EUEmkaValueType::Int || Type == EUEmkaValueType::UInt) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, MakeInt64ArrayParam);
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
		if (RetType == EUEmkaValueType::Real)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetRealArrayResult);
		if (RetType == EUEmkaValueType::Real32)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetReal32ArrayResult);
		if (RetType == EUEmkaValueType::Str)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetStrArrayResult);
		if (RetType == EUEmkaValueType::Int || RetType == EUEmkaValueType::UInt) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetIntArrayResult);
		if (RetType == EUEmkaValueType::UInt8
		 || RetType == EUEmkaValueType::Char
		 || RetType == EUEmkaValueType::Enum)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetByteArrayResult);
		return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetInt32ArrayResult);
	}

	if (RetType == EUEmkaValueType::Real)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetRealResult);
	if (RetType == EUEmkaValueType::Real32)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetReal32Result);
	if (RetType == EUEmkaValueType::Str)									 return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetStrResult);
	if (RetType == EUEmkaValueType::Int || RetType == EUEmkaValueType::UInt) return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetIntResult);
	return GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, GetInt32Result);
}

// -------------------------------------------------------------------------
// ExpandNode - generates the intermediate Blueprint graph at compile time
// -------------------------------------------------------------------------

void UK2Node_UEmka::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
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
		CallNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, RunUmkaInlineMulti), UUEmkaFunctionLibrary::StaticClass());
	}
	else
	{
		CallNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UUEmkaFunctionLibrary, RunUmkaInline), UUEmkaFunctionLibrary::StaticClass());
	}
	CallNode->AllocateDefaultPins();

	// Wire exec in -> runner
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *CallNode->GetExecPin());

	// Wire exec out -> Then
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output), *CallNode->GetThenPin());

	// Set Script literal - includes the generated __uemka_call shim for struct signatures
	CallNode->FindPinChecked(TEXT("Script"))->DefaultValue = GetEffectiveScript(Script, ParsedSignature);

	// Set FunctionName literal
	CallNode->FindPinChecked(TEXT("FunctionName"))->DefaultValue = GetEffectiveFunctionName(ParsedSignature);

	if (bMultiReturn)
	{
		// Set ResultTypes: "type:isArray" pairs (e.g. "0:0,12:0" = int,str  or  "0:1,12:0" = []int,str)
		TArray<FString> TypeValues;
		for (const FUEmkaPinDef& Def : ParsedSignature.ReturnParams)
		{
			TypeValues.Add(FString::Printf(TEXT("%d:%d"), static_cast<int32>(Def.Type), Def.bIsArray ? 1 : 0));
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
			const FName MakeFuncName = GetMakeParamFuncName(Param.Type, Param.bIsArray);

			UK2Node_CallFunction* MakeParamNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			MakeParamNode->FunctionReference.SetExternalMember(MakeFuncName, UUEmkaFunctionLibrary::StaticClass());
			MakeParamNode->AllocateDefaultPins();

			// MakeIntParam / MakeIntArrayParam / MakeInt64ArrayParam / MakeByteArrayParam take an explicit Type enum
			if (UEdGraphPin* TypePin = MakeParamNode->FindPin(TEXT("Type")))
			{
				TypePin->DefaultValue = UEnum::GetValueAsString(Param.Type);
			}

			// Connect our input pin -> helper Values/Value pin
			if (UEdGraphPin* InputPin = FindPin(FName(*Param.Name), EGPD_Input))
			{
				const FName ValuePinName = Param.bIsArray ? TEXT("Values") : TEXT("Value");
				if (UEdGraphPin* ValuePin = MakeParamNode->FindPin(ValuePinName))
				{
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
			GetResultNode->FunctionReference.SetExternalMember(GetGetResultFuncName(Def.Type, Def.bIsArray), UUEmkaFunctionLibrary::StaticClass());
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
			if (UEdGraphPin* GetOutPin = GetResultNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
			{
				if (OutputPin) CompilerContext.MovePinLinksToIntermediate(*OutputPin, *GetOutPin);
			}
		}
	}
	else if (ParsedSignature.ReturnType.IsSet())
	{
		UK2Node_CallFunction* GetResultNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		GetResultNode->FunctionReference.SetExternalMember(GetGetResultFuncName(ParsedSignature.ReturnType.GetValue(), ParsedSignature.bReturnIsArray), UUEmkaFunctionLibrary::StaticClass());
		GetResultNode->AllocateDefaultPins();

		// RunUmkaInline Result out -> GetResult input
		UEdGraphPin* ResultStructPin = CallNode->FindPin(TEXT("Result"), EGPD_Output);
		if (UEdGraphPin* GetInputPin = GetResultNode->FindPin(TEXT("Result")))
		{
			if (ResultStructPin) ResultStructPin->MakeLinkTo(GetInputPin);
		}

		// GetResult ReturnValue -> our typed output pin
		UEdGraphPin* OutputPin = FindPin(PIN_ReturnValue, EGPD_Output);
		if (UEdGraphPin* GetOutPin = GetResultNode->FindPin(TEXT("ReturnValue"), EGPD_Output))
		{
			if (OutputPin) CompilerContext.MovePinLinksToIntermediate(*OutputPin, *GetOutPin);
		}
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
