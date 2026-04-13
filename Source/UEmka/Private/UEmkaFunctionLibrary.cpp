// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaFunctionLibrary.h"
#include "umka_api.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(UEmkaFunctionLibrary)

#if PLATFORM_WINDOWS
#include <io.h>
#include <fcntl.h>
#define UMKA_PIPE(fds, sz)    (_pipe(fds, sz, _O_BINARY) == 0)
#define UMKA_DUP(fd)          _dup(fd)
#define UMKA_DUP2(a, b)       _dup2(a, b)
#define UMKA_CLOSE(fd)        _close(fd)
#define UMKA_READ(fd, buf, n) _read(fd, buf, n)
#define UMKA_FILENO(f)        _fileno(f)
#elif PLATFORM_UNIX || PLATFORM_MAC
#include <unistd.h>
#define UMKA_PIPE(fds, sz)    (pipe(fds) == 0)
#define UMKA_DUP(fd)          dup(fd)
#define UMKA_DUP2(a, b)       dup2(a, b)
#define UMKA_CLOSE(fd)        close(fd)
#define UMKA_READ(fd, buf, n) static_cast<int32>(read(fd, buf, n))
#define UMKA_FILENO(f)        fileno(f)
#endif

DEFINE_LOG_CATEGORY_STATIC(LogUEmka, Log, All);

// Umka VM stack size per script execution. 64 KB is sufficient for small inline scripts.
// Increase if scripts use deep recursion or large local arrays.
static constexpr int32 UmkaStackSize = 64 * 1024;

// Stable storage for dynarray headers - Reserve prevents realloc so pointers remain valid.
// Layout matches UmkaDynArray(T) macro: type*, itemSize (int64), data*
struct FUmkaDynArrayHeader
{
	const UmkaType* type;
	int64 itemSize;
	void* data;
};

// ResultTypes string (RunUmkaInlineMulti) encodes EUEmkaValueType by ordinal value.
// These asserts guard against silent breakage if the enum is reordered.
static_assert(static_cast<int32>(EUEmkaValueType::Int)  == 0,  "EUEmkaValueType ordinal changed - update ResultTypes serialization");
static_assert(static_cast<int32>(EUEmkaValueType::Str)  == 12, "EUEmkaValueType ordinal changed - update ResultTypes serialization");
static_assert(static_cast<int32>(EUEmkaValueType::Void) == 14, "EUEmkaValueType ordinal changed - update ResultTypes serialization");

