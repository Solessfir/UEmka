// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaComposite.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/EnumProperty.h"
#include "UObject/NoExportTypes.h"
#include "UObject/StrProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 MaxDepth = 16;
	constexpr int32 MaxItems = 65536;
	constexpr int32 MaxBytes = 64 * 1024 * 1024;
	constexpr int32 MaxNodes = 262144;

	enum class EKind : uint8 { Signed, Unsigned, Real, String, Array, Struct, Map };

	struct FValue
	{
		EKind Kind = EKind::Signed;
		uint64 Integer = 0;
		double Real = 0;
		FString String;
		TArray<FString> Names;
		TArray<FValue> Children;
	};

	bool Fail(FString& Error, const TCHAR* Message)
	{
		Error = Message;
		return false;
	}

	bool Enter(const int32 Depth, int32& Nodes, FString& Error)
	{
		return (Depth <= MaxDepth && ++Nodes <= MaxNodes) || Fail(Error, TEXT("Composite value exceeds the nesting or item limit."));
	}

	bool IsInteger(const FValue& Value)
	{
		return Value.Kind == EKind::Signed || Value.Kind == EKind::Unsigned;
	}

	bool IntegerFits(const FValue& Value, const int32 Size, const bool bUnsigned)
	{
		if (!IsInteger(Value) || Size < 1 || Size > 8)
		{
			return false;
		}
		if (bUnsigned)
		{
			if (Size == 8) return true;
			if (Value.Kind == EKind::Signed && static_cast<int64>(Value.Integer) < 0)
			{
				return false;
			}
			return Size == 8 || Value.Integer < (uint64(1) << (Size * 8));
		}
		const uint64 Maximum = Size == 8 ? MAX_int64 : (uint64(1) << (Size * 8 - 1)) - 1;
		if (Value.Kind == EKind::Unsigned)
		{
			return Value.Integer <= Maximum;
		}
		const int64 Signed = static_cast<int64>(Value.Integer);
		return Signed >= -static_cast<int64>(Maximum) - 1 && Signed <= static_cast<int64>(Maximum);
	}

	bool ReflectedUnsigned(const FNumericProperty* Property)
	{
		return Property->IsA<FByteProperty>() || Property->IsA<FUInt16Property>() || Property->IsA<FUInt32Property>() || Property->IsA<FUInt64Property>();
	}

	template<typename T> T Load(const void* Data)
	{
		T Result;
		FMemory::Memcpy(&Result, Data, sizeof(T));
		return Result;
	}

	template<typename T> void Store(void* Data, const T Value)
	{
		FMemory::Memcpy(Data, &Value, sizeof(T));
	}

	TUniquePtr<FStructProperty> MathProperty(UScriptStruct* Struct)
	{
		TUniquePtr<FStructProperty> Property = MakeUnique<FStructProperty>(nullptr, NAME_None);
		Property->Struct = Struct;
		Property->SetElementSize(Struct->GetStructureSize());
		return Property;
	}

	bool FromProperty(const FProperty* Property, const void* Data, FValue& Value, FString& Error, int32 Depth, int32& Nodes, bool bSingle = false)
	{
		if (!Property || !Data || !Enter(Depth, Nodes, Error))
		{
			return Fail(Error, TEXT("Invalid reflected composite value or nesting limit."));
		}
		if (!bSingle && Property->ArrayDim > 1)
		{
			Value.Kind = EKind::Array;
			if (Property->ArrayDim > MaxItems) return Fail(Error, TEXT("Static array is too large."));
			Value.Children.SetNum(Property->ArrayDim);
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				if (!FromProperty(Property, static_cast<const uint8*>(Data) + Index * Property->GetElementSize(), Value.Children[Index], Error, Depth + 1, Nodes, true)) return false;
			return true;
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
			return FromProperty(Enum->GetUnderlyingProperty(), Data, Value, Error, Depth + 1, Nodes);
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			Value.Integer = Bool->GetPropertyValue(Data);
			return true;
		}
		if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
		{
			if (Number->IsFloatingPoint())
			{
				Value.Kind = EKind::Real;
				Value.Real = Number->GetFloatingPointPropertyValue(Data);
			}
			else
			{
				Value.Kind = ReflectedUnsigned(Number) ? EKind::Unsigned : EKind::Signed;
				Value.Integer = ReflectedUnsigned(Number) ? Number->GetUnsignedIntPropertyValue(Data) : static_cast<uint64>(Number->GetSignedIntPropertyValue(Data));
			}
			return true;
		}
		Value.Kind = EKind::String;
		if (const FStrProperty* String = CastField<FStrProperty>(Property)) Value.String = String->GetPropertyValue(Data);
		else if (const FNameProperty* Name = CastField<FNameProperty>(Property)) Value.String = Name->GetPropertyValue(Data).ToString();
		else if (const FTextProperty* Text = CastField<FTextProperty>(Property)) Value.String = Text->GetPropertyValue(Data).ToString();
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			Value.Kind = EKind::Array;
			FScriptArrayHelper Helper(Array, Data);
			if (Helper.Num() > MaxItems) return Fail(Error, TEXT("Array is too large."));
			Value.Children.SetNum(Helper.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
				if (!FromProperty(Array->Inner, Helper.GetRawPtr(Index), Value.Children[Index], Error, Depth + 1, Nodes)) return false;
		}
		else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			Value.Kind = EKind::Struct;
			Value.String = Struct->Struct->GetName();
			if (Struct->Struct == TBaseStructure<FTransform>::Get())
			{
				// FTransform uses SIMD storage; its public accessors preserve the logical field layout.
				const FTransform& Transform = *static_cast<const FTransform*>(Data);
				const FQuat Rotation = Transform.GetRotation();
				const FVector Translation = Transform.GetTranslation();
				const FVector Scale = Transform.GetScale3D();
				Value.Names = {TEXT("Rotation"), TEXT("Translation"), TEXT("Scale3D")};
				Value.Children.SetNum(3);
				const TUniquePtr<FStructProperty> Quat = MathProperty(TBaseStructure<FQuat>::Get());
				const TUniquePtr<FStructProperty> Vector = MathProperty(TBaseStructure<FVector>::Get());
				return FromProperty(Quat.Get(), &Rotation, Value.Children[0], Error, Depth + 1, Nodes)
					&& FromProperty(Vector.Get(), &Translation, Value.Children[1], Error, Depth + 1, Nodes)
					&& FromProperty(Vector.Get(), &Scale, Value.Children[2], Error, Depth + 1, Nodes);
			}
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				if (Value.Children.Num() >= MaxItems) return Fail(Error, TEXT("Struct has too many fields."));
				Value.Names.Add(It->GetAuthoredName());
				FValue& Child = Value.Children.AddDefaulted_GetRef();
				if (!FromProperty(*It, It->ContainerPtrToValuePtr<void>(Data), Child, Error, Depth + 1, Nodes)) return false;
			}
		}
		else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
		{
			Value.Kind = EKind::Map;
			FScriptMapHelper Helper(Map, Data);
			if (Helper.Num() > MaxItems) return Fail(Error, TEXT("Map is too large."));
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index)) continue;
				FValue& Key = Value.Children.AddDefaulted_GetRef();
				if (!FromProperty(Map->KeyProp, Helper.GetKeyPtr(Index), Key, Error, Depth + 1, Nodes)) return false;
				FValue& Item = Value.Children.AddDefaulted_GetRef();
				if (!FromProperty(Map->ValueProp, Helper.GetValuePtr(Index), Item, Error, Depth + 1, Nodes)) return false;
			}
		}
		else return Fail(Error, TEXT("Unsupported reflected property in composite value."));
		return true;
	}

	bool ToProperty(const FValue& Value, FProperty* Property, void* Data, FString& Error, bool bSingle = false)
	{
		if (!bSingle && Property->ArrayDim > 1)
		{
			if (Value.Kind != EKind::Array || Value.Children.Num() != Property->ArrayDim) return Fail(Error, TEXT("Static array length does not match."));
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				if (!ToProperty(Value.Children[Index], Property, static_cast<uint8*>(Data) + Index * Property->GetElementSize(), Error, true)) return false;
			return true;
		}
		if (FEnumProperty* Enum = CastField<FEnumProperty>(Property)) return ToProperty(Value, Enum->GetUnderlyingProperty(), Data, Error);
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			if (!IsInteger(Value) || Value.Integer > 1) return Fail(Error, TEXT("Boolean must be zero or one."));
			Bool->SetPropertyValue(Data, Value.Integer != 0);
			return true;
		}
		if (FNumericProperty* Number = CastField<FNumericProperty>(Property))
		{
			if (Number->IsFloatingPoint())
			{
				if (Value.Kind != EKind::Real || (Number->GetElementSize() == 4 && FMath::IsFinite(Value.Real) && !FMath::IsFinite(static_cast<float>(Value.Real)))) return Fail(Error, TEXT("Floating-point value has the wrong type or exceeds the property range."));
				Number->SetFloatingPointPropertyValue(Data, Value.Real);
			}
			else
			{
				// Blueprint int64 also carries Umka uint's full bit pattern.
				if (!(Number->GetElementSize() == 8 && IsInteger(Value)) && !IntegerFits(Value, Number->GetElementSize(), ReflectedUnsigned(Number))) return Fail(Error, TEXT("Integer exceeds the reflected property range."));
				Number->SetIntPropertyValue(Data, Value.Integer);
			}
			return true;
		}
		if (Value.Kind == EKind::String)
		{
			if (FStrProperty* String = CastField<FStrProperty>(Property)) String->SetPropertyValue(Data, Value.String);
			else if (FNameProperty* Name = CastField<FNameProperty>(Property)) Name->SetPropertyValue(Data, FName(*Value.String));
			else if (FTextProperty* Text = CastField<FTextProperty>(Property)) Text->SetPropertyValue(Data, FText::FromString(Value.String));
			else return Fail(Error, TEXT("String does not match the reflected property."));
			return true;
		}
		if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			if (Value.Kind != EKind::Array) return Fail(Error, TEXT("Expected an array."));
			FScriptArrayHelper Helper(Array, Data);
			Helper.Resize(Value.Children.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
				if (!ToProperty(Value.Children[Index], Array->Inner, Helper.GetRawPtr(Index), Error)) return false;
			return true;
		}
		if (FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			if (Value.Kind != EKind::Struct) return Fail(Error, TEXT("Expected a struct."));
			if (Struct->Struct == TBaseStructure<FTransform>::Get())
			{
				if (Value.Children.Num() != 3) return Fail(Error, TEXT("Composite transform must contain Rotation, Translation and Scale3D."));
				const int32 RotationIndex = Value.Names.IndexOfByPredicate([](const FString& Name) { return Name.Equals(TEXT("Rotation"), ESearchCase::CaseSensitive); });
				const int32 TranslationIndex = Value.Names.IndexOfByPredicate([](const FString& Name) { return Name.Equals(TEXT("Translation"), ESearchCase::CaseSensitive); });
				const int32 ScaleIndex = Value.Names.IndexOfByPredicate([](const FString& Name) { return Name.Equals(TEXT("Scale3D"), ESearchCase::CaseSensitive); });
				if (RotationIndex == INDEX_NONE || TranslationIndex == INDEX_NONE || ScaleIndex == INDEX_NONE) return Fail(Error, TEXT("Composite transform is missing a native field."));
				FQuat Rotation = FQuat::Identity;
				FVector Translation = FVector::ZeroVector;
				FVector Scale = FVector::OneVector;
				const TUniquePtr<FStructProperty> Quat = MathProperty(TBaseStructure<FQuat>::Get());
				const TUniquePtr<FStructProperty> Vector = MathProperty(TBaseStructure<FVector>::Get());
				if (!ToProperty(Value.Children[RotationIndex], Quat.Get(), &Rotation, Error)
					|| !ToProperty(Value.Children[TranslationIndex], Vector.Get(), &Translation, Error)
					|| !ToProperty(Value.Children[ScaleIndex], Vector.Get(), &Scale, Error)) return false;
				*static_cast<FTransform*>(Data) = FTransform(Rotation, Translation, Scale);
				return true;
			}
			int32 Count = 0;
			for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
			{
				const FString Name = It->GetAuthoredName();
				const int32 Index = Value.Names.IndexOfByPredicate([&Name](const FString& FieldName) { return FieldName.Equals(Name, ESearchCase::CaseSensitive); });
				if (Index == INDEX_NONE) return Fail(Error, TEXT("Composite struct is missing a reflected field."));
				if (!ToProperty(Value.Children[Index], *It, It->ContainerPtrToValuePtr<void>(Data), Error)) return false;
				++Count;
			}
			return Count == Value.Children.Num() || Fail(Error, TEXT("Composite struct contains extra fields."));
		}
		if (FMapProperty* Map = CastField<FMapProperty>(Property))
		{
			if (Value.Kind != EKind::Map) return Fail(Error, TEXT("Expected a map."));
			FScriptMapHelper Helper(Map, Data);
			Helper.EmptyValues();
			TSet<FString> StringKeys;
			TSet<uint64> IntegerKeys;
			for (int32 Index = 0; Index < Value.Children.Num(); Index += 2)
			{
				const FValue& Key = Value.Children[Index];
				if (Key.Kind == EKind::String)
				{
					if (StringKeys.Contains(Key.String)) return Fail(Error, TEXT("Composite map contains duplicate keys."));
					StringKeys.Add(Key.String);
				}
				else if (IsInteger(Key))
				{
					if (IntegerKeys.Contains(Key.Integer)) return Fail(Error, TEXT("Composite map contains duplicate keys."));
					IntegerKeys.Add(Key.Integer);
				}
				else return Fail(Error, TEXT("Composite maps require string or integer keys."));
				FDefaultConstructedPropertyElement KeyData(Map->KeyProp);
				FDefaultConstructedPropertyElement ItemData(Map->ValueProp);
				if (!ToProperty(Value.Children[Index], Map->KeyProp, KeyData.GetObjAddress(), Error) || !ToProperty(Value.Children[Index + 1], Map->ValueProp, ItemData.GetObjAddress(), Error)) return false;
				Helper.AddPair(KeyData.GetObjAddress(), ItemData.GetObjAddress());
				if (Helper.Num() != Index / 2 + 1) return Fail(Error, TEXT("Composite map keys become duplicates in the reflected key type."));
			}
			Helper.Rehash();
			return true;
		}
		return Fail(Error, TEXT("Composite value does not match the reflected property."));
	}

	bool WriteString(FMemoryWriter& Writer, const FString& String, FString& Error)
	{
		FTCHARToUTF8 UTF8(*String, String.Len());
		int32 Count = UTF8.Length();
		if (Count > MaxBytes - Writer.Tell() - sizeof(int32)) return Fail(Error, TEXT("Composite payload is too large."));
		Writer << Count;
		Writer.Serialize(const_cast<ANSICHAR*>(UTF8.Get()), Count);
		return true;
	}

	bool WriteValue(FMemoryWriter& Writer, const FValue& Value, FString& Error)
	{
		if (Writer.Tell() > MaxBytes - 16) return Fail(Error, TEXT("Composite payload is too large."));
		uint8 Kind = static_cast<uint8>(Value.Kind);
		Writer << Kind;
		if (IsInteger(Value)) { uint64 Integer = Value.Integer; Writer << Integer; }
		else if (Value.Kind == EKind::Real) { double Real = Value.Real; Writer << Real; }
		else if (Value.Kind == EKind::String) return WriteString(Writer, Value.String, Error);
		else
		{
			if (Value.Kind == EKind::Struct && !WriteString(Writer, Value.String, Error)) return false;
			int32 Count = Value.Kind == EKind::Map ? Value.Children.Num() / 2 : Value.Children.Num();
			Writer << Count;
			for (int32 Index = 0; Index < Value.Children.Num(); ++Index)
			{
				if (Value.Kind == EKind::Struct && !WriteString(Writer, Value.Names[Index], Error)) return false;
				if (!WriteValue(Writer, Value.Children[Index], Error)) return false;
			}
		}
		return !Writer.IsError();
	}

	bool Need(FMemoryReader& Reader, const int64 Count, FString& Error)
	{
		return (Count >= 0 && Count <= Reader.TotalSize() - Reader.Tell()) || Fail(Error, TEXT("Truncated composite payload."));
	}

	bool ReadString(FMemoryReader& Reader, FString& String, FString& Error)
	{
		if (!Need(Reader, sizeof(int32), Error)) return false;
		int32 Count = 0;
		Reader << Count;
		if (!Need(Reader, Count, Error)) return false;
		TArray<ANSICHAR> UTF8;
		UTF8.SetNumUninitialized(Count + 1);
		Reader.Serialize(UTF8.GetData(), Count);
		UTF8[Count] = 0;
		FUTF8ToTCHAR Converted(UTF8.GetData(), Count);
		String = FString(Converted.Length(), Converted.Get());
		return true;
	}

	bool ReadValue(FMemoryReader& Reader, FValue& Value, FString& Error, int32 Depth, int32& Nodes)
	{
		if (!Enter(Depth, Nodes, Error) || !Need(Reader, 1, Error)) return false;
		uint8 Kind = 0;
		Reader << Kind;
		if (Kind > static_cast<uint8>(EKind::Map)) return Fail(Error, TEXT("Invalid composite value tag."));
		Value.Kind = static_cast<EKind>(Kind);
		if (IsInteger(Value)) { if (!Need(Reader, 8, Error)) return false; Reader << Value.Integer; }
		else if (Value.Kind == EKind::Real) { if (!Need(Reader, 8, Error)) return false; Reader << Value.Real; }
		else if (Value.Kind == EKind::String) return ReadString(Reader, Value.String, Error);
		else
		{
			if (Value.Kind == EKind::Struct && !ReadString(Reader, Value.String, Error)) return false;
			if (!Need(Reader, sizeof(int32), Error)) return false;
			int32 Count = 0;
			Reader << Count;
			if (Count < 0 || Count > MaxItems) return Fail(Error, TEXT("Invalid composite container length."));
			const int32 Children = Value.Kind == EKind::Map ? Count * 2 : Count;
			if (!Need(Reader, Children, Error) || Children > MaxNodes - Nodes) return Fail(Error, TEXT("Composite payload exceeds the item limit."));
			Value.Children.SetNum(Children);
			if (Value.Kind == EKind::Struct) Value.Names.SetNum(Count);
			TSet<FString> FieldNames;
			for (int32 Index = 0; Index < Children; ++Index)
			{
				if (Value.Kind == EKind::Struct)
				{
					if (!ReadString(Reader, Value.Names[Index], Error)) return false;
					if (FieldNames.Contains(Value.Names[Index])) return Fail(Error, TEXT("Composite struct contains duplicate fields."));
					FieldNames.Add(Value.Names[Index]);
				}
				if (!ReadValue(Reader, Value.Children[Index], Error, Depth + 1, Nodes)) return false;
			}
		}
		return !Reader.IsError();
	}

	bool Parse(const TArray<uint8>& Bytes, FValue& Value, FString& Error)
	{
		if (Bytes.Num() > MaxBytes) return Fail(Error, TEXT("Composite payload is too large."));
		FMemoryReader Reader(Bytes);
		int32 Nodes = 0;
		return ReadValue(Reader, Value, Error, 0, Nodes) && (Reader.Tell() == Reader.TotalSize() || Fail(Error, TEXT("Composite payload has trailing data.")));
	}

	bool Serialize(const FValue& Value, TArray<uint8>& Out, FString& Error)
	{
		Out.Reset();
		FMemoryWriter Writer(Out);
		if (!WriteValue(Writer, Value, Error)) { Out.Reset(); return false; }
		return true;
	}

	bool KindIs(const UmkaType* Type, const char* Name)
	{
		const char* Kind = umkaGetTypeKindName(Type);
		return Kind && FCStringAnsi::Strcmp(Kind, Name) == 0;
	}

	bool IsUnsigned(const UmkaType* Type)
	{
		const char* Kind = umkaGetTypeKindName(Type);
		return Kind && (FCStringAnsi::Strncmp(Kind, "uint", 4) == 0 || KindIs(Type, "char") || KindIs(Type, "bool"));
	}

	bool IsUmkaInteger(const UmkaType* Type)
	{
		const char* Kind = umkaGetTypeKindName(Type);
		return Kind && (FCStringAnsi::Strncmp(Kind, "int", 3) == 0 || IsUnsigned(Type));
	}

	bool ValidateUmka(const FValue& Value, const UmkaType* Type, FString& Error)
	{
		if (!Type || umkaGetTypeSize(Type) <= 0) return Fail(Error, TEXT("Invalid compiled composite type."));
		if (IsUmkaInteger(Type))
			return (IntegerFits(Value, umkaGetTypeSize(Type), IsUnsigned(Type)) && (!KindIs(Type, "bool") || Value.Integer <= 1)) || Fail(Error, TEXT("Composite integer exceeds the compiled type range."));
		if (KindIs(Type, "real") || KindIs(Type, "real32"))
			return (Value.Kind == EKind::Real && (!KindIs(Type, "real32") || !FMath::IsFinite(Value.Real) || FMath::IsFinite(static_cast<float>(Value.Real)))) || Fail(Error, TEXT("Composite real does not match the compiled type range."));
		if (KindIs(Type, "str"))
		{
			if (Value.Kind != EKind::String) return Fail(Error, TEXT("Composite string does not match the compiled type."));
			for (int32 Index = 0; Index < Value.String.Len(); ++Index)
				if (!Value.String[Index]) return Fail(Error, TEXT("Composite string contains a null character."));
			return true;
		}
		if (umkaIsStaticArrayType(Type) || umkaIsDynArrayType(Type))
		{
			if (Value.Kind != EKind::Array || (umkaIsStaticArrayType(Type) && Value.Children.Num() != umkaGetArrayLen(Type))) return Fail(Error, TEXT("Composite array does not match the compiled array shape."));
			const int32 Size = umkaGetTypeSize(umkaGetBaseType(Type));
			if (Size <= 0 || int64(Size) * Value.Children.Num() > MaxBytes) return Fail(Error, TEXT("Native composite array is too large."));
			for (const FValue& Child : Value.Children) if (!ValidateUmka(Child, umkaGetBaseType(Type), Error)) return false;
			return true;
		}
		if (KindIs(Type, "map"))
		{
			if (Value.Kind != EKind::Map) return Fail(Error, TEXT("Expected a composite map."));
			const UmkaType* KeyType = umkaGetMapKeyType(Type);
			const UmkaType* ItemType = umkaGetMapItemType(Type);
			if ((!IsUmkaInteger(KeyType) && !KindIs(KeyType, "str")) || (!IsUmkaInteger(ItemType) && !KindIs(ItemType, "real") && !KindIs(ItemType, "real32") && !KindIs(ItemType, "str") && !KindIs(ItemType, "struct"))) return Fail(Error, TEXT("Composite maps require string or integer keys and scalar or record values."));
			TSet<FString> StringKeys;
			TSet<uint64> IntegerKeys;
			for (int32 Index = 0; Index < Value.Children.Num(); Index += 2)
			{
				if (!ValidateUmka(Value.Children[Index], KeyType, Error) || !ValidateUmka(Value.Children[Index + 1], ItemType, Error)) return false;
				if (KindIs(KeyType, "str"))
				{
					if (StringKeys.Contains(Value.Children[Index].String)) return Fail(Error, TEXT("Composite map contains duplicate keys."));
					StringKeys.Add(Value.Children[Index].String);
				}
				else
				{
					if (IntegerKeys.Contains(Value.Children[Index].Integer)) return Fail(Error, TEXT("Composite map contains duplicate keys."));
					IntegerKeys.Add(Value.Children[Index].Integer);
				}
			}
			return true;
		}
		const int32 Count = umkaGetFieldCount(Type);
		if (Count < 0 || Value.Kind != EKind::Struct || Value.Children.Num() != Count) return Fail(Error, TEXT("Composite struct does not match the compiled fields."));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const char* Name = umkaGetFieldNameByIndex(Type, Index);
			const FString FieldName = Name ? UTF8_TO_TCHAR(Name) : TEXT("");
			const int32 SourceIndex = Name ? Value.Names.IndexOfByPredicate([&FieldName](const FString& SourceName) { return SourceName.Equals(FieldName, ESearchCase::CaseSensitive); }) : INDEX_NONE;
			if (SourceIndex == INDEX_NONE) return Fail(Error, TEXT("Composite struct is missing a compiled field."));
			if (!ValidateUmka(Value.Children[SourceIndex], umkaGetFieldTypeByIndex(Type, Index), Error)) return false;
		}
		return true;
	}

	struct FDynArray { const UmkaType* Type; int64 ItemSize; void* Data; };

	void WriteInteger(void* Data, int32 Size, uint64 Value)
	{
		switch (Size)
		{
			case 1: Store(Data, static_cast<uint8>(Value)); break;
			case 2: Store(Data, static_cast<uint16>(Value)); break;
			case 4: Store(Data, static_cast<uint32>(Value)); break;
			default: Store(Data, Value); break;
		}
	}

	uint64 ReadInteger(const void* Data, int32 Size, bool bUnsigned)
	{
		switch (Size)
		{
			case 1: return bUnsigned ? Load<uint8>(Data) : static_cast<uint64>(Load<int8>(Data));
			case 2: return bUnsigned ? Load<uint16>(Data) : static_cast<uint64>(Load<int16>(Data));
			case 4: return bUnsigned ? Load<uint32>(Data) : static_cast<uint64>(Load<int32>(Data));
			default: return Load<uint64>(Data);
		}
	}

	bool ToUmka(Umka* VM, const FValue& Value, const UmkaType* Type, void* Data, FString& Error)
	{
		if (IsUmkaInteger(Type)) WriteInteger(Data, umkaGetTypeSize(Type), Value.Integer);
		else if (KindIs(Type, "real")) Store(Data, Value.Real);
		else if (KindIs(Type, "real32")) Store(Data, static_cast<float>(Value.Real));
		else if (KindIs(Type, "str"))
		{
			FTCHARToUTF8 UTF8(*Value.String);
			char* String = umkaMakeStr(VM, UTF8.Get());
			if (!String) return Fail(Error, TEXT("Could not allocate a native composite string."));
			Store(Data, String);
		}
		else if (umkaIsStaticArrayType(Type) || umkaIsDynArrayType(Type))
		{
			void* Items = Data;
			const UmkaType* Element = umkaGetBaseType(Type);
			const int32 Size = umkaGetTypeSize(Element);
			if (Size <= 0 || int64(Size) * Value.Children.Num() > MaxBytes) return Fail(Error, TEXT("Native composite array is too large."));
			if (umkaIsDynArrayType(Type))
			{
				FDynArray Header{};
				umkaMakeDynArray(VM, &Header, Type, Value.Children.Num());
				Store(Data, Header);
				if (!umkaAlive(VM)) return Fail(Error, TEXT("Could not allocate a native composite array."));
				Items = Header.Data;
				if (Value.Children.Num() && !Items) return Fail(Error, TEXT("Could not allocate a native composite array."));
			}
			for (int32 Index = 0; Index < Value.Children.Num(); ++Index)
				if (!ToUmka(VM, Value.Children[Index], Element, static_cast<uint8*>(Items) + int64(Index) * Size, Error)) return false;
		}
		else if (KindIs(Type, "map"))
		{
			UmkaMap Map{};
			umkaMakeMap(VM, &Map, Type);
			Store(Data, Map);
			if (!umkaAlive(VM)) return Fail(Error, TEXT("Could not allocate a native composite map."));
			const UmkaType* KeyType = umkaGetMapKeyType(Type);
			for (int32 Index = 0; Index < Value.Children.Num(); Index += 2)
			{
				UmkaStackSlot Key{};
				if (KindIs(KeyType, "str"))
				{
					FTCHARToUTF8 UTF8(*Value.Children[Index].String);
					Key.ptrVal = umkaMakeStr(VM, UTF8.Get());
					if (!Key.ptrVal) return Fail(Error, TEXT("Could not allocate a native composite map key."));
				}
				else Key.uintVal = Value.Children[Index].Integer;
				void* Item = umkaEnsureMapItem(VM, &Map, Key);
				if (KindIs(KeyType, "str")) umkaDecRef(VM, Key.ptrVal);
				Store(Data, Map);
				if (!Item || !ToUmka(VM, Value.Children[Index + 1], umkaGetMapItemType(Type), Item, Error)) return Fail(Error, TEXT("Could not create a native composite map item."));
			}
		}
		else
		{
			for (int32 Index = 0; Index < umkaGetFieldCount(Type); ++Index)
			{
				const FString FieldName = UTF8_TO_TCHAR(umkaGetFieldNameByIndex(Type, Index));
				const int32 SourceIndex = Value.Names.IndexOfByPredicate([&FieldName](const FString& SourceName) { return SourceName.Equals(FieldName, ESearchCase::CaseSensitive); });
				if (!ToUmka(VM, Value.Children[SourceIndex], umkaGetFieldTypeByIndex(Type, Index), static_cast<uint8*>(Data) + umkaGetFieldOffsetByIndex(Type, Index), Error)) return false;
			}
		}
		return true;
	}

	bool FromUmka(const UmkaType* Type, const void* Data, FValue& Value, FString& Error, int32 Depth, int32& Nodes);

	struct FMapReadContext
	{
		const UmkaType* Type;
		FValue& Value;
		FString& Error;
		int32 Depth;
		int32& Nodes;
	};

	bool ReadMapEntry(UmkaStackSlot Key, const void* Item, void* User)
	{
		FMapReadContext& Context = *static_cast<FMapReadContext*>(User);
		if (Context.Value.Children.Num() >= MaxItems * 2) return Fail(Context.Error, TEXT("Native composite map is too large."));
		FValue& KeyValue = Context.Value.Children.AddDefaulted_GetRef();
		const UmkaType* KeyType = umkaGetMapKeyType(Context.Type);
		// A stack key is widened; reconstruct narrow native storage without assuming alignment.
		uint64 NativeKey = 0;
		if (KindIs(KeyType, "str")) Store(&NativeKey, Key.ptrVal);
		else WriteInteger(&NativeKey, umkaGetTypeSize(KeyType), Key.uintVal);
		if (!FromUmka(KeyType, &NativeKey, KeyValue, Context.Error, Context.Depth + 1, Context.Nodes)) return false;
		FValue& ItemValue = Context.Value.Children.AddDefaulted_GetRef();
		return FromUmka(umkaGetMapItemType(Context.Type), Item, ItemValue, Context.Error, Context.Depth + 1, Context.Nodes);
	}

	bool FromUmka(const UmkaType* Type, const void* Data, FValue& Value, FString& Error, int32 Depth, int32& Nodes)
	{
		if (!Type || !Data || !Enter(Depth, Nodes, Error)) return Fail(Error, TEXT("Invalid native composite value or nesting limit."));
		if (IsUmkaInteger(Type))
		{
			Value.Kind = IsUnsigned(Type) ? EKind::Unsigned : EKind::Signed;
			Value.Integer = ReadInteger(Data, umkaGetTypeSize(Type), IsUnsigned(Type));
		}
		else if (KindIs(Type, "real") || KindIs(Type, "real32"))
		{
			Value.Kind = EKind::Real;
			Value.Real = KindIs(Type, "real32") ? Load<float>(Data) : Load<double>(Data);
		}
		else if (KindIs(Type, "str"))
		{
			Value.Kind = EKind::String;
			const char* String = Load<const char*>(Data);
			const int32 Length = String ? umkaGetStrLen(String) : 0;
			if (Length < 0 || Length > MaxBytes) return Fail(Error, TEXT("Native composite string is too large."));
			if (String) { FUTF8ToTCHAR Converted(String, Length); Value.String = FString(Converted.Length(), Converted.Get()); }
		}
		else if (umkaIsStaticArrayType(Type) || umkaIsDynArrayType(Type))
		{
			Value.Kind = EKind::Array;
			const UmkaType* Element = umkaGetBaseType(Type);
			const int32 Size = umkaGetTypeSize(Element);
			const FDynArray Header = umkaIsDynArrayType(Type) ? Load<FDynArray>(Data) : FDynArray{};
			const int32 Count = umkaIsDynArrayType(Type) ? umkaGetDynArrayLen(&Header) : umkaGetArrayLen(Type);
			const void* Items = umkaIsDynArrayType(Type) ? Header.Data : Data;
			if (Count < 0 || Count > MaxItems || Size <= 0 || int64(Count) * Size > MaxBytes || (Count && !Items) || (umkaIsDynArrayType(Type) && Count && Header.ItemSize != Size)) return Fail(Error, TEXT("Invalid native composite array shape."));
			Value.Children.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
				if (!FromUmka(Element, static_cast<const uint8*>(Items) + int64(Index) * Size, Value.Children[Index], Error, Depth + 1, Nodes)) return false;
		}
		else if (KindIs(Type, "map"))
		{
			Value.Kind = EKind::Map;
			const UmkaMap Map = Load<UmkaMap>(Data);
			const int32 Count = umkaGetMapLen(&Map);
			if (Count < 0 || Count > MaxItems) return Fail(Error, TEXT("Invalid native composite map length."));
			FMapReadContext Context{Type, Value, Error, Depth, Nodes};
			if (!umkaVisitMap(&Map, ReadMapEntry, &Context))
			{
				if (Error.IsEmpty()) Error = TEXT("Could not read the native composite map.");
				return false;
			}
		}
		else
		{
			const int32 Count = umkaGetFieldCount(Type);
			if (Count < 0 || Count > MaxItems) return Fail(Error, TEXT("Unsupported native composite type."));
			Value.Kind = EKind::Struct;
			const char* Name = umkaGetTypeName(Type);
			Value.String = Name ? UTF8_TO_TCHAR(Name) : TEXT("");
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const char* FieldName = umkaGetFieldNameByIndex(Type, Index);
				if (!FieldName) return Fail(Error, TEXT("Compiled struct field has no name."));
				Value.Names.Add(UTF8_TO_TCHAR(FieldName));
				FValue& Child = Value.Children.AddDefaulted_GetRef();
				if (!FromUmka(umkaGetFieldTypeByIndex(Type, Index), static_cast<const uint8*>(Data) + umkaGetFieldOffsetByIndex(Type, Index), Child, Error, Depth + 1, Nodes)) return false;
			}
		}
		return true;
	}
}

