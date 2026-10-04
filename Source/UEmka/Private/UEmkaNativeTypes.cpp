// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaNativeTypes.h"

#include "UEmkaFunctionLibrary.h"
#include "UObject/NoExportTypes.h"

FString UEmkaNativeTypes::GetModuleSource()
{
	return TEXT("type Vector* = struct { X, Y, Z: real }\n")
		TEXT("type Rotator* = struct { Pitch, Yaw, Roll: real }\n")
		TEXT("type LinearColor* = struct { R, G, B, A: real32 }\n")
		TEXT("type Quat* = struct { X, Y, Z, W: real }\n")
		TEXT("type Transform* = struct { Rotation: Quat; Translation, Scale3D: Vector }\n");
}

UScriptStruct* UEmkaNativeTypes::GetNativeStruct(const FString& Name)
{
	if (Name.Equals(TEXT("Vector"), ESearchCase::CaseSensitive)) return TBaseStructure<FVector>::Get();
	if (Name.Equals(TEXT("Rotator"), ESearchCase::CaseSensitive)) return TBaseStructure<FRotator>::Get();
	if (Name.Equals(TEXT("LinearColor"), ESearchCase::CaseSensitive)) return TBaseStructure<FLinearColor>::Get();
	if (Name.Equals(TEXT("Quat"), ESearchCase::CaseSensitive)) return TBaseStructure<FQuat>::Get();
	if (Name.Equals(TEXT("Transform"), ESearchCase::CaseSensitive)) return TBaseStructure<FTransform>::Get();
	return nullptr;
}

bool UEmkaNativeTypes::IsValidLayout(const FUEmkaCompiledValue& Value, const FString& Name)
{
	if (!Value.bSupported || Value.Type != EUEmkaValueType::Composite || !Value.bIsStruct
		|| Value.bIsArray || Value.bIsStaticArray || Value.bIsMap || Value.bIsTuple || Value.bIsEnum) return false;
	TArray<FString> Names;
	EUEmkaValueType ScalarType = EUEmkaValueType::Real;
	if (Name.Equals(TEXT("Vector"), ESearchCase::CaseSensitive)) Names = {TEXT("X"), TEXT("Y"), TEXT("Z")};
	else if (Name.Equals(TEXT("Rotator"), ESearchCase::CaseSensitive)) Names = {TEXT("Pitch"), TEXT("Yaw"), TEXT("Roll")};
	else if (Name.Equals(TEXT("Quat"), ESearchCase::CaseSensitive)) Names = {TEXT("X"), TEXT("Y"), TEXT("Z"), TEXT("W")};
	else if (Name.Equals(TEXT("LinearColor"), ESearchCase::CaseSensitive))
	{
		Names = {TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A")};
		ScalarType = EUEmkaValueType::Real32;
	}
	else if (Name.Equals(TEXT("Transform"), ESearchCase::CaseSensitive)) Names = {TEXT("Rotation"), TEXT("Translation"), TEXT("Scale3D")};
	else return false;
	if (Value.Fields.Num() != Names.Num()) return false;
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		const FUEmkaCompiledValue& Field = Value.Fields[Index];
		if (!Field.Name.Equals(Names[Index], ESearchCase::CaseSensitive)) return false;
		if (Name.Equals(TEXT("Transform"), ESearchCase::CaseSensitive))
		{
			if (!IsValidLayout(Field, Index == 0 ? TEXT("Quat") : TEXT("Vector"))) return false;
		}
		else if (!Field.bSupported || Field.Type != ScalarType || Field.bIsArray || Field.bIsStaticArray
			|| Field.bIsMap || Field.bIsStruct || Field.bIsTuple || Field.bIsEnum || !Field.Fields.IsEmpty()) return false;
	}
	return true;
}
