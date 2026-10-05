// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UEmkaScriptAsset.h"
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
	UInt32	UMETA(DisplayName = "uint32"),	// intVal, PC_Int64
	UInt	UMETA(DisplayName = "uint"),	// 64-bit unsigned, uintVal, PC_Int64
	Bool	UMETA(DisplayName = "bool"),	// intVal, PC_Boolean
	Char	UMETA(DisplayName = "char"),	// intVal, PC_Byte
	Real	UMETA(DisplayName = "real"),	// 64-bit float, realVal, PC_Double
	Real32	UMETA(DisplayName = "real32"),	// 32-bit float, real32Val (param) / realVal (result), PC_Float
	Str		UMETA(DisplayName = "str"),		// ptrVal (umkaMakeStr), PC_String
	Enum	UMETA(DisplayName = "enum"),	// Legacy enum marker; compiled signatures use the underlying integer type
	Void	UMETA(DisplayName = "void"),	// No return value
	Composite UMETA(Hidden),
};

struct UEMKA_API FUEmkaCompiledValue
{
	FString Name;
	FString TypeName;
	FString NativeStructName;
	EUEmkaValueType Type = EUEmkaValueType::Void;
	bool bSupported = false;
	bool bIsArray = false;
	bool bIsStaticArray = false;
	bool bIsEnum = false;
	bool bIsStruct = false;
	bool bIsTuple = false;
	bool bIsMap = false;
	bool bHasDefault = false;
	FString DefaultValue;
	TArray<uint8> DefaultCompositeValue;
	int32 ArrayLen = 0;
	int32 EnumByteSize = 8;
	TArray<FUEmkaCompiledValue> Fields;
	bool operator==(const FUEmkaCompiledValue& Other) const
	{
		return Name.Equals(Other.Name, ESearchCase::CaseSensitive)
			&& TypeName.Equals(Other.TypeName, ESearchCase::CaseSensitive)
			&& NativeStructName.Equals(Other.NativeStructName, ESearchCase::CaseSensitive)
			&& DefaultValue.Equals(Other.DefaultValue, ESearchCase::CaseSensitive)
			&& Type == Other.Type && bSupported == Other.bSupported
			&& bIsArray == Other.bIsArray && bIsStaticArray == Other.bIsStaticArray
			&& bIsEnum == Other.bIsEnum && bIsStruct == Other.bIsStruct && bIsTuple == Other.bIsTuple
			&& bIsMap == Other.bIsMap && bHasDefault == Other.bHasDefault
			&& DefaultCompositeValue == Other.DefaultCompositeValue
			&& ArrayLen == Other.ArrayLen && EnumByteSize == Other.EnumByteSize && Fields == Other.Fields;
	}
};

struct UEMKA_API FUEmkaCompiledSignature
{
	TArray<FUEmkaCompiledValue> Params;
	FUEmkaCompiledValue Result;
};

USTRUCT(BlueprintType)
struct FUEmkaExecutionOptions
{
	GENERATED_BODY()

	// Keep script globals between calls on this node and Blueprint instance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Umka", Meta = (DisplayName = "Preserve Script State"))
	bool bUseSession = false;

	// Maximum script instructions per call. Zero disables the instruction limit.
	// Each call starts a new budget.
	// Does not interrupt blocked native callbacks or built-in functions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Umka", Meta = (ClampMin = "0", UIMin = "0"))
	int64 MaxInstructions = 0;

	// Maximum memory reserved by the script, in bytes. Zero disables this memory limit.
	// Includes retained allocations and fiber stacks; excludes compiler and Unreal memory.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Umka", Meta = (ClampMin = "0", UIMin = "0"))
	int64 MaxHeapBytes = 0;

	// Start this call with fresh script globals, even when state preservation is enabled.
	UPROPERTY(BlueprintReadWrite, Category = "Umka", Meta = (DisplayName = "Reset Script State"))
	bool bResetSession = false;
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

	// Fixed-size [N]T arrays use inline Umka storage instead of a dynamic-array header.
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	bool bIsStaticArray = false;

	// Array storage - only one is populated, matching Type
	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<int64> IntArrayValue;

	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<double> RealArrayValue;

	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<float> Real32ArrayValue;

	UPROPERTY(BlueprintReadWrite, Category = "UEmka")
	TArray<FString> StringArrayValue;

	UPROPERTY()
	TArray<uint8> CompositeValue;
};