bool UEmkaComposite::EncodeProperty(const FProperty* Property, const void* Data, TArray<uint8>& Out, FString& Error)
{
	Out.Reset();
	Error.Reset();
	FValue Value;
	int32 Nodes = 0;
	return FromProperty(Property, Data, Value, Error, 0, Nodes) && Serialize(Value, Out, Error);
}

bool UEmkaComposite::DecodeProperty(const TArray<uint8>& Bytes, FProperty* Property, void* Data, FString& Error)
{
	Error.Reset();
	if (!Property || !Data) return Fail(Error, TEXT("Invalid reflected composite destination."));
	FValue Value;
	if (!Parse(Bytes, Value, Error)) return false;
	FDefaultConstructedPropertyElement Temporary(Property);
	if (!ToProperty(Value, Property, Temporary.GetObjAddress(), Error)) return false;
	Property->CopyCompleteValue(Data, Temporary.GetObjAddress());
	return true;
}

bool UEmkaComposite::WriteUmka(Umka* VM, const UmkaType* Type, const TArray<uint8>& Bytes, void* NativeData, FString& Error)
{
	Error.Reset();
	if (!VM || !NativeData) return Fail(Error, TEXT("Invalid native composite destination."));
	FValue Value;
	return Parse(Bytes, Value, Error) && ValidateUmka(Value, Type, Error) && ToUmka(VM, Value, Type, NativeData, Error);
}

bool UEmkaComposite::ReadUmka(const UmkaType* Type, const void* NativeData, TArray<uint8>& Out, FString& Error)
{
	Out.Reset();
	Error.Reset();
	FValue Value;
	int32 Nodes = 0;
	return FromUmka(Type, NativeData, Value, Error, 0, Nodes) && Serialize(Value, Out, Error);
}