// Pushes all input parameters onto the Umka function call stack.
// ArrayHeaders must outlive umkaCall() - Umka holds raw pointers into the header data.
static void PushUmkaParams(Umka* umka, UmkaFuncContext& Context, const TArray<FUEmkaScriptParam>& Params, TArray<FUmkaDynArrayHeader>& ArrayHeaders)
{
	ArrayHeaders.Reserve(Params.Num());
	for (int32 i = 0; i < Params.Num(); ++i)
	{
		UmkaStackSlot* Slot = umkaGetParam(Context.params, i);
		if (!Slot)
		{
			break;
		}

		const FUEmkaScriptParam& Param = Params[i];
		if (Param.bIsArray)
		{
			// Pass the array type ([]T), not the element type - doAllocDynArray accesses type->base->size internally
			const UmkaType* ParamType = umkaGetParamType(Context.params, i);
			FUmkaDynArrayHeader& Header = ArrayHeaders.AddZeroed_GetRef();
			switch (Param.Type)
			{
				case EUEmkaValueType::Real:
					umkaMakeDynArray(umka, &Header, ParamType, Param.RealArrayValue.Num());
					FMemory::Memcpy(Header.data, Param.RealArrayValue.GetData(), Param.RealArrayValue.Num() * sizeof(double));
					break;
				case EUEmkaValueType::Real32:
					umkaMakeDynArray(umka, &Header, ParamType, Param.Real32ArrayValue.Num());
					FMemory::Memcpy(Header.data, Param.Real32ArrayValue.GetData(), Param.Real32ArrayValue.Num() * sizeof(float));
					break;
				case EUEmkaValueType::Str:
				{
					umkaMakeDynArray(umka, &Header, ParamType, Param.StringArrayValue.Num());
					char** DataPtr = static_cast<char**>(Header.data);
					for (int32 j = 0; j < Param.StringArrayValue.Num(); ++j)
					{
						DataPtr[j] = umkaMakeStr(umka, TCHAR_TO_UTF8(*Param.StringArrayValue[j]));
					}
					break;
				}
				default: // all int-like types including bool, char, uint
				{
					umkaMakeDynArray(umka, &Header, ParamType, Param.IntArrayValue.Num());
					int64* DataPtr = static_cast<int64*>(Header.data);
					for (int32 j = 0; j < Param.IntArrayValue.Num(); ++j)
					{
						DataPtr[j] = Param.IntArrayValue[j];
					}
					break;
				}
			}
			// DynArray params are passed by value across 3 consecutive slots: type, itemSize, data
			Slot[0].ptrVal = const_cast<UmkaType*>(Header.type);
			Slot[1].intVal = Header.itemSize;
			Slot[2].ptrVal = Header.data;
			continue;
		}

		switch (Param.Type)
		{
			case EUEmkaValueType::Int:
			case EUEmkaValueType::Int8:
			case EUEmkaValueType::Int16:
			case EUEmkaValueType::Int32:
			case EUEmkaValueType::UInt8:
			case EUEmkaValueType::UInt16:
			case EUEmkaValueType::UInt32:
			case EUEmkaValueType::Bool:
			case EUEmkaValueType::Char:
			case EUEmkaValueType::Enum:
				Slot->intVal = Param.IntValue;
				break;
			case EUEmkaValueType::UInt:
				Slot->uintVal = static_cast<uint64>(Param.IntValue);
				break;
			case EUEmkaValueType::Real:
				Slot->realVal = Param.RealValue;
				break;
			case EUEmkaValueType::Real32:
				Slot->real32Val = Param.Real32Value;
				break;
			case EUEmkaValueType::Str:
				Slot->ptrVal = umkaMakeStr(umka, TCHAR_TO_UTF8(*Param.StringValue));
				break;
			default:
				break;
		}
	}
}

#if PLATFORM_WINDOWS || PLATFORM_UNIX || PLATFORM_MAC
// RAII scope guard that redirects CRT stdout to an internal pipe for the duration of its lifetime.
// Call FlushToLog() after umkaCall() to restore stdout and emit any captured printf output to UE_LOG.
struct FUmkaStdoutCapture
{
	static constexpr int32 BufSize = 64 * 1024;
	int32 Pipe[2] = {-1, -1};
	int32 SavedFd = -1;
	bool bActive = false;

	FUmkaStdoutCapture()
	{
		bActive = UMKA_FILENO(stdout) >= 0 && UMKA_PIPE(Pipe, BufSize);
		if (bActive)
		{
			SavedFd = UMKA_DUP(UMKA_FILENO(stdout));
			UMKA_DUP2(Pipe[1], UMKA_FILENO(stdout));
			UMKA_CLOSE(Pipe[1]);
			Pipe[1] = -1;
		}
	}

