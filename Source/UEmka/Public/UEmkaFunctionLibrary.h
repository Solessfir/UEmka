// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UEmkaFunctionLibrary.generated.h"

// Maps Umka type keywords to their UmkaStackSlot field and UE pin type.
// All ordinal types except uint use intVal. uint uses uintVal.
// real32 uses real32Val for params but realVal for results ("Not used in result slots").
UENUM(BlueprintType)
enum class EUEmkaValueType : uint8
{
	Int		UMETA(DisplayName = "int"),		// 64-bit signed, intVal, PC_Int64
	Int8	UMETA(DisplayName = "int8"),	// intVal, PC_Int
	Int16	UMETA(DisplayName = "int16"),	// intVal, PC_Int
	Int32	UMETA(DisplayName = "int32"),	// intVal, PC_Int
	UInt8	UMETA(DisplayName = "uint8"),	// intVal, PC_Byte
	UInt16	UMETA(DisplayName = "uint16"),	// intVal, PC_Int
	UInt32	UMETA(DisplayName = "uint32"),	// intVal, PC_Int
	UInt	UMETA(DisplayName = "uint"),	// 64-bit unsigned, uintVal, PC_Int64
	Bool	UMETA(DisplayName = "bool"),	// intVal, PC_Boolean
	Char	UMETA(DisplayName = "char"),	// intVal, PC_Byte
	Real	UMETA(DisplayName = "real"),	// 64-bit float, realVal, PC_Double
	Real32	UMETA(DisplayName = "real32"),	// 32-bit float, real32Val (param) / realVal (result), PC_Float
	Str		UMETA(DisplayName = "str"),		// ptrVal (umkaMakeStr), PC_String
	Enum	UMETA(DisplayName = "enum"),	// user-defined enum type, int64 in Umka, Byte pin in BP
	Void	UMETA(DisplayName = "void"),	// No return value
};

// Ordered, typed parameter for RunUmkaInline. One element per Umka function parameter.
// BlueprintType is required for use in BlueprintCallable UFUNCTION parameters;
// no Make/Break nodes are exposed - UK2Node_UEmka constructs these via MakeStruct internally.
USTRUCT(BlueprintType)
struct FUEmkaScriptParam
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	EUEmkaValueType Type = EUEmkaValueType::Int;

	// Used for all integer types (int, int8..int32, uint8..uint32, bool, char) and uint (reinterpreted)
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	int64 IntValue = 0;

	// Used for real (64-bit) and real32 result (real32 result comes back in realVal, not real32Val)
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	double RealValue = 0.0;

	// Used for real32 params only
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	float Real32Value = 0.f;

	// Used for str
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	FString StringValue;

	// Array flag - set by Make*ArrayParam helpers
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	bool bIsArray = false;

	// Array storage - only one is populated, matching Type
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<int64> IntArrayValue;

	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<double> RealArrayValue;

	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<float> Real32ArrayValue;

	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<FString> StringArrayValue;
};

UCLASS()
class UEMKA_API UUEmkaFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Used by UK2Node_UEmka ExpandNode only. Executes a script inline with ordered typed parameters.
	UFUNCTION(BlueprintCallable, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller"))
	static bool RunUmkaInline(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, EUEmkaValueType ResultType, bool bResultIsArray, FUEmkaScriptParam& Result, FString& Error);

	// Used by UK2Node_UEmka ExpandNode only for multi-return functions (fn foo*(): (int, str)).
	// ResultTypes is a comma-separated list of EUEmkaValueType integer values (e.g. "0,12" for int, str).
	UFUNCTION(BlueprintCallable, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller"))
	static bool RunUmkaInlineMulti(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const FString& ResultTypes, TArray<FUEmkaScriptParam>& Results, FString& Error);

	// Index into the multi-return Results array from RunUmkaInlineMulti.
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam GetMultiResultAt(const TArray<FUEmkaScriptParam>& Results, const int32 Index);

	// Compile-only check - no execution. Used by UK2Node_UEmka for static validation.
	// Returns false and populates OutError / OutLine (1-based) on failure.
	static bool CompileCheckScript(const FString& Script, FString& OutError, int32& OutLine);

	// --- Param construction helpers (ExpandNode intermediate graph only) ---

	// Covers: int, int8, int16, int32, uint8, uint16, uint32, uint, char
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeIntParam(EUEmkaValueType Type, int64 Value);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeBoolParam(bool Value);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeRealParam(double Value);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeReal32Param(float Value);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeStrParam(const FString& Value);

	// --- Result extraction helpers (ExpandNode intermediate graph only) ---

	// Covers uint (int64 output pin)
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static int64 GetIntResult(const FUEmkaScriptParam& Result);

	// Covers int, int8..int32, uint8..uint32, bool, char (int32 output pin)
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static int32 GetInt32Result(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static double GetRealResult(const FUEmkaScriptParam& Result);

	// real32 result comes back in realVal (not real32Val), returns as float
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static float GetReal32Result(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FString GetStrResult(const FUEmkaScriptParam& Result);

	// --- Array param construction helpers (ExpandNode intermediate graph only) ---

	// []int, []int8..[]uint32, []bool, []char - BP pin is TArray<int> (int32)
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeIntArrayParam(EUEmkaValueType Type, const TArray<int32>& Values);

	// []uint - BP pin is TArray<int64>
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeInt64ArrayParam(EUEmkaValueType Type, const TArray<int64>& Values);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeRealArrayParam(const TArray<double>& Values);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeReal32ArrayParam(const TArray<float>& Values);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static FUEmkaScriptParam MakeStrArrayParam(const TArray<FString>& Values);

	// --- Array result extraction helpers (ExpandNode intermediate graph only) ---

	// []int, []int8..[]uint32, []bool, []char - returns TArray<int> (int32)
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static TArray<int32> GetInt32ArrayResult(const FUEmkaScriptParam& Result);

	// []uint - returns TArray<int64>
	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static TArray<int64> GetIntArrayResult(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static TArray<double> GetRealArrayResult(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static TArray<float> GetReal32ArrayResult(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Category = "UEmka", Meta = (BlueprintInternalUseOnly = true))
	static TArray<FString> GetStrArrayResult(const FUEmkaScriptParam& Result);
};
