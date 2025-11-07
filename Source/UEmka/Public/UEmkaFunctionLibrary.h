// Copyright Solessfir. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "umka_api.h"
#include "UEmkaFunctionLibrary.generated.h"

USTRUCT(BlueprintType)
struct FUmkaContext
{
	GENERATED_BODY()

	FUmkaContext() = default;

	explicit FUmkaContext(const FString& InFunctionName, const FString& InCode)
		: FunctionName(InFunctionName)
		, Code(InCode)
	{
	}

	explicit FUmkaContext(const UmkaFuncContext& InContext)
		: Params(InContext.params)
		, Result(InContext.result)
	{
	}

	UmkaStackSlot* Params = nullptr;
	UmkaStackSlot* Result = nullptr;

	FString FunctionName;
	FString Code;

	TArray<int32> IntParams;
	TArray<float> FloatParams;
	TArray<double> DoubleParams;
};

UCLASS()
class UUEmkaHardwareFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Meta = (AutoCreateRefTerm = "Code", MultiLine), Category = "UEmka")
	static bool RunUmka(UPARAM(Ref) FUmkaContext& UmkaContext, FString& Error);

	UFUNCTION(BlueprintCallable, Meta = (AutoCreateRefTerm = "FunctionName, Code, Params", MultiLine), Category = "UEmka")
	static FUmkaContext CreateUmkaIntContext(const FString& FunctionName, const FString& Code, const TArray<int32>& Params);

	UFUNCTION(BlueprintCallable, Meta = (AutoCreateRefTerm = "FunctionName, Code, Params", MultiLine), Category = "UEmka")
	static FUmkaContext CreateUmkaFloatContext(const FString& FunctionName, const FString& Code, const TArray<float>& Params);

	UFUNCTION(BlueprintCallable, Meta = (AutoCreateRefTerm = "FunctionName, Code, Params", MultiLine), Category = "UEmka")
	static FUmkaContext CreateUmkaDoubleContext(const FString& FunctionName, const FString& Code, const TArray<double>& Params);

	UFUNCTION(BlueprintCallable, Category = "UEmka")
	static bool SetUmkaIntParam(UPARAM(Ref) FUmkaContext& UmkaContext, const int32 ParamIndex, const int32 Int);

	UFUNCTION(BlueprintCallable, Category = "UEmka")
	static bool SetUmkaFloatParam(UPARAM(Ref) FUmkaContext& UmkaContext, const int32 ParamIndex, const float Float);

	UFUNCTION(BlueprintCallable, Category = "UEmka")
	static bool SetUmkaDoubleParam(UPARAM(Ref) FUmkaContext& UmkaContext, const int32 ParamIndex, const double Double);

	UFUNCTION(BlueprintPure, Meta = (BlueprintAutocast, DisplayName = "To Int (UmkaContext)", CompactNodeTitle = "->"), Category = "UEmka")
	static int32 Conv_UmkaContextToInt(const FUmkaContext& UmkaContext);

	UFUNCTION(BlueprintPure, Meta = (BlueprintAutocast, DisplayName = "To Float (UmkaContext)", CompactNodeTitle = "->"), Category = "UEmka")
	static float Conv_UmkaContextToFloat(const FUmkaContext& UmkaContext);

	UFUNCTION(BlueprintPure, Meta = (BlueprintAutocast, DisplayName = "To Double (UmkaContext)", CompactNodeTitle = "->"), Category = "UEmka")
	static double Conv_UmkaContextToDouble(const FUmkaContext& UmkaContext);

private:
	template<typename T>
	static FUmkaContext CreateUmkaContext(const FString& FunctionName, const FString& Code, const TArray<T>& Params)
	{
		FUmkaContext Context(FunctionName, Code);

		if constexpr (std::is_same_v<T, int32>)
		{
			Context.IntParams = Params;
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			Context.FloatParams = Params;
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			Context.DoubleParams = Params;
		}
		else
		{
			static_assert(!sizeof(T*), "Unsupported parameter type for UmkaContext. Supported: int32, float, double.");
		}

		return Context;
	}
};