	// Restores stdout, reads the captured output, and emits each line to UE_LOG.
	void FlushToLog(const FString& FunctionName)
	{
		if (!bActive)
		{
			return;
		}

		if (SavedFd != -1)
		{
			fflush(stdout);
			UMKA_DUP2(SavedFd, UMKA_FILENO(stdout));
			UMKA_CLOSE(SavedFd);
			SavedFd = -1;
		}

		if (Pipe[0] != -1)
		{
			TArray<char> Buf;
			Buf.SetNumZeroed(BufSize + 1);
			const int32 BytesRead = UMKA_READ(Pipe[0], Buf.GetData(), BufSize);
			UMKA_CLOSE(Pipe[0]);
			Pipe[0] = -1;
			if (BytesRead > 0)
			{
				Buf[BytesRead] = '\0';
				FString Captured = UTF8_TO_TCHAR(Buf.GetData());
				Captured.TrimEndInline();
				if (!Captured.IsEmpty())
				{
					TArray<FString> Lines;
					Captured.ParseIntoArray(Lines, TEXT("\n"), false);
					for (const FString& Line : Lines)
					{
						const FString Trimmed = Line.TrimEnd();
						if (!Trimmed.IsEmpty())
						{
							UE_LOG(LogUEmka, Log, TEXT("[%s] %s"), *FunctionName, *Trimmed);
						}
					}
				}
			}
		}
	}

	// Safety net: restores stdout and closes any open fds if FlushToLog was not called.
	~FUmkaStdoutCapture()
	{
		if (bActive && SavedFd != -1)
		{
			fflush(stdout);
			UMKA_DUP2(SavedFd, UMKA_FILENO(stdout));
			UMKA_CLOSE(SavedFd);
		}
		if (Pipe[0] != -1)
		{
			UMKA_CLOSE(Pipe[0]);
		}
	}
};
#endif

