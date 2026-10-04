// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "umka_api.h"

class FProperty;

namespace UEmkaComposite
{
	bool EncodeProperty(const FProperty* Property, const void* Data, TArray<uint8>& Out, FString& Error);
	bool DecodeProperty(const TArray<uint8>& Bytes, FProperty* Property, void* Data, FString& Error);
	bool WriteUmka(Umka* VM, const UmkaType* Type, const TArray<uint8>& Bytes, void* NativeData, FString& Error);
	bool ReadUmka(const UmkaType* Type, const void* NativeData, TArray<uint8>& Out, FString& Error);
}
