// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FUEmkaCompiledValue;
class UScriptStruct;

namespace UEmkaNativeTypes
{
UEMKA_API FString GetModuleSource();
UEMKA_API UScriptStruct* GetNativeStruct(const FString& Name);
UEMKA_API bool IsValidLayout(const FUEmkaCompiledValue& Value, const FString& Name);
}
