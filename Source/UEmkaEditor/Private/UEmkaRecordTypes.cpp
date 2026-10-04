// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaRecordTypes.h"

#include "EdGraphSchema_K2.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/SecureHash.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UEmkaFunctionLibrary.h"
#include "UObject/Package.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace
{
constexpr int32 MaxTypeDepth = 16;

FEdGraphPinType ScalarPinType(const EUEmkaValueType Type)
{
	FEdGraphPinType PinType;
	switch (Type)
	{
		case EUEmkaValueType::Int:
		case EUEmkaValueType::UInt:
		case EUEmkaValueType::UInt32: PinType.PinCategory = UEdGraphSchema_K2::PC_Int64; break;
		case EUEmkaValueType::Int8:
		case EUEmkaValueType::Int16:
		case EUEmkaValueType::Int32:
		case EUEmkaValueType::UInt16: PinType.PinCategory = UEdGraphSchema_K2::PC_Int; break;
		case EUEmkaValueType::UInt8:
		case EUEmkaValueType::Char:
		case EUEmkaValueType::Enum: PinType.PinCategory = UEdGraphSchema_K2::PC_Byte; break;
		case EUEmkaValueType::Bool: PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; break;
		case EUEmkaValueType::Real:
		case EUEmkaValueType::Real32:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = Type == EUEmkaValueType::Real ? UEdGraphSchema_K2::PC_Double : UEdGraphSchema_K2::PC_Float;
			break;
		case EUEmkaValueType::Str: PinType.PinCategory = UEdGraphSchema_K2::PC_String; break;
		default: break;
	}
	return PinType;
}

bool AppendShape(const FUEmkaCompiledValue& Value, FString& Shape, FString& OutError, const int32 Depth)
{
	if (Depth > MaxTypeDepth)
	{
		OutError = TEXT("Umka record types cannot nest more than 16 levels");
		return false;
	}
	Shape += FString::Printf(TEXT("%d:%d:%d:%d["), static_cast<int32>(Value.Type), Value.bIsArray, Value.bIsMap, Value.bIsStruct);
	for (const FUEmkaCompiledValue& Field : Value.Fields)
	{
		Shape += FString::Printf(TEXT("%d:%s="), Field.Name.Len(), *Field.Name);
		if (!AppendShape(Field, Shape, OutError, Depth + 1)) return false;
	}
	Shape += TEXT("]");
	return true;
}

FEdGraphPinType ResolvePinType(const FUEmkaCompiledValue& Value, UObject* Owner, FString& OutError, const int32 Depth)
{
	if (Depth > MaxTypeDepth || !Value.bSupported)
	{
		OutError = Depth > MaxTypeDepth ? TEXT("Umka record types cannot nest more than 16 levels")
			: FString::Printf(TEXT("Unsupported Umka pin type '%s'"), *Value.TypeName);
		return {};
	}
	if (Value.bIsArray)
	{
		FEdGraphPinType ElementType;
		if (Value.Fields.Num() == 1)
		{
			ElementType = ResolvePinType(Value.Fields[0], Owner, OutError, Depth + 1);
		}
		else
		{
			ElementType = ScalarPinType(Value.Type);
		}
		if (!OutError.IsEmpty()) return {};
		if (ElementType.PinCategory.IsNone() || ElementType.ContainerType != EPinContainerType::None)
		{
			OutError = TEXT("Blueprint arrays require a scalar or record element type");
			return {};
		}
		ElementType.ContainerType = EPinContainerType::Array;
		return ElementType;
	}
	if (Value.bIsMap)
	{
		if (Value.Fields.Num() != 2 || (Value.Fields[0].Type > EUEmkaValueType::UInt && Value.Fields[0].Type != EUEmkaValueType::Str))
		{
			OutError = TEXT("Blueprint maps require Umka string or integer keys");
			return {};
		}
		FEdGraphPinType KeyType = ResolvePinType(Value.Fields[0], Owner, OutError, Depth + 1);
		if (!OutError.IsEmpty()) return {};
		const FEdGraphPinType ValueType = ResolvePinType(Value.Fields[1], Owner, OutError, Depth + 1);
		if (!OutError.IsEmpty()) return {};
		if (KeyType.ContainerType != EPinContainerType::None || ValueType.ContainerType != EPinContainerType::None || Value.Fields[1].bIsStruct)
		{
			OutError = TEXT("Blueprint maps require scalar keys and values");
			return {};
		}
		KeyType.ContainerType = EPinContainerType::Map;
		KeyType.PinValueType.TerminalCategory = ValueType.PinCategory;
		KeyType.PinValueType.TerminalSubCategory = ValueType.PinSubCategory;
		KeyType.PinValueType.TerminalSubCategoryObject = ValueType.PinSubCategoryObject;
		return KeyType;
	}
	if (!Value.bIsStruct)
	{
		FEdGraphPinType PinType = ScalarPinType(Value.Type);
		if (PinType.PinCategory.IsNone()) OutError = FString::Printf(TEXT("Unsupported Umka scalar type '%s'"), *Value.TypeName);
		return PinType;
	}
	if (!Owner || Value.Fields.IsEmpty())
	{
		OutError = TEXT("Blueprint record types require an owning package and at least one field");
		return {};
	}

	FString Shape;
	if (!AppendShape(Value, Shape, OutError, Depth)) return {};
	const auto ShapeUtf8 = StringCast<UTF8CHAR>(*Shape);
	uint8 Hash[FSHA1::DigestSize];
	FSHA1::HashBuffer(ShapeUtf8.Get(), ShapeUtf8.Length(), Hash);
	const FName StructName(*FString::Printf(TEXT("__UEmkaRecord_%s"), *BytesToHex(Hash, UE_ARRAY_COUNT(Hash))));
	UObject* Package = Owner->GetOutermost();
	UUserDefinedStruct* Struct = FindObject<UUserDefinedStruct>(Package, *StructName.ToString());
	if (!Struct)
	{
		TArray<FEdGraphPinType> FieldTypes;
		TMap<FName, FString> FieldNames;
		for (const FUEmkaCompiledValue& Field : Value.Fields)
		{
			if (Field.Name.IsEmpty() || !FName::IsValidXName(Field.Name, INVALID_OBJECTNAME_CHARACTERS))
			{
				OutError = FString::Printf(TEXT("Invalid Umka record field name '%s'"), *Field.Name);
				return {};
			}
			if (const FString* ExistingName = FieldNames.Find(FName(*Field.Name)))
			{
				OutError = FString::Printf(TEXT("Umka record field names '%s' and '%s' conflict because Blueprint names are case-insensitive"), **ExistingName, *Field.Name);
				return {};
			}
			FieldNames.Add(FName(*Field.Name), Field.Name);
			FieldTypes.Add(ResolvePinType(Field, Owner, OutError, Depth + 1));
			if (!OutError.IsEmpty()) return {};
		}

		// Package exports survive cooking even when the owning Blueprint's editor data is stripped.
		Struct = FStructureEditorUtils::CreateUserDefinedStruct(Package, StructName, RF_Public | RF_Transactional);
		if (!Struct)
		{
			OutError = TEXT("User-defined Blueprint structures are disabled");
			return {};
		}
		TArray<FStructVariableDescription>& Descriptions = FStructureEditorUtils::GetVarDesc(Struct);
		Descriptions.Reset(Value.Fields.Num());
		for (int32 Index = 0; Index < Value.Fields.Num(); ++Index)
		{
			FStructVariableDescription& Description = Descriptions.AddDefaulted_GetRef();
			Description.VarGuid = FGuid::NewGuid();
			Description.FriendlyName = Value.Fields[Index].Name;
			// The suffix lets UUserDefinedStruct recover authored names without editor-only metadata.
			Description.VarName = FName(*FString::Printf(TEXT("%s_%d_%s"), *Description.FriendlyName, Index, *Description.VarGuid.ToString(EGuidFormats::Digits)));
			Description.SetPinType(FieldTypes[Index]);
		}
		Struct->Status = EUserDefinedStructureStatus::UDSS_Dirty;
		FStructureEditorUtils::CompileStructure(Struct);
		Struct->MarkPackageDirty();
	}
	if (Struct->Status != EUserDefinedStructureStatus::UDSS_UpToDate)
	{
		OutError = FString::Printf(TEXT("Could not compile Blueprint record type '%s': %s"), *Value.TypeName, *Struct->ErrorMessage);
		return {};
	}
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	PinType.PinSubCategoryObject = Struct;
	return PinType;
}
}

FEdGraphPinType UEmkaRecordTypes::GetPinType(const FUEmkaCompiledValue& Value, UObject* Owner, FString& OutError)
{
	OutError.Reset();
	return ResolvePinType(Value, Owner, OutError, 0);
}