bool UUEmkaFunctionLibrary::RunUmkaInline(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, EUEmkaValueType ResultType, bool bResultIsArray, FUEmkaScriptParam& Result, FString& Error)
{
	if (FunctionName.IsEmpty())
	{
		Error = "No function name provided";
		return false;
	}

	if (Script.IsEmpty())
	{
		Error = "No script provided";
		return false;
	}

	Umka* umka = umkaAlloc();
	if (!umka)
	{
		Error = TEXT("Umka: failed to allocate VM");
		return false;
	}

	if (!umkaInit(umka, "script.um", TCHAR_TO_UTF8(*Script), UmkaStackSize, nullptr, 0, nullptr, false, false, nullptr))
	{
		Error = "Umka failed to initialize";
		umkaFree(umka);
		return false;
	}

	if (!umkaCompile(umka))
	{
		const UmkaError* UmkaErr = umkaGetError(umka);
		Error = FString::Format(TEXT("{0}:{1}: {2}"), {UmkaErr->fileName, UmkaErr->line, UmkaErr->msg});
		umkaFree(umka);
		return false;
	}

	UmkaFuncContext Context;
	if (!umkaGetFunc(umka, nullptr, TCHAR_TO_UTF8(*FunctionName), &Context))
	{
		Error = FString::Format(TEXT("Function '{0}' not found"), {FunctionName});
		umkaFree(umka);
		return false;
	}

	// Push parameters - ArrayHeaders must outlive umkaCall() (Umka holds raw pointers)
	TArray<FUmkaDynArrayHeader> ArrayHeaders;
	PushUmkaParams(umka, Context, Params, ArrayHeaders);

	// For structured (array) returns, Umka expects result->ptrVal to point to pre-allocated storage
	// before the call - it passes this as a hidden out-parameter internally
	FUmkaDynArrayHeader StructuredResult = {};
	if (bResultIsArray && Context.result)
	{
		Context.result->ptrVal = &StructuredResult;
	}

	// Redirect CRT stdout around umkaCall so printf() output lands in UE_LOG.
	// printf is a VM builtin (not overridable via umkaAddFunc) so we capture at the fd level.
	#if PLATFORM_WINDOWS || PLATFORM_UNIX || PLATFORM_MAC
	FUmkaStdoutCapture StdoutCapture;
	#endif

	const bool bSuccess = umkaCall(umka, &Context) == 0;

	#if PLATFORM_WINDOWS || PLATFORM_UNIX || PLATFORM_MAC
	StdoutCapture.FlushToLog(FunctionName);
	#endif

	if (bSuccess && ResultType != EUEmkaValueType::Void && Context.result)
	{
		Result.Type = ResultType;
		Result.bIsArray = bResultIsArray;

		if (bResultIsArray)
		{
			// vmCall overwrites *fn->result with REG_RESULT after the call, so Context.result->ptrVal
			// is no longer reliable for structured returns. Read StructuredResult directly - Umka
			// fills it via the hidden out-parameter before vmCall returns.
			const int32 Len = umkaGetDynArrayLen(&StructuredResult);
			switch (ResultType)
			{
				case EUEmkaValueType::Real:
				{
					Result.RealArrayValue.SetNum(Len);
					if (Len > 0)
					{
						FMemory::Memcpy(Result.RealArrayValue.GetData(), StructuredResult.data, Len * sizeof(double));
					}
					break;
				}
				case EUEmkaValueType::Real32:
				{
					Result.Real32ArrayValue.SetNum(Len);
					if (Len > 0)
					{
						FMemory::Memcpy(Result.Real32ArrayValue.GetData(), StructuredResult.data, Len * sizeof(float));
					}
					break;
				}
				case EUEmkaValueType::Str:
				{
					char** DataPtr = static_cast<char**>(StructuredResult.data);
					Result.StringArrayValue.SetNum(Len);
					for (int32 j = 0; j < Len; ++j)
					{
						Result.StringArrayValue[j] = DataPtr[j] ? UTF8_TO_TCHAR(DataPtr[j]) : TEXT("");
					}
					break;
				}
				default: // all int-like types
				{
					const int64* DataPtr = static_cast<const int64*>(StructuredResult.data);
					Result.IntArrayValue.SetNum(Len);
					for (int32 j = 0; j < Len; ++j)
					{
						Result.IntArrayValue[j] = DataPtr[j];
					}
					break;
				}
			}
		}
		else
		{
			switch (ResultType)
			{
				case EUEmkaValueType::Int:
				case EUEmkaValueType::Int8:
				case EUEmkaValueType::Int16:
				case EUEmkaValueType::Int32:
				case EUEmkaValueType::UInt8:
				case EUEmkaValueType::UInt16:
				case EUEmkaValueType::UInt32:
				case EUEmkaValueType::Bool:
				case EUEmkaValueType::Char:
				case EUEmkaValueType::Enum:
					Result.IntValue = Context.result->intVal;
					break;
				case EUEmkaValueType::UInt:
					Result.IntValue = static_cast<int64>(Context.result->uintVal);
					break;
				// real32 results use realVal - real32Val is not used in result slots
				case EUEmkaValueType::Real:
				case EUEmkaValueType::Real32:
					Result.RealValue = Context.result->realVal;
					break;
				case EUEmkaValueType::Str:
					if (Context.result->ptrVal)
					{
						Result.StringValue = UTF8_TO_TCHAR(static_cast<const char*>(Context.result->ptrVal));
					}
					break;
				default:
					break;
			}
		}
	}
	else if (!bSuccess)
	{
		const UmkaError* UmkaErr = umkaGetError(umka);
		if (UmkaErr && UmkaErr->msg)
		{
			Error = FString::Format(TEXT("{0}:{1}: {2}"), {UmkaErr->fileName, UmkaErr->line, UmkaErr->msg});
		}
		else
		{
			Error = TEXT("Umka runtime error");
		}
		UE_LOG(LogUEmka, Error, TEXT("[%s] %s: %s"), *GetPathNameSafe(Caller), *FunctionName, *Error);
	}

	umkaFree(umka);
	return bSuccess;
}

// -------------------------------------------------------------------------
// Multi-return helpers (shared with RunUmkaInlineMulti)
// -------------------------------------------------------------------------