UCLASS()
class UEMKA_API UUEmkaFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller", AutoCreateRefTerm = "SessionId,Options"), Category = "UEmka")
	static bool RunUmkaInlineConfigured(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const EUEmkaValueType ResultType, const bool bResultIsArray, const bool bResultIsStaticArray, FUEmkaScriptParam& Result, FString& Error, const FGuid& SessionId, const FUEmkaExecutionOptions& Options);

	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller", AutoCreateRefTerm = "SessionId,Options"), Category = "UEmka")
	static bool RunUmkaInlineMultiConfigured(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const FString& ResultTypes, TArray<FUEmkaScriptParam>& Results, FString& Error, const FGuid& SessionId, const FUEmkaExecutionOptions& Options);

	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller", AutoCreateRefTerm = "SessionId,Options"), Category = "UEmka")
	static bool RunUmkaAssetConfigured(UObject* Caller, UUEmkaScriptAsset* Asset, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const EUEmkaValueType ResultType, const bool bResultIsArray, const bool bResultIsStaticArray, FUEmkaScriptParam& Result, FString& Error, const FGuid& SessionId, const FUEmkaExecutionOptions& Options);

	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller", AutoCreateRefTerm = "SessionId,Options"), Category = "UEmka")
	static bool RunUmkaAssetMultiConfigured(UObject* Caller, UUEmkaScriptAsset* Asset, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const FString& ResultTypes, TArray<FUEmkaScriptParam>& Results, FString& Error, const FGuid& SessionId, const FUEmkaExecutionOptions& Options);

	UFUNCTION(BlueprintCallable, Category = "UEmka|Execution", Meta = (DefaultToSelf = "Caller"))
	static bool ResetRuntimeSession(UObject* Caller, const FGuid& SessionId);

	UFUNCTION(BlueprintCallable, Category = "UEmka|Execution", Meta = (DefaultToSelf = "Caller"))
	static int32 ResetRuntimeSessionsForCaller(UObject* Caller);

	// May be requested from another thread; the VM observes it at dispatch boundaries.
	static bool CancelExecution(UObject* Caller, const FGuid& SessionId);
	UFUNCTION(BlueprintCallable, Category = "UEmka|Execution")
	static void ResetAllRuntimeSessions();
	static void CleanupInvalidRuntimeSessions();

	// Used by UK2Node_UEmka ExpandNode only. Executes a script inline with ordered typed parameters.
	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller"), Category = "UEmka")
	static bool RunUmkaInline(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const EUEmkaValueType ResultType, const bool bResultIsArray, const bool bResultIsStaticArray, FUEmkaScriptParam& Result, FString& Error);

	// Used by UK2Node_UEmka ExpandNode only for multi-return functions (fn foo*(): (int, str)).
	// ResultTypes is a comma-separated list of type:arrayKind:enumByteSize triples.
	// arrayKind is 0 for scalar, 1 for []T, and 2 for [N]T. The third field preserves
	// explicit enum bases for compatibility; runtime layout comes from compiled type metadata.
	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller"), Category = "UEmka")
	static bool RunUmkaInlineMulti(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const FString& ResultTypes, TArray<FUEmkaScriptParam>& Results, FString& Error);

	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller"), Category = "UEmka")
	static bool RunUmkaAsset(UObject* Caller, UUEmkaScriptAsset* Asset, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const EUEmkaValueType ResultType, const bool bResultIsArray, const bool bResultIsStaticArray, FUEmkaScriptParam& Result, FString& Error);

	UFUNCTION(BlueprintCallable, Meta = (BlueprintInternalUseOnly = true, DefaultToSelf = "Caller", HidePin = "Caller"), Category = "UEmka")
	static bool RunUmkaAssetMulti(UObject* Caller, UUEmkaScriptAsset* Asset, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const FString& ResultTypes, TArray<FUEmkaScriptParam>& Results, FString& Error);

	// Index into the multi-return Results array from RunUmkaInlineMulti.
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam GetMultiResultAt(const TArray<FUEmkaScriptParam>& Results, const int32 Index);

	// Compile-only check - no execution. Used by UK2Node_UEmka for static validation.
	// Returns false and populates OutError / OutLine (1-based) on failure.
	static bool CompileCheckScript(const FString& Script, FString& OutError, int32& OutLine, const TArray<FUEmkaModuleSource>& Modules = {}, const FString& FileName = TEXT("script.um"), const bool bAllowFileImports = true);

	// Compile-only signature inspection. Unsupported shapes are reported through bSupported.
	static bool InspectScriptFunction(const FString& Script, const FString& FunctionName, FUEmkaCompiledSignature& OutSignature, const TArray<FUEmkaModuleSource>& Modules = {}, const FString& FileName = TEXT("script.um"), const bool bAllowFileImports = true);

	UFUNCTION(BlueprintPure, CustomThunk, Meta = (BlueprintInternalUseOnly = true, CustomStructureParam = "Value"), Category = "UEmka")
	static FUEmkaScriptParam MakeCompositeParam(const int32& Value, const bool bIsStaticArray);
	DECLARE_FUNCTION(execMakeCompositeParam);

	UFUNCTION(BlueprintPure, CustomThunk, Meta = (BlueprintInternalUseOnly = true, CustomStructureParam = "Value"), Category = "UEmka")
	static void GetCompositeResult(const FUEmkaScriptParam& Result, int32& Value);
	DECLARE_FUNCTION(execGetCompositeResult);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeCompositeDefaultParam(const FString& Value, const bool bIsArray, const bool bIsStaticArray);

	static bool EncodeComposite(const FProperty* Property, const void* Value, FUEmkaScriptParam& Out, FString& Error, const bool bIsStaticArray = false);
	static bool DecodeComposite(const FUEmkaScriptParam& Result, FProperty* Property, void* Value, FString& Error);

	// --- Param construction helpers (ExpandNode intermediate graph only) ---

	// Covers: int, int8, int16, int32, uint8, uint16, uint32, uint, char
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeIntParam(const EUEmkaValueType Type, const int64 Value);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeBoolParam(const bool Value);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeRealParam(const double Value);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeReal32Param(const float Value);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeStrParam(const FString& Value);

	// --- Result extraction helpers (ExpandNode intermediate graph only) ---

	// Covers int, uint, and uint32 (int64 output pin)
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static int64 GetIntResult(const FUEmkaScriptParam& Result);

	// Covers int8..int32, uint8..uint16, bool, char (int32 output pin)
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static int32 GetInt32Result(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static double GetRealResult(const FUEmkaScriptParam& Result);

	// real32 result comes back in realVal (not real32Val), returns as float
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static float GetReal32Result(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FString GetStrResult(const FUEmkaScriptParam& Result);

	// --- Array param construction helpers (ExpandNode intermediate graph only) ---

	// []int8, []int16, []int32, []uint16 - BP pin is TArray<int> (int32)
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeIntArrayParam(const EUEmkaValueType Type, const TArray<int32>& Values, const bool bIsStaticArray);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeBoolArrayParam(const TArray<bool>& Values, const bool bIsStaticArray);

	// []uint8, []char, []MyEnum - BP pin is TArray<uint8> (Byte), matching the node's Array of Byte pin
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeByteArrayParam(const EUEmkaValueType Type, const TArray<uint8>& Values, const bool bIsStaticArray);

	// []int, []uint, []uint32 - BP pin is TArray<int64>
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeInt64ArrayParam(const EUEmkaValueType Type, const TArray<int64>& Values, const bool bIsStaticArray);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeRealArrayParam(const TArray<double>& Values, const bool bIsStaticArray);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeReal32ArrayParam(const TArray<float>& Values, const bool bIsStaticArray);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static FUEmkaScriptParam MakeStrArrayParam(const TArray<FString>& Values, const bool bIsStaticArray);

	// --- Array result extraction helpers (ExpandNode intermediate graph only) ---

	// []int8, []int16, []int32, []uint16 - returns TArray<int> (int32)
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static TArray<int32> GetInt32ArrayResult(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static TArray<bool> GetBoolArrayResult(const FUEmkaScriptParam& Result);

	// []uint8, []char, []MyEnum - returns TArray<uint8> (Byte)
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static TArray<uint8> GetByteArrayResult(const FUEmkaScriptParam& Result);

	// []int, []uint, []uint32 - returns TArray<int64>
	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static TArray<int64> GetIntArrayResult(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static TArray<double> GetRealArrayResult(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static TArray<float> GetReal32ArrayResult(const FUEmkaScriptParam& Result);

	UFUNCTION(BlueprintPure, Meta = (BlueprintInternalUseOnly = true), Category = "UEmka")
	static TArray<FString> GetStrArrayResult(const FUEmkaScriptParam& Result);
};
