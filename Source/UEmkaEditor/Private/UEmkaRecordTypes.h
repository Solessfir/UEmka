// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphPin.h"

struct FUEmkaCompiledValue;

namespace UEmkaRecordTypes
{
FEdGraphPinType GetPinType(const FUEmkaCompiledValue& Value, UObject* Owner, FString& OutError);
bool GetDefaultValue(const FUEmkaCompiledValue& Value, const FEdGraphPinType& PinType, FString& OutValue, FString& OutError);
bool EncodeDefaultValue(const FEdGraphPinType& PinType, const FString& Value, TArray<uint8>& OutBytes, FString& OutError);
bool GetFieldDefaultValue(const FEdGraphPinType& ParentPinType, const FString& ParentText, const FName FieldName, FString& OutText, FString& OutError);
}