// Returns the byte size Umka assigns to each primitive type in a struct field.
// For all primitives, alignment = size, matching typeAlignmentRecompute in umka_types.c.
static int32 UmkaTypeSize(EUEmkaValueType T)
{
	switch (T)
	{
		case EUEmkaValueType::Int8:
		case EUEmkaValueType::UInt8:
		case EUEmkaValueType::Bool:
		case EUEmkaValueType::Char:   return 1;
		case EUEmkaValueType::Int16:
		case EUEmkaValueType::UInt16: return 2;
		case EUEmkaValueType::Int32:
		case EUEmkaValueType::UInt32:
		case EUEmkaValueType::Real32: return 4;
		default:                      return 8; // int, uint, real, str (pointer)
	}
}

// Rounds X up to the nearest multiple of A - mirrors Umka's align() in umka_common.h.
static int32 UmkaAlignUp(int32 X, int32 A) { return ((X + A - 1) / A) * A; }

// Returns the byte size and alignment of a field in a multi-return struct.
// DynArray ([]T): size = 24 (type* + itemSize + data*), align = 8. Matches umka_types.c.
// Primitives: align = size.
static void UmkaFieldSizeAlign(EUEmkaValueType T, bool bIsArray, int32& OutSize, int32& OutAlign)
{
	if (bIsArray)
	{
		OutSize = 24; // sizeof(DynArray)
		OutAlign = 8; // alignof(int64_t)
		return;
	}
	OutSize = OutAlign = UmkaTypeSize(T);
}

// Computes struct field offsets using the same rule as typeAddField in umka_types.c:
// field[i].offset = align(field[i-1].offset + field[i-1].size, field[i].alignment)
static void ComputeFieldOffsets(const TArray<EUEmkaValueType>& Types, const TArray<bool>& bIsArrayFlags, TArray<int32>& OutOffsets, int32& OutStructSize)
{
	OutOffsets.SetNum(Types.Num());
	int32 MinNextOffset = 0;
	int32 StructAlign = 1;
	for (int32 i = 0; i < Types.Num(); ++i)
	{
		int32 FieldSize, FieldAlign;
		UmkaFieldSizeAlign(Types[i], bIsArrayFlags.IsValidIndex(i) && bIsArrayFlags[i], FieldSize, FieldAlign);
		if (FieldAlign > StructAlign)
		{
			StructAlign = FieldAlign;
		}
		OutOffsets[i] = UmkaAlignUp(MinNextOffset, FieldAlign);
		MinNextOffset = OutOffsets[i] + FieldSize;
	}
	OutStructSize = UmkaAlignUp(MinNextOffset, StructAlign);
}

