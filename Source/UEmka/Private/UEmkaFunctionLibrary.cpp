// Copyright Solessfir. All Rights Reserved.

#include "UEmkaFunctionLibrary.h"

bool UUEmkaHardwareFunctionLibrary::RunUmka(FUmkaContext& UmkaContext, FString& Error)
{
	if (UmkaContext.FunctionName.IsEmpty())
	{
		Error = "No Function Name provided";
		return false;
	}

	if (UmkaContext.Code.IsEmpty())
	{
		Error = "No Code provided";
		return false;
	}

	Umka* umka = umkaAlloc();
	check(umka)

	constexpr int32 Megabyte = 1024 * 1024;
	bool bSuccess = umkaInit(umka, "dummy.um", TCHAR_TO_UTF8(*UmkaContext.Code), Megabyte, nullptr, 0, nullptr, false, false, nullptr);
	if (!bSuccess)
	{
		Error = "Umka failed to initialize";
		return false;
	}

	bSuccess = umkaCompile(umka);
	if (!bSuccess)
	{
		const UmkaError* UmkaError = umkaGetError(umka);
		UE_LOG(LogTemp, Warning, TEXT("%hs:%d: %hs"), UmkaError->fileName, UmkaError->line, UmkaError->msg)
		Error = FString::Format(TEXT("{0}:{1}: {2}"), {UmkaError->fileName, UmkaError->line, UmkaError->msg});
		return false;
	}

	UmkaFuncContext Context;
	bSuccess = umkaGetFunc(umka, nullptr, TCHAR_TO_UTF8(*UmkaContext.FunctionName), &Context);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("Umka failed to get `%s` function"), *UmkaContext.FunctionName)
		Error = FString::Format(TEXT("Umka failed to get `{0}` function"), {UmkaContext.FunctionName});
		return false;
	}

	for (int32 Index = 0; Index < UmkaContext.IntParams.Num(); ++Index)
	{
		umkaGetParam(Context.params, Index)->intVal = UmkaContext.IntParams[Index];
	}

	UmkaContext = FUmkaContext(Context);

	bSuccess = umkaCall(umka, &Context) == 0;
	umkaFree(umka);
	return bSuccess;
}

FUmkaContext UUEmkaHardwareFunctionLibrary::CreateUmkaIntContext(const FString& FunctionName, const FString& Code, const TArray<int32>& Params)
{
	return CreateUmkaContext<int32>(FunctionName, Code, Params);
}

FUmkaContext UUEmkaHardwareFunctionLibrary::CreateUmkaFloatContext(const FString& FunctionName, const FString& Code, const TArray<float>& Params)
{
	return CreateUmkaContext<float>(FunctionName, Code, Params);
}

FUmkaContext UUEmkaHardwareFunctionLibrary::CreateUmkaDoubleContext(const FString& FunctionName, const FString& Code, const TArray<double>& Params)
{
	return CreateUmkaContext<double>(FunctionName, Code, Params);
}

bool UUEmkaHardwareFunctionLibrary::SetUmkaIntParam(FUmkaContext& UmkaContext, const int32 ParamIndex, const int32 Int)
{
	if (ParamIndex < 0)
	{
		return false;
	}

	if (UmkaContext.IntParams.IsValidIndex(ParamIndex))
	{
		UmkaContext.IntParams[ParamIndex] = Int;
		return true;
	}

	UmkaContext.IntParams.Add(Int);
	return true;
}

bool UUEmkaHardwareFunctionLibrary::SetUmkaFloatParam(FUmkaContext& UmkaContext, const int32 ParamIndex, const float Float)
{
	if (!UmkaContext.Params || ParamIndex < 0)
	{
		return false;
	}

	UmkaStackSlot* Slot = umkaGetParam(UmkaContext.Params, ParamIndex);
	if (!Slot)
	{
		return false;
	}

	Slot->real32Val = Float;
	return true;
}

bool UUEmkaHardwareFunctionLibrary::SetUmkaDoubleParam(FUmkaContext& UmkaContext, const int32 ParamIndex, const double Double)
{
	if (!UmkaContext.Params || ParamIndex < 0)
	{
		return false;
	}

	UmkaStackSlot* Slot = umkaGetParam(UmkaContext.Params, ParamIndex);
	if (!Slot)
	{
		return false;
	}

	Slot->realVal = Double;
	return true;
}

int32 UUEmkaHardwareFunctionLibrary::Conv_UmkaContextToInt(const FUmkaContext& UmkaContext)
{
	return UmkaContext.Result ? UmkaContext.Result->intVal : 0;
}

float UUEmkaHardwareFunctionLibrary::Conv_UmkaContextToFloat(const FUmkaContext& UmkaContext)
{
	return UmkaContext.Result ? UmkaContext.Result->real32Val : 0.f;
}

double UUEmkaHardwareFunctionLibrary::Conv_UmkaContextToDouble(const FUmkaContext& UmkaContext)
{
	return UmkaContext.Result ? UmkaContext.Result->realVal : 0.0;
}
