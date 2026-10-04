// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "UEmkaFunctionLibrary.h"

namespace UEmkaTests
{
struct FTypeCase
{
	FString Name;
	EUEmkaValueType Type;
	FUEmkaScriptParam First;
	FUEmkaScriptParam Second;
	FString Declaration;
};

inline TArray<FTypeCase> GetTypeCases()
{
	TArray<FTypeCase> Cases;
	auto AddInteger = [&Cases](const TCHAR* Name, const EUEmkaValueType Type, const int64 Min, const int64 Max)
	{
		Cases.Add({Name, Type, UUEmkaFunctionLibrary::MakeIntParam(Type, Min), UUEmkaFunctionLibrary::MakeIntParam(Type, Max), {}});
	};
	AddInteger(TEXT("int"), EUEmkaValueType::Int, MIN_int64, MAX_int64);
	AddInteger(TEXT("int8"), EUEmkaValueType::Int8, -128, 127);
	AddInteger(TEXT("int16"), EUEmkaValueType::Int16, -32768, 32767);
	AddInteger(TEXT("int32"), EUEmkaValueType::Int32, MIN_int32, MAX_int32);
	AddInteger(TEXT("uint8"), EUEmkaValueType::UInt8, 0, 255);
	AddInteger(TEXT("uint16"), EUEmkaValueType::UInt16, 0, 65535);
	AddInteger(TEXT("uint32"), EUEmkaValueType::UInt32, 0, 4294967295LL);
	AddInteger(TEXT("uint"), EUEmkaValueType::UInt, 0, -1);
	Cases.Add({TEXT("bool"), EUEmkaValueType::Bool, UUEmkaFunctionLibrary::MakeBoolParam(false), UUEmkaFunctionLibrary::MakeBoolParam(true), {}});
	AddInteger(TEXT("char"), EUEmkaValueType::Char, 0, 255);
	Cases.Add({TEXT("real"), EUEmkaValueType::Real, UUEmkaFunctionLibrary::MakeRealParam(-1234.125), UUEmkaFunctionLibrary::MakeRealParam(1.0 / 3.0), {}});
	Cases.Add({TEXT("real32"), EUEmkaValueType::Real32, UUEmkaFunctionLibrary::MakeReal32Param(-3.25f), UUEmkaFunctionLibrary::MakeReal32Param(0.1f), {}});
	Cases.Add({TEXT("str"), EUEmkaValueType::Str, UUEmkaFunctionLibrary::MakeStrParam(TEXT("Zażółć gęślą jaźń 世界 🚀\n\t\"\\")), UUEmkaFunctionLibrary::MakeStrParam(TEXT("")), {}});
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FTypeCase Enum = Cases[Index];
		Enum.Name = TEXT("TestEnum_") + Cases[Index].Name;
		const FString Min = Index == 0 ? TEXT("(-9223372036854775807 - 1)") : FString::Printf(TEXT("%lld"), Enum.First.IntValue);
		const FString Max = Index == 7 ? TEXT("18446744073709551615") : FString::Printf(TEXT("%lld"), Enum.Second.IntValue);
		Enum.Declaration = FString::Printf(TEXT("type %s = enum(%s) { low = %s; high = %s }\n"), *Enum.Name, *Cases[Index].Name, *Min, *Max);
		Cases.Add(MoveTemp(Enum));
	}
	return Cases;
}
}