bool UUEmkaFunctionLibrary::RunUmkaInlineMulti(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const FString& ResultTypes, TArray<FUEmkaScriptParam>& Results, FString& Error)
{
	if (FunctionName.IsEmpty())
	{
		Error = "No function name provided";
		return false;
	}

	if (Script.IsEmpty())
	{
		Error = "No script provided";
		return false;
	}

	// Parse "type:isArray" pairs (e.g. "0:0,12:0" = int,str  or  "0:1,12:0" = []int,str)
	TArray<EUEmkaValueType> RetTypes;
	TArray<bool> bRetIsArray;
	{
		TArray<FString> Parts;
		ResultTypes.ParseIntoArray(Parts, TEXT(","), true);
		for (const FString& P : Parts)
		{
			TArray<FString> KV;
			P.TrimStartAndEnd().ParseIntoArray(KV, TEXT(":"), true);
			RetTypes.Add(static_cast<EUEmkaValueType>(FCString::Atoi(*KV[0])));
			bRetIsArray.Add(KV.Num() > 1 && FCString::Atoi(*KV[1]) != 0);
		}
	}

	if (RetTypes.IsEmpty())
	{
		Error = "No result types provided";
		return false;
	}

	Umka* umka = umkaAlloc();
	if (!umka)
	{
		Error = TEXT("Umka: failed to allocate VM");
		return false;
	}

	if (!umkaInit(umka, "script.um", TCHAR_TO_UTF8(*Script), UmkaStackSize, nullptr, 0, nullptr, false, false, nullptr))
	{
		Error = "Umka failed to initialize";
		umkaFree(umka);
		return false;
	}

	if (!umkaCompile(umka))
	{
		const UmkaError* UmkaErr = umkaGetError(umka);
		Error = FString::Format(TEXT("{0}:{1}: {2}"), {UmkaErr->fileName, UmkaErr->line, UmkaErr->msg});
		umkaFree(umka);
		return false;
	}

	UmkaFuncContext Context;
	if (!umkaGetFunc(umka, nullptr, TCHAR_TO_UTF8(*FunctionName), &Context))
	{
		Error = FString::Format(TEXT("Function '{0}' not found"), {FunctionName});
		umkaFree(umka);
		return false;
	}

	// Push parameters - ArrayHeaders must outlive umkaCall() (Umka holds raw pointers)
	TArray<FUmkaDynArrayHeader> ArrayHeaders;
	PushUmkaParams(umka, Context, Params, ArrayHeaders);

	// Pre-allocate the result struct buffer and point the hidden #result param at it.
	// Field offsets are computed using the same layout rules as Umka (umka_types.c typeAddField).
	TArray<int32> FieldOffsets;
	int32 StructSize = 0;
	ComputeFieldOffsets(RetTypes, bRetIsArray, FieldOffsets, StructSize);

	TArray<uint8> StructBuffer;
	StructBuffer.SetNumZeroed(FMath::Max(StructSize, 1));

	if (Context.result)
	{
		Context.result->ptrVal = StructBuffer.GetData();
	}

	// Redirect CRT stdout around umkaCall so printf() output lands in UE_LOG.
	// printf is a VM builtin (not overridable via umkaAddFunc) so we capture at the fd level.
	#if PLATFORM_WINDOWS || PLATFORM_UNIX || PLATFORM_MAC
	FUmkaStdoutCapture StdoutCapture;
	#endif

	const bool bSuccess = umkaCall(umka, &Context) == 0;

	#if PLATFORM_WINDOWS || PLATFORM_UNIX || PLATFORM_MAC
	StdoutCapture.FlushToLog(FunctionName);
	#endif

	if (bSuccess)
	{
		const uint8* Base = StructBuffer.GetData();
		Results.SetNum(RetTypes.Num());
		for (int32 i = 0; i < RetTypes.Num(); ++i)
		{
			const EUEmkaValueType T = RetTypes[i];
			const bool bIsArr = bRetIsArray.IsValidIndex(i) && bRetIsArray[i];
			const uint8* FieldPtr = Base + FieldOffsets[i];
			FUEmkaScriptParam& R = Results[i];
			R.Type = T;
			R.bIsArray = bIsArr;

			if (bIsArr)
			{
				// FieldPtr points to the inline DynArray header (24 bytes) inside the result struct
				const FUmkaDynArrayHeader* Header = reinterpret_cast<const FUmkaDynArrayHeader*>(FieldPtr);
				const int32 Len = umkaGetDynArrayLen(FieldPtr);
				switch (T)
				{
					case EUEmkaValueType::Real:
						R.RealArrayValue.SetNum(Len);
						if (Len > 0)
						{
							FMemory::Memcpy(R.RealArrayValue.GetData(), Header->data, Len * sizeof(double));
						}
						break;
					case EUEmkaValueType::Real32:
						R.Real32ArrayValue.SetNum(Len);
						if (Len > 0)
						{
							FMemory::Memcpy(R.Real32ArrayValue.GetData(), Header->data, Len * sizeof(float));
						}
						break;
					case EUEmkaValueType::Str:
					{
						char** DataPtr = static_cast<char**>(Header->data);
						R.StringArrayValue.SetNum(Len);
						for (int32 j = 0; j < Len; ++j)
						{
							R.StringArrayValue[j] = DataPtr[j] ? UTF8_TO_TCHAR(DataPtr[j]) : TEXT("");
						}
						break;
					}
					default: // all int-like types
					{
						const int64* DataPtr = static_cast<const int64*>(Header->data);
						R.IntArrayValue.SetNum(Len);
						for (int32 j = 0; j < Len; ++j)
						{
							R.IntArrayValue[j] = DataPtr[j];
						}
						break;
					}
				}
			}
			else
			{
				switch (T)
				{
					case EUEmkaValueType::Int:
					case EUEmkaValueType::Enum:   R.IntValue = *reinterpret_cast<const int64*>(FieldPtr);  break;
					case EUEmkaValueType::Int8:   R.IntValue = *reinterpret_cast<const int8*>(FieldPtr);   break;
					case EUEmkaValueType::Int16:  R.IntValue = *reinterpret_cast<const int16*>(FieldPtr);  break;
					case EUEmkaValueType::Int32:  R.IntValue = *reinterpret_cast<const int32*>(FieldPtr);  break;
					case EUEmkaValueType::UInt8:
					case EUEmkaValueType::Char:   R.IntValue = *reinterpret_cast<const uint8*>(FieldPtr);  break;
					case EUEmkaValueType::UInt16: R.IntValue = *reinterpret_cast<const uint16*>(FieldPtr); break;
					case EUEmkaValueType::UInt32: R.IntValue = *reinterpret_cast<const uint32*>(FieldPtr); break;
					case EUEmkaValueType::UInt:   R.IntValue = static_cast<int64>(*reinterpret_cast<const uint64*>(FieldPtr)); break;
					case EUEmkaValueType::Bool:   R.IntValue = *reinterpret_cast<const bool*>(FieldPtr) ? 1 : 0; break;
					case EUEmkaValueType::Real:   R.RealValue = *reinterpret_cast<const double*>(FieldPtr); break;
					// real32 in a struct field is 4 bytes; widen to double so GetReal32Result works unchanged
					case EUEmkaValueType::Real32: R.RealValue = static_cast<double>(*reinterpret_cast<const float*>(FieldPtr)); break;
					case EUEmkaValueType::Str:
						if (const char* StrPtr = *reinterpret_cast<const char* const*>(FieldPtr))
						{
							R.StringValue = UTF8_TO_TCHAR(StrPtr);
						}
						break;
					default: break;
				}
			}
		}
	}
	else
	{
		const UmkaError* UmkaErr = umkaGetError(umka);
		if (UmkaErr && UmkaErr->msg)
		{
			Error = FString::Format(TEXT("{0}:{1}: {2}"), {UmkaErr->fileName, UmkaErr->line, UmkaErr->msg});
		}
		else
		{
			Error = TEXT("Umka runtime error");
		}
		UE_LOG(LogUEmka, Error, TEXT("[%s] %s: %s"), *GetPathNameSafe(Caller), *FunctionName, *Error);
	}

	umkaFree(umka);
	return bSuccess;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::GetMultiResultAt(const TArray<FUEmkaScriptParam>& Results, const int32 Index)
{
	return Results.IsValidIndex(Index) ? Results[Index] : FUEmkaScriptParam{};
}

bool UUEmkaFunctionLibrary::CompileCheckScript(const FString& Script, FString& OutError, int32& OutLine)
{
	OutError.Empty();
	OutLine = -1;

	Umka* UmkaInst = umkaAlloc();
	if (!UmkaInst)
	{
		OutError = TEXT("umkaAlloc failed");
		return false;
	}

	if (!umkaInit(UmkaInst, "script.um", TCHAR_TO_UTF8(*Script), UmkaStackSize, nullptr, 0, nullptr, false, false, nullptr))
	{
		OutError = TEXT("Umka failed to initialize");
		umkaFree(UmkaInst);
		return false;
	}

	const bool bOk = umkaCompile(UmkaInst) != 0;
	if (!bOk)
	{
		if (const UmkaError* Err = umkaGetError(UmkaInst))
		{
			OutLine = Err->line;
			OutError = FString::Format(TEXT("Line {0}: {1}"), {Err->line, UTF8_TO_TCHAR(Err->msg)});
		}
	}

	umkaFree(UmkaInst);
	return bOk;
}

// -------------------------------------------------------------------------
// Param construction helpers
// -------------------------------------------------------------------------

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType Type, int64 Value)
{
	FUEmkaScriptParam P;
	P.Type = Type;
	P.IntValue = Value;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeBoolParam(bool Value)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Bool;
	P.IntValue = Value ? 1 : 0;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeRealParam(double Value)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Real;
	P.RealValue = Value;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeReal32Param(float Value)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Real32;
	P.Real32Value = Value;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeStrParam(const FString& Value)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Str;
	P.StringValue = Value;
	return P;
}

