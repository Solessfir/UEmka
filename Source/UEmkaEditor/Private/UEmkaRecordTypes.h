// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphPin.h"

struct FUEmkaCompiledValue;

namespace UEmkaRecordTypes
{
FEdGraphPinType GetPinType(const FUEmkaCompiledValue& Value, UObject* Owner, FString& OutError);
}
