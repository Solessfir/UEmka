// Copyright Solessfir 2026. All Rights Reserved.

#include "K2Node_UEmka.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UEmkaCoverageCases.h"

namespace
{
FUEmkaScriptParam MakeCoverageArray(const UEmkaTests::FTypeCase& Case, const bool bStatic, const bool bEmpty = false)
{
	switch (Case.Type)
	{
		case EUEmkaValueType::Bool:
			return UUEmkaFunctionLibrary::MakeBoolArrayParam(bEmpty ? TArray<bool>{} : TArray<bool>{Case.First.IntValue != 0, Case.Second.IntValue != 0}, bStatic);
		case EUEmkaValueType::UInt8:
		case EUEmkaValueType::Char:
			return UUEmkaFunctionLibrary::MakeByteArrayParam(Case.Type, bEmpty ? TArray<uint8>{} : TArray<uint8>{static_cast<uint8>(Case.First.IntValue), static_cast<uint8>(Case.Second.IntValue)}, bStatic);
		case EUEmkaValueType::Int8:
		case EUEmkaValueType::Int16:
		case EUEmkaValueType::Int32:
		case EUEmkaValueType::UInt16:
			return UUEmkaFunctionLibrary::MakeIntArrayParam(Case.Type, bEmpty ? TArray<int32>{} : TArray<int32>{static_cast<int32>(Case.First.IntValue), static_cast<int32>(Case.Second.IntValue)}, bStatic);
		case EUEmkaValueType::Real:
			return UUEmkaFunctionLibrary::MakeRealArrayParam(bEmpty ? TArray<double>{} : TArray<double>{Case.First.RealValue, Case.Second.RealValue}, bStatic);
		case EUEmkaValueType::Real32:
			return UUEmkaFunctionLibrary::MakeReal32ArrayParam(bEmpty ? TArray<float>{} : TArray<float>{Case.First.Real32Value, Case.Second.Real32Value}, bStatic);
		case EUEmkaValueType::Str:
			return UUEmkaFunctionLibrary::MakeStrArrayParam(bEmpty ? TArray<FString>{} : TArray<FString>{Case.First.StringValue, Case.Second.StringValue}, bStatic);
		default:
			return UUEmkaFunctionLibrary::MakeInt64ArrayParam(Case.Type, bEmpty ? TArray<int64>{} : TArray<int64>{Case.First.IntValue, Case.Second.IntValue}, bStatic);
	}
}

void CheckCoverageValue(FAutomationTestBase& Test, const FString& Label, const FUEmkaScriptParam& Actual, const FUEmkaScriptParam& Expected)
{
	Test.TestEqual(Label + TEXT(" type"), Actual.Type, Expected.Type);
	Test.TestEqual(Label + TEXT(" array shape"), Actual.bIsArray, Expected.bIsArray);
	Test.TestEqual(Label + TEXT(" fixed shape"), Actual.bIsStaticArray, Expected.bIsStaticArray);
	if (Expected.bIsArray)
	{
		switch (Expected.Type)
		{
			case EUEmkaValueType::Bool:
				Test.TestTrue(Label + TEXT(" Boolean values"), UUEmkaFunctionLibrary::GetBoolArrayResult(Actual) == UUEmkaFunctionLibrary::GetBoolArrayResult(Expected));
				break;
			case EUEmkaValueType::UInt8:
			case EUEmkaValueType::Char:
				Test.TestTrue(Label + TEXT(" byte values"), UUEmkaFunctionLibrary::GetByteArrayResult(Actual) == UUEmkaFunctionLibrary::GetByteArrayResult(Expected));
				break;
			case EUEmkaValueType::Int8:
			case EUEmkaValueType::Int16:
			case EUEmkaValueType::Int32:
			case EUEmkaValueType::UInt16:
				Test.TestTrue(Label + TEXT(" integer values"), UUEmkaFunctionLibrary::GetInt32ArrayResult(Actual) == UUEmkaFunctionLibrary::GetInt32ArrayResult(Expected));
				break;
			case EUEmkaValueType::Real:
				Test.TestTrue(Label + TEXT(" double values"), UUEmkaFunctionLibrary::GetRealArrayResult(Actual) == Expected.RealArrayValue);
				break;
			case EUEmkaValueType::Real32:
				Test.TestTrue(Label + TEXT(" float values"), UUEmkaFunctionLibrary::GetReal32ArrayResult(Actual) == Expected.Real32ArrayValue);
				break;
			case EUEmkaValueType::Str:
				Test.TestTrue(Label + TEXT(" string values"), UUEmkaFunctionLibrary::GetStrArrayResult(Actual) == Expected.StringArrayValue);
				break;
			default:
				Test.TestTrue(Label + TEXT(" 64-bit values"), UUEmkaFunctionLibrary::GetIntArrayResult(Actual) == Expected.IntArrayValue);
				break;
		}
		return;
	}
	switch (Expected.Type)
	{
		case EUEmkaValueType::Real:
			Test.TestTrue(Label + TEXT(" double precision"), UUEmkaFunctionLibrary::GetRealResult(Actual) == Expected.RealValue);
			break;
		case EUEmkaValueType::Real32:
			Test.TestTrue(Label + TEXT(" float precision"), UUEmkaFunctionLibrary::GetReal32Result(Actual) == Expected.Real32Value);
			break;
		case EUEmkaValueType::Str:
			Test.TestEqual(Label + TEXT(" string"), UUEmkaFunctionLibrary::GetStrResult(Actual), Expected.StringValue);
			break;
		default:
			Test.TestEqual(Label + TEXT(" integer bits"), UUEmkaFunctionLibrary::GetIntResult(Actual), Expected.IntValue);
			if (Expected.Type != EUEmkaValueType::Int && Expected.Type != EUEmkaValueType::UInt && Expected.Type != EUEmkaValueType::UInt32)
			{
				Test.TestEqual(Label + TEXT(" integer pin"), UUEmkaFunctionLibrary::GetInt32Result(Actual), static_cast<int32>(Expected.IntValue));
			}
			break;
	}
}

FString CoverageResultTypes(const TArray<FUEmkaScriptParam>& Values)
{
	TArray<FString> Types;
	for (const FUEmkaScriptParam& Value : Values)
	{
		Types.Add(FString::Printf(TEXT("%d:%d:8"), static_cast<int32>(Value.Type), Value.bIsArray ? (Value.bIsStaticArray ? 2 : 1) : 0));
	}
	return FString::Join(Types, TEXT(","));
}

bool InspectCoverageFunction(FAutomationTestBase& Test, const FString& Label, const FString& Script, const FString& FunctionName, FUEmkaCompiledSignature& Signature)
{
	if (!Test.TestTrue(Label + TEXT(" compiles for inspection"), UUEmkaFunctionLibrary::InspectScriptFunction(Script, FunctionName, Signature))) return false;
	Test.TestTrue(Label + TEXT(" supported result"), Signature.Result.bSupported);
	for (const FUEmkaCompiledValue& Param : Signature.Params)
	{
		Test.TestTrue(Label + TEXT(" supported parameter ") + Param.Name, Param.bSupported);
	}
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaScalarCoverageTest, "UEmka.Runtime.AllScalarFamiliesAndAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaScalarCoverageTest::RunTest(const FString& Parameters)
{
	for (const UEmkaTests::FTypeCase& Case : UEmkaTests::GetTypeCases())
	{
		for (const bool bAlias : {false, true})
		{
			const FString Script = Case.Declaration + (bAlias ? FString::Printf(TEXT("type FirstAlias = %s\ntype Alias = FirstAlias\n"), *Case.Name) : FString())
				+ FString::Printf(TEXT("fn Test*(Value: %s): %s { return Value }"), bAlias ? TEXT("Alias") : *Case.Name, bAlias ? TEXT("Alias") : *Case.Name);
			const FString Label = Case.Name + (bAlias ? TEXT(" alias") : TEXT(" scalar"));
			FUEmkaCompiledSignature Compiled;
			if (!InspectCoverageFunction(*this, Label, Script, TEXT("Test"), Compiled)) continue;
			TestEqual(Label + TEXT(" compiled result type"), Compiled.Result.Type, Case.Type);
			TestEqual(Label + TEXT(" compiled enum identity"), Compiled.Result.bIsEnum, !Case.Declaration.IsEmpty());
			for (const FUEmkaScriptParam& Param : {Case.First, Case.Second})
			{
				FUEmkaScriptParam Result;
				FString Error;
				if (TestTrue(Label + TEXT(" executes"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"), {Param}, Case.Type, false, false, Result, Error)))
				{
					CheckCoverageValue(*this, Label, Result, Param);
				}
				TestTrue(Label + TEXT(" has no error"), Error.IsEmpty());
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaArrayCoverageTest, "UEmka.Runtime.AllArrayFamiliesAndAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaArrayCoverageTest::RunTest(const FString& Parameters)
{
	for (const UEmkaTests::FTypeCase& Case : UEmkaTests::GetTypeCases())
	{
		for (const bool bAlias : {false, true})
		{
			for (const bool bStatic : {false, true})
			{
				const FString Shape = bStatic ? TEXT("[2]") : TEXT("[]");
				const FString ArrayType = Shape + Case.Name;
				const FString Script = Case.Declaration + (bAlias ? FString::Printf(TEXT("type Element = %s\ntype Values = %sElement\ntype Alias = Values\n"), *Case.Name, *Shape) : FString())
					+ FString::Printf(TEXT("fn Test*(Values: %s): %s { return Values }"), bAlias ? TEXT("Alias") : *ArrayType, bAlias ? TEXT("Alias") : *ArrayType);
				const FString Label = ArrayType + (bAlias ? TEXT(" alias") : TEXT(" direct"));
				FUEmkaCompiledSignature Compiled;
				if (!InspectCoverageFunction(*this, Label, Script, TEXT("Test"), Compiled)) continue;
				TestEqual(Label + TEXT(" compiled element"), Compiled.Result.Type, Case.Type);
				TestTrue(Label + TEXT(" compiled array shape"), Compiled.Result.bIsArray && Compiled.Result.bIsStaticArray == bStatic);
				if (bStatic) TestEqual(Label + TEXT(" compiled length"), Compiled.Result.ArrayLen, 2);
				for (const bool bEmpty : {false, true})
				{
					if (bStatic && bEmpty) continue;
					const FUEmkaScriptParam Param = MakeCoverageArray(Case, bStatic, bEmpty);
					FUEmkaScriptParam Result;
					FString Error;
					if (TestTrue(Label + (bEmpty ? TEXT(" empty executes") : TEXT(" executes")), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"), {Param}, Case.Type, true, bStatic, Result, Error)))
					{
						CheckCoverageValue(*this, Label, Result, Param);
					}
					TestTrue(Label + TEXT(" has no error"), Error.IsEmpty());
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaLegacyEnumCoverageTest, "UEmka.Runtime.LegacyEnumMarkerAllBases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaLegacyEnumCoverageTest::RunTest(const FString& Parameters)
{
	for (const UEmkaTests::FTypeCase& Case : UEmkaTests::GetTypeCases())
	{
		if (Case.Declaration.IsEmpty()) continue;
		for (const FString& Shape : {FString(), FString(TEXT("[]")), FString(TEXT("[2]"))})
		{
			const bool bArray = !Shape.IsEmpty();
			const bool bStatic = Shape == TEXT("[2]");
			const FString Script = Case.Declaration + FString::Printf(TEXT("fn Test*(Value: %s%s): %s%s { return Value }"), *Shape, *Case.Name, *Shape, *Case.Name);
			FUEmkaCompiledSignature Compiled;
			if (!InspectCoverageFunction(*this, Case.Name + Shape, Script, TEXT("Test"), Compiled)) continue;
			for (const FUEmkaScriptParam& Boundary : {Case.First, Case.Second})
			{
				FUEmkaScriptParam Param = bArray ? MakeCoverageArray(Case, bStatic) : Boundary;
				Param.Type = EUEmkaValueType::Enum;
				FUEmkaScriptParam Result;
				FString Error;
				if (TestTrue(Case.Name + Shape + TEXT(" legacy marker executes"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"), {Param}, EUEmkaValueType::Enum, bArray, bStatic, Result, Error)))
				{
					CheckCoverageValue(*this, Case.Name + Shape, Result, Param);
				}
				if (bArray) break;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaStringLiteralCoverageTest, "UEmka.Runtime.StringLiteralEncoding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaStringLiteralCoverageTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Expressions = {TEXT("\"\""), TEXT("\"Zażółć gęślą jaźń 世界 🙂\""), TEXT("\"line\\n\\t\\\"quoted\\\"\\\\tail\""), TEXT("`raw \"quotes\" \\ path\n世界 🙂`")};
	const TArray<FString> Expected = {TEXT(""), TEXT("Zażółć gęślą jaźń 世界 🙂"), TEXT("line\n\t\"quoted\"\\tail"), TEXT("raw \"quotes\" \\ path\n世界 🙂")};
	for (int32 Index = 0; Index < Expressions.Num(); ++Index)
	{
		const FString Script = TEXT("fn Test*(): str { return ") + Expressions[Index] + TEXT(" }");
		FUEmkaCompiledSignature Compiled;
		if (!InspectCoverageFunction(*this, Expressions[Index], Script, TEXT("Test"), Compiled)) continue;
		FUEmkaScriptParam Result;
		FString Error;
		if (TestTrue(TEXT("String literal executes"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("Test"), {}, EUEmkaValueType::Str, false, false, Result, Error)))
		{
			TestEqual(TEXT("UTF-8 literal preserved"), UUEmkaFunctionLibrary::GetStrResult(Result), Expected[Index]);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaMixedTupleCoverageTest, "UEmka.Runtime.MixedTupleCompiledLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaMixedTupleCoverageTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("type Kind = enum(int16) { Low = -1234; High = 1234 }\n")
		TEXT("fn Test*(Tiny: int8, Code: Kind, Huge: int, Flag: bool, Byte: char, Float: real32, Double: real, Text: str, Dynamic: []str, Fixed: [2]str, Floats: []real32, Doubles: [2]real): ")
		TEXT("(int8, Kind, int, bool, char, real32, real, str, []str, [2]str, []real32, [2]real) ")
		TEXT("{ return Tiny, Code, Huge, Flag, Byte, Float, Double, Text, Dynamic, Fixed, Floats, Doubles }");
	const TArray<FUEmkaScriptParam> Params = {
		UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int8, -128),
		UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int16, -1234),
		UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, MIN_int64),
		UUEmkaFunctionLibrary::MakeBoolParam(true),
		UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Char, 255),
		UUEmkaFunctionLibrary::MakeReal32Param(1.2345679f),
		UUEmkaFunctionLibrary::MakeRealParam(1.2345678901234567),
		UUEmkaFunctionLibrary::MakeStrParam(TEXT("Zażółć 世界 🙂")),
		UUEmkaFunctionLibrary::MakeStrArrayParam({TEXT("世界"), TEXT("")}, false),
		UUEmkaFunctionLibrary::MakeStrArrayParam({TEXT(""), TEXT("🙂")}, true),
		UUEmkaFunctionLibrary::MakeReal32ArrayParam({1.2345679f, -0.03125f}, false),
		UUEmkaFunctionLibrary::MakeRealArrayParam({1.2345678901234567, -1.0 / 3.0}, true)};
	FUEmkaCompiledSignature Compiled;
	if (!InspectCoverageFunction(*this, TEXT("Mixed tuple"), Script, TEXT("Test"), Compiled)) return false;
	TestTrue(TEXT("Compiled return is a tuple"), Compiled.Result.bIsTuple);
	TestEqual(TEXT("Tuple has every mixed field"), Compiled.Result.Fields.Num(), Params.Num());
	if (Compiled.Result.Fields.Num() == Params.Num())
	{
		TestEqual(TEXT("Enum storage is narrower than serialized compatibility width"), Compiled.Result.Fields[1].EnumByteSize, 2);
	}
	TArray<FUEmkaScriptParam> Results;
	FString Error;
	if (!TestTrue(TEXT("Mixed tuple executes using compiled offsets"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, Script, TEXT("Test"), Params, CoverageResultTypes(Params), Results, Error))) return false;
	if (TestEqual(TEXT("Mixed tuple result count"), Results.Num(), Params.Num()))
	{
		for (int32 Index = 0; Index < Params.Num(); ++Index)
		{
			CheckCoverageValue(*this, FString::Printf(TEXT("Mixed field %d"), Index), UUEmkaFunctionLibrary::GetMultiResultAt(Results, Index), Params[Index]);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaFlatStructCoverageTest, "UEmka.Runtime.FlatStructAllPrimitiveAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaFlatStructCoverageTest::RunTest(const FString& Parameters)
{
	const TArray<UEmkaTests::FTypeCase> Cases = UEmkaTests::GetTypeCases();
	// Leave room for Umka's hidden upvalue and structured result parameters.
	for (int32 Start = 0; Start < Cases.Num(); Start += 11)
	{
		TArray<FString> Fields;
		TArray<FUEmkaScriptParam> Params;
		FString Script;
		for (int32 Index = Start; Index < FMath::Min(Start + 11, Cases.Num()); ++Index)
		{
			const UEmkaTests::FTypeCase& Case = Cases[Index];
			Script += Case.Declaration + FString::Printf(TEXT("type FieldAlias%d = %s\n"), Index, *Case.Name);
			Fields.Add(FString::Printf(TEXT("Field%d: FieldAlias%d"), Index, Index));
			Params.Add(Index % 2 ? Case.Second : Case.First);
		}
		Script += TEXT("type Record = struct { ") + FString::Join(Fields, TEXT("; ")) + TEXT(" }\ntype RecordAlias = Record\ntype Alias = RecordAlias\nfn Test*(Input: Alias): Alias { return Input }");
		FUEmkaCompiledSignature Compiled;
		if (!InspectCoverageFunction(*this, TEXT("Flat struct"), Script, TEXT("Test"), Compiled)) continue;
		TestTrue(TEXT("Compiled result retains the original struct"), Compiled.Result.bIsStruct);
		TestEqual(TEXT("Compiled struct field count"), Compiled.Result.Fields.Num(), Params.Num());
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Script);
		TestTrue(TEXT("Flat struct aliases require a supported shim"), Signature.bValid && Signature.UnsupportedReason.IsEmpty() && Signature.bNeedsShim);
		TestEqual(TEXT("All struct inputs flattened"), Signature.Params.Num(), Params.Num());
		TestEqual(TEXT("All struct outputs flattened"), Signature.ReturnParams.Num(), Params.Num());
		const FString EffectiveScript = UK2Node_UEmka::GetEffectiveScript(Script, Signature);
		const FString FunctionName = UK2Node_UEmka::GetEffectiveFunctionName(Signature);
		if (!InspectCoverageFunction(*this, TEXT("Flat struct shim"), EffectiveScript, FunctionName, Compiled)) continue;
		TArray<FUEmkaScriptParam> Results;
		FString Error;
		if (!TestTrue(TEXT("All primitive struct aliases execute through shim"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, EffectiveScript, FunctionName, Params, CoverageResultTypes(Params), Results, Error))) continue;
		if (TestEqual(TEXT("Every flattened field returned"), Results.Num(), Params.Num()))
		{
			for (int32 Index = 0; Index < Params.Num(); ++Index)
			{
				CheckCoverageValue(*this, FString::Printf(TEXT("Struct field %d"), Start + Index), Results[Index], Params[Index]);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaShimForwardingCoverageTest, "UEmka.Runtime.StructShimReturnShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaShimForwardingCoverageTest::RunTest(const FString& Parameters)
{
	const FString Declaration = TEXT("type Number = int16\ntype Record = struct { Value: Number }\ntype Alias = Record\n");
	const TArray<FString> Functions = {
		TEXT("fn Test*(Input: Alias, Values: [2]int) { Values[Input.Value] = 42 }"),
		TEXT("fn Test*(Input: Alias): int16 { return Input.Value }"),
		TEXT("fn Test*(Input: Alias): Alias { return Input }"),
		TEXT("fn Test*(Input: Alias, Values: []str): []str { return append(Values, sprintf(\"%d\", Input.Value)) }"),
		TEXT("fn Test*(Input: Alias, Values: [2]str): [2]str { Values[0] = sprintf(\"%d\", Input.Value); return Values }"),
		TEXT("fn Test*(Input: Alias, Text: str): (int16, str) { return Input.Value, Text }")};
	for (int32 Index = 0; Index < Functions.Num(); ++Index)
	{
		const FString Script = Declaration + Functions[Index];
		FUEmkaCompiledSignature Compiled;
		if (!InspectCoverageFunction(*this, FString::Printf(TEXT("Forwarding source %d"), Index), Script, TEXT("Test"), Compiled)) continue;
		const FUEmkaSignature Signature = UK2Node_UEmka::ParseScript(Script);
		TestTrue(TEXT("Supported forwarding shape needs shim"), Signature.bValid && Signature.UnsupportedReason.IsEmpty() && Signature.bNeedsShim);
		const FString EffectiveScript = UK2Node_UEmka::GetEffectiveScript(Script, Signature);
		const FString FunctionName = UK2Node_UEmka::GetEffectiveFunctionName(Signature);
		if (!InspectCoverageFunction(*this, FString::Printf(TEXT("Forwarding shim %d"), Index), EffectiveScript, FunctionName, Compiled)) continue;
		TArray<FUEmkaScriptParam> Params = {UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int16, Index == 0 ? 1 : -123)};
		if (Index == 0) Params.Add(UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType::Int, {9, 10}, true));
		if (Index == 3 || Index == 4) Params.Add(UUEmkaFunctionLibrary::MakeStrArrayParam({TEXT("世界"), TEXT("🙂")}, Index == 4));
		if (Index == 5) Params.Add(UUEmkaFunctionLibrary::MakeStrParam(TEXT("Zażółć 世界 🙂")));
		FString Error;
		if (Index == 5)
		{
			TArray<FUEmkaScriptParam> Results;
			if (TestTrue(TEXT("Tuple forwarded by struct shim"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr, EffectiveScript, FunctionName, Params, CoverageResultTypes(Params), Results, Error))
				&& TestEqual(TEXT("Forwarded tuple count"), Results.Num(), Params.Num()))
			{
				for (int32 Field = 0; Field < Params.Num(); ++Field) CheckCoverageValue(*this, TEXT("Forwarded tuple"), Results[Field], Params[Field]);
			}
			continue;
		}
		FUEmkaScriptParam Result;
		const EUEmkaValueType ResultType = Index == 0 ? EUEmkaValueType::Void : (Index < 3 ? EUEmkaValueType::Int16 : EUEmkaValueType::Str);
		if (!TestTrue(TEXT("Single result forwarded by struct shim"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, EffectiveScript, FunctionName, Params, ResultType, Index >= 3, Index == 4, Result, Error))) continue;
		if (Index == 0)
		{
			TestTrue(TEXT("Void forwarding has no error"), Error.IsEmpty());
			Params[0] = UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int16, 3);
			Result = UUEmkaFunctionLibrary::MakeStrParam(TEXT("stale"));
			Result.IntValue = 99;
			Result.RealValue = 99;
			Result.Real32Value = 99;
			Result.IntArrayValue = {99};
			Result.RealArrayValue = {99};
			Result.Real32ArrayValue = {99};
			Result.StringArrayValue = {TEXT("stale")};
			AddExpectedError(FunctionName + TEXT(":"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("Void shim reaches the original function's indexed write"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
				EffectiveScript, FunctionName, Params, EUEmkaValueType::Void, false, false, Result, Error));
			TestTrue(TEXT("Void shim propagates the original function's bounds error"), Error.Contains(TEXT("range"), ESearchCase::IgnoreCase));
			TestTrue(TEXT("Failed void shim clears stale result storage"), Result.IntValue == 0 && Result.RealValue == 0 && Result.Real32Value == 0
				&& Result.StringValue.IsEmpty() && Result.IntArrayValue.IsEmpty() && Result.RealArrayValue.IsEmpty()
				&& Result.Real32ArrayValue.IsEmpty() && Result.StringArrayValue.IsEmpty());
		}
		else if (Index < 3)
		{
			if (Index == 2) TestTrue(TEXT("One-field struct unwraps to one scalar"), Signature.ReturnParams.IsEmpty() && Signature.ReturnType.IsSet());
			CheckCoverageValue(*this, TEXT("Scalar or one-field struct"), Result, Params[0]);
		}
		else
		{
			const FUEmkaScriptParam Expected = UUEmkaFunctionLibrary::MakeStrArrayParam(Index == 3 ? TArray<FString>{TEXT("世界"), TEXT("🙂"), TEXT("-123")} : TArray<FString>{TEXT("-123"), TEXT("🙂")}, Index == 4);
			CheckCoverageValue(*this, TEXT("Forwarded array"), Result, Expected);
		}
	}
	return true;
}

#endif