// -------------------------------------------------------------------------
// Result extraction helpers
// -------------------------------------------------------------------------

int64 UUEmkaFunctionLibrary::GetIntResult(const FUEmkaScriptParam& Result)
{
	return Result.IntValue;
}

int32 UUEmkaFunctionLibrary::GetInt32Result(const FUEmkaScriptParam& Result)
{
	return static_cast<int32>(Result.IntValue);
}

double UUEmkaFunctionLibrary::GetRealResult(const FUEmkaScriptParam& Result)
{
	return Result.RealValue;
}

float UUEmkaFunctionLibrary::GetReal32Result(const FUEmkaScriptParam& Result)
{
	// real32 results come back in realVal, not real32Val
	return static_cast<float>(Result.RealValue);
}

FString UUEmkaFunctionLibrary::GetStrResult(const FUEmkaScriptParam& Result)
{
	return Result.StringValue;
}

// -------------------------------------------------------------------------
// Array param construction helpers
// -------------------------------------------------------------------------

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeIntArrayParam(EUEmkaValueType Type, const TArray<int32>& Values)
{
	FUEmkaScriptParam P;
	P.Type = Type;
	P.bIsArray = true;
	P.IntArrayValue.SetNum(Values.Num());
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		P.IntArrayValue[i] = Values[i];
	}
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeInt64ArrayParam(EUEmkaValueType Type, const TArray<int64>& Values)
{
	FUEmkaScriptParam P;
	P.Type = Type;
	P.bIsArray = true;
	P.IntArrayValue = Values;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeRealArrayParam(const TArray<double>& Values)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Real;
	P.bIsArray = true;
	P.RealArrayValue = Values;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeReal32ArrayParam(const TArray<float>& Values)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Real32;
	P.bIsArray = true;
	P.Real32ArrayValue = Values;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeStrArrayParam(const TArray<FString>& Values)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Str;
	P.bIsArray = true;
	P.StringArrayValue = Values;
	return P;
}

// -------------------------------------------------------------------------
// Array result extraction helpers
// -------------------------------------------------------------------------

TArray<int32> UUEmkaFunctionLibrary::GetInt32ArrayResult(const FUEmkaScriptParam& Result)
{
	TArray<int32> Out;
	Out.SetNum(Result.IntArrayValue.Num());
	for (int32 i = 0; i < Result.IntArrayValue.Num(); ++i)
	{
		Out[i] = static_cast<int32>(Result.IntArrayValue[i]);
	}
	return Out;
}

TArray<int64> UUEmkaFunctionLibrary::GetIntArrayResult(const FUEmkaScriptParam& Result)
{
	return Result.IntArrayValue;
}

TArray<double> UUEmkaFunctionLibrary::GetRealArrayResult(const FUEmkaScriptParam& Result)
{
	return Result.RealArrayValue;
}

TArray<float> UUEmkaFunctionLibrary::GetReal32ArrayResult(const FUEmkaScriptParam& Result)
{
	return Result.Real32ArrayValue;
}

TArray<FString> UUEmkaFunctionLibrary::GetStrArrayResult(const FUEmkaScriptParam& Result)
{
	return Result.StringArrayValue;
}
