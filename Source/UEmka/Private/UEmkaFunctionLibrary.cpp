// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaFunctionLibrary.h"
#include "umka_api.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(UEmkaFunctionLibrary)

#define UEMKA_CAPTURE_STDOUT ((PLATFORM_WINDOWS || PLATFORM_UNIX || PLATFORM_MAC) && !NO_LOGGING)

#if UEMKA_CAPTURE_STDOUT && PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <io.h>
#include <fcntl.h>
#define UMKA_PIPE(fds, sz)    (_pipe(fds, sz, _O_BINARY) == 0)
#define UMKA_DUP(fd)          _dup(fd)
#define UMKA_DUP2(a, b)       _dup2(a, b)
#define UMKA_CLOSE(fd)        _close(fd)
#define UMKA_READ(fd, buf, n) _read(fd, buf, n)
#define UMKA_FILENO(f)        _fileno(f)
#elif UEMKA_CAPTURE_STDOUT
#include <unistd.h>
#include <fcntl.h>
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

// Stores an int-like value into a dynarray element of the given size.
// Sign is irrelevant for stores - two's complement truncation preserves the low bytes.
static void StoreIntElem(void* Data, const int32 Index, const int64 ItemSize, const int64 Value)
{
	switch (ItemSize)
	{
		case 1: static_cast<uint8*>(Data)[Index]  = static_cast<uint8>(Value);  break;
		case 2: static_cast<uint16*>(Data)[Index] = static_cast<uint16>(Value); break;
		case 4: static_cast<uint32*>(Data)[Index] = static_cast<uint32>(Value); break;
		default: static_cast<int64*>(Data)[Index] = Value;                      break;
	}
}

// Loads a dynarray element as int64, using the declared type for width and sign extension.
// Enum width follows the dynarray's itemSize - user enums may declare a smaller base type
// (e.g. "type Tiny = enum (uint8)" has 1-byte elements).
static int64 LoadIntElem(const void* Data, const int32 Index, const EUEmkaValueType Type, const int64 ItemSize)
{
	switch (Type)
	{
		case EUEmkaValueType::Int8:   return static_cast<const int8*>(Data)[Index];
		case EUEmkaValueType::Int16:  return static_cast<const int16*>(Data)[Index];
		case EUEmkaValueType::Int32:  return static_cast<const int32*>(Data)[Index];
		case EUEmkaValueType::UInt8:
		case EUEmkaValueType::Char:
		case EUEmkaValueType::Bool:   return static_cast<const uint8*>(Data)[Index];
		case EUEmkaValueType::UInt16: return static_cast<const uint16*>(Data)[Index];
		case EUEmkaValueType::UInt32: return static_cast<const uint32*>(Data)[Index];
		case EUEmkaValueType::UInt:   return static_cast<int64>(static_cast<const uint64*>(Data)[Index]);
		case EUEmkaValueType::Enum:
			switch (ItemSize)
			{
				case 1:  return static_cast<const uint8*>(Data)[Index];
				case 2:  return static_cast<const uint16*>(Data)[Index];
				case 4:  return static_cast<const uint32*>(Data)[Index];
				default: return static_cast<const int64*>(Data)[Index];
			}
		default:                      return static_cast<const int64*>(Data)[Index]; // Int
	}
}

static int32 GetArrayValueNum(const FUEmkaScriptParam& Param)
{
	switch (Param.Type)
	{
		case EUEmkaValueType::Real:   return Param.RealArrayValue.Num();
		case EUEmkaValueType::Real32: return Param.Real32ArrayValue.Num();
		case EUEmkaValueType::Str:    return Param.StringArrayValue.Num();
		default:                      return Param.IntArrayValue.Num();
	}
}

static void WriteArrayElements(Umka* Umka, void* Data, const int32 Len, const int32 ItemSize, const FUEmkaScriptParam& Param)
{
	if (Len <= 0)
	{
		return;
	}

	switch (Param.Type)
	{
		case EUEmkaValueType::Real:
			FMemory::Memcpy(Data, Param.RealArrayValue.GetData(), Len * sizeof(double));
			break;
		case EUEmkaValueType::Real32:
			FMemory::Memcpy(Data, Param.Real32ArrayValue.GetData(), Len * sizeof(float));
			break;
		case EUEmkaValueType::Str:
		{
			char** DataPtr = static_cast<char**>(Data);
			for (int32 i = 0; i < Len; ++i)
			{
				DataPtr[i] = umkaMakeStr(Umka, TCHAR_TO_UTF8(*Param.StringArrayValue[i]));
			}
			break;
		}
		default:
			for (int32 i = 0; i < Len; ++i)
			{
				StoreIntElem(Data, i, ItemSize, Param.IntArrayValue[i]);
			}
			break;
	}
}

// Copies contiguous Umka array elements into the matching Blueprint-facing storage.
static void ReadArrayResult(const void* Data, const int32 Len, const int32 ItemSize, const EUEmkaValueType Type, FUEmkaScriptParam& Out)
{
	switch (Type)
	{
		case EUEmkaValueType::Real:
		{
			Out.RealArrayValue.SetNum(Len);
			if (Len > 0)
			{
				FMemory::Memcpy(Out.RealArrayValue.GetData(), Data, Len * sizeof(double));
			}
			break;
		}
		case EUEmkaValueType::Real32:
		{
			Out.Real32ArrayValue.SetNum(Len);
			if (Len > 0)
			{
				FMemory::Memcpy(Out.Real32ArrayValue.GetData(), Data, Len * sizeof(float));
			}
			break;
		}
		case EUEmkaValueType::Str:
		{
			const char* const* DataPtr = static_cast<const char* const*>(Data);
			Out.StringArrayValue.SetNum(Len);
			for (int32 j = 0; j < Len; ++j)
			{
				Out.StringArrayValue[j] = DataPtr[j] ? UTF8_TO_TCHAR(DataPtr[j]) : TEXT("");
			}
			break;
		}
		default: // all int-like types
		{
			Out.IntArrayValue.SetNum(Len);
			for (int32 j = 0; j < Len; ++j)
			{
				Out.IntArrayValue[j] = LoadIntElem(Data, j, Type, ItemSize);
			}
			break;
		}
	}
}

// Copies a finished Umka dynarray into the matching array field of a script param.
static void ReadDynArrayResult(const FUmkaDynArrayHeader& Header, const EUEmkaValueType Type, FUEmkaScriptParam& Out)
{
	ReadArrayResult(Header.data, umkaGetDynArrayLen(&Header), static_cast<int32>(Header.itemSize), Type, Out);
}

// Formats the current Umka error as "file:line: msg", or returns Fallback when unavailable.
static FString FormatUmkaError(Umka* VM, const TCHAR* Fallback)
{
	const UmkaError* Err = umkaGetError(VM);
	if (Err && Err->msg)
	{
		return FString::Format(TEXT("{0}:{1}: {2}"), {Err->fileName, Err->line, Err->msg});
	}
	return Fallback;
}

// ResultTypes string (RunUmkaInlineMulti) encodes EUEmkaValueType by ordinal value.
// These asserts guard against silent breakage if the enum is reordered.
static_assert(static_cast<int32>(EUEmkaValueType::Int)  == 0,  "EUEmkaValueType ordinal changed - update ResultTypes serialization");
static_assert(static_cast<int32>(EUEmkaValueType::Str)  == 12, "EUEmkaValueType ordinal changed - update ResultTypes serialization");
static_assert(static_cast<int32>(EUEmkaValueType::Void) == 14, "EUEmkaValueType ordinal changed - update ResultTypes serialization");

// Pushes all input parameters onto the Umka function call stack.
// ArrayHeaders must outlive umkaCall() - Umka holds raw pointers into the header data.
static bool PushUmkaParams(Umka* Umka, const UmkaFuncContext& Context, const TArray<FUEmkaScriptParam>& Params, TArray<FUmkaDynArrayHeader>& ArrayHeaders, FString& Error)
{
	ArrayHeaders.Reserve(Params.Num());
	for (int32 i = 0; i < Params.Num(); ++i)
	{
		UmkaStackSlot* Slot = umkaGetParam(Context.params, i);
		if (!Slot)
		{
			Error = FString::Printf(TEXT("Parameter %d does not exist in the compiled Umka function"), i + 1);
			return false;
		}

		const FUEmkaScriptParam& Param = Params[i];
		if (Param.bIsArray)
		{
			const UmkaType* ParamType = umkaGetParamType(Context.params, i);
			const bool bCompiledStaticArray = umkaIsStaticArrayType(ParamType);
			const bool bCompiledDynArray = umkaIsDynArrayType(ParamType);
			if (!bCompiledStaticArray && !bCompiledDynArray)
			{
				Error = FString::Printf(TEXT("Parameter %d was represented as a Blueprint array but is not an Umka array"), i + 1);
				return false;
			}
			if (Param.bIsStaticArray != bCompiledStaticArray)
			{
				Error = FString::Printf(TEXT("Parameter %d array-kind metadata does not match the compiled Umka signature"), i + 1);
				return false;
			}

			const int32 ActualLen = GetArrayValueNum(Param);
			const UmkaType* ElementType = umkaGetBaseType(ParamType);
			const int32 ItemSize = umkaGetTypeSize(ElementType);
			if (ItemSize <= 0 && ActualLen > 0)
			{
				Error = FString::Printf(TEXT("Parameter %d has an invalid Umka array element size"), i + 1);
				return false;
			}

			if (bCompiledStaticArray)
			{
				const int32 ExpectedLen = umkaGetArrayLen(ParamType);
				if (ActualLen != ExpectedLen)
				{
					Error = FString::Printf(
						TEXT("Parameter %d expects exactly %d array elements, but Blueprint supplied %d"),
						i + 1,
						ExpectedLen,
						ActualLen);
					return false;
				}
				const int32 ArraySize = umkaGetTypeSize(ParamType);
				if (ArraySize > 0)
				{
					FMemory::Memzero(Slot, ArraySize);
					WriteArrayElements(Umka, Slot, ActualLen, ItemSize, Param);
				}
			}
			else
			{
				// []T is a three-slot header. Umka allocates backing storage from the VM heap.
				FUmkaDynArrayHeader& Header = ArrayHeaders.AddZeroed_GetRef();
				umkaMakeDynArray(Umka, &Header, ParamType, ActualLen);
				WriteArrayElements(Umka, Header.data, ActualLen, static_cast<int32>(Header.itemSize), Param);
				Slot[0].ptrVal = const_cast<UmkaType*>(Header.type);
				Slot[1].intVal = Header.itemSize;
				Slot[2].ptrVal = Header.data;
			}
			continue;
		}
		else
		{
			const UmkaType* ParamType = umkaGetParamType(Context.params, i);
			if (umkaIsStaticArrayType(ParamType) || umkaIsDynArrayType(ParamType))
			{
				Error = FString::Printf(TEXT("Parameter %d is an Umka array but was represented as a scalar"), i + 1);
				return false;
			}
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
				Slot->ptrVal = umkaMakeStr(Umka, TCHAR_TO_UTF8(*Param.StringValue));
				break;
			default:
				break;
		}
	}
	return true;
}

#if UEMKA_CAPTURE_STDOUT
// stdout is a process-global descriptor. Serialize the redirect/call/restore sequence so
// concurrent script executions cannot capture or restore each other's pipe endpoints.
static FCriticalSection GUmkaStdoutCaptureMutex;

// RAII scope guard that redirects CRT stdout to an internal pipe for the duration of its lifetime.
// Call FlushToLog() after umkaCall() to restore stdout and emit any captured printf output to UE_LOG.
// The pipe's write end is non-blocking: once the buffer is full, further printf output is dropped.
// A blocking write end would hang the engine - nothing drains the pipe while umkaCall runs.
struct FUmkaStdoutCapture
{
	static constexpr int32 BufSize = 64 * 1024;
	int32 Pipe[2] = {-1, -1};
	int32 SavedFd = -1;
	bool bActive = false;

	static bool MakeWriteEndNonBlocking(const int32 Fd)
	{
		#if PLATFORM_WINDOWS
		// Anonymous pipes are named pipes internally - PIPE_NOWAIT makes a full-pipe
		// write return immediately with 0 bytes written instead of blocking
		HANDLE Handle = reinterpret_cast<HANDLE>(_get_osfhandle(Fd));
		if (Handle == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		DWORD Mode = PIPE_NOWAIT;
		return SetNamedPipeHandleState(Handle, &Mode, nullptr, nullptr) != 0;
		#else
		const int32 Flags = fcntl(Fd, F_GETFL);
		return Flags != -1 && fcntl(Fd, F_SETFL, Flags | O_NONBLOCK) != -1;
		#endif
	}

	FUmkaStdoutCapture()
	{
		if (UMKA_FILENO(stdout) < 0 || !UMKA_PIPE(Pipe, BufSize))
		{
			return;
		}

		if (!MakeWriteEndNonBlocking(Pipe[1]))
		{
			UMKA_CLOSE(Pipe[0]);
			UMKA_CLOSE(Pipe[1]);
			Pipe[0] = Pipe[1] = -1;
			return;
		}

		SavedFd = UMKA_DUP(UMKA_FILENO(stdout));
		if (SavedFd == -1 || UMKA_DUP2(Pipe[1], UMKA_FILENO(stdout)) == -1)
		{
			if (SavedFd != -1)
			{
				UMKA_CLOSE(SavedFd);
				SavedFd = -1;
			}
			UMKA_CLOSE(Pipe[0]);
			UMKA_CLOSE(Pipe[1]);
			Pipe[0] = Pipe[1] = -1;
			return;
		}

		bActive = true;
		UMKA_CLOSE(Pipe[1]);
		Pipe[1] = -1;
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
			// A dropped write on the full pipe sets the stream's sticky error flag,
			// which would silently disable all printf in the process from now on
			clearerr(stdout);
		}

		if (Pipe[0] != -1)
		{
			TArray<char> Buf;
			Buf.SetNumZeroed(BufSize + 1);
			const int32 BytesRead = UMKA_READ(Pipe[0], Buf.GetData(), BufSize);
			UMKA_CLOSE(Pipe[0]);
			Pipe[0] = -1;

			if (BytesRead == BufSize)
			{
				UE_LOG(LogUEmka, Warning, TEXT("[%s] printf output exceeded %d KB - excess was dropped"), *FunctionName, BufSize / 1024);
			}
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
			clearerr(stdout);
		}
		if (Pipe[0] != -1)
		{
			UMKA_CLOSE(Pipe[0]);
		}
	}
};
#endif

// Owns an Umka VM for one inline execution: allocates, compiles the script, and resolves the target function.
// Frees the VM on destruction. Check IsValid() / Error after construction.
struct FUmkaScopedVM
{
	Umka* VM = nullptr;
	UmkaFuncContext Context = {};
	FString Error;

	FUmkaScopedVM(const FString& Script, const FString& FunctionName)
	{
		if (FunctionName.IsEmpty())
		{
			Error = TEXT("No function name provided");
			return;
		}

		if (Script.IsEmpty())
		{
			Error = TEXT("No script provided");
			return;
		}

		VM = umkaAlloc();
		if (!VM)
		{
			Error = TEXT("Umka: failed to allocate VM");
			return;
		}

		if (!umkaInit(VM, "script.um", TCHAR_TO_UTF8(*Script), UmkaStackSize, nullptr, 0, nullptr, false, false, nullptr))
		{
			Error = TEXT("Umka failed to initialize");
			return;
		}

		if (!umkaCompile(VM))
		{
			Error = FormatUmkaError(VM, TEXT("Umka compile error"));
			return;
		}

		if (!umkaGetFunc(VM, nullptr, TCHAR_TO_UTF8(*FunctionName), &Context))
		{
			Error = FString::Format(TEXT("Function '{0}' not found"), {FunctionName});
		}
	}

	~FUmkaScopedVM()
	{
		if (VM)
		{
			umkaFree(VM);
		}
	}

	FUmkaScopedVM(const FUmkaScopedVM&) = delete;
	FUmkaScopedVM& operator=(const FUmkaScopedVM&) = delete;

	bool IsValid() const
	{
		return VM && Error.IsEmpty();
	}
};

bool UUEmkaFunctionLibrary::RunUmkaInline(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const EUEmkaValueType ResultType, const bool bResultIsArray, const bool bResultIsStaticArray, FUEmkaScriptParam& Result, FString& Error)
{
	FUmkaScopedVM Vm(Script, FunctionName);
	if (!Vm.IsValid())
	{
		Error = Vm.Error;
		return false;
	}

	// Push parameters - ArrayHeaders must outlive umkaCall() (Umka holds raw pointers)
	TArray<FUmkaDynArrayHeader> ArrayHeaders;
	if (!PushUmkaParams(Vm.VM, Vm.Context, Params, ArrayHeaders, Error))
	{
		return false;
	}

	// Arrays are structured results. []T writes a 24-byte header while [N]T writes its
	// elements inline. Use the compiled result type as the source of truth for both cases.
	FUmkaDynArrayHeader DynArrayResult = {};
	TArray<uint8> StaticArrayResult;
	const UmkaType* CompiledResultType = umkaGetResultType(Vm.Context.params, Vm.Context.result);
	const bool bCompiledStaticArray = umkaIsStaticArrayType(CompiledResultType);
	const bool bCompiledDynArray = umkaIsDynArrayType(CompiledResultType);
	if (ResultType != EUEmkaValueType::Void
		&& (bResultIsArray != (bCompiledStaticArray || bCompiledDynArray)
			|| (bResultIsArray && bResultIsStaticArray != bCompiledStaticArray)))
	{
		Error = TEXT("Result array-kind metadata does not match the compiled Umka signature");
		return false;
	}
	if (bResultIsArray && Vm.Context.result)
	{
		if (bCompiledStaticArray)
		{
			StaticArrayResult.SetNumZeroed(FMath::Max(umkaGetTypeSize(CompiledResultType), 1));
			Vm.Context.result->ptrVal = StaticArrayResult.GetData();
		}
		else
		{
			Vm.Context.result->ptrVal = &DynArrayResult;
		}
	}

	// Redirect CRT stdout around umkaCall so printf() output lands in UE_LOG.
	// printf is a VM builtin (not overridable via umkaAddFunc) so we capture at the fd level.
	#if UEMKA_CAPTURE_STDOUT
	FScopeLock StdoutCaptureLock(&GUmkaStdoutCaptureMutex);
	FUmkaStdoutCapture StdoutCapture;
	#endif

	const bool bSuccess = umkaCall(Vm.VM, &Vm.Context) == 0;

	#if UEMKA_CAPTURE_STDOUT
	StdoutCapture.FlushToLog(FunctionName);
	#endif

	if (bSuccess && ResultType != EUEmkaValueType::Void && Vm.Context.result)
	{
		Result.Type = ResultType;
		Result.bIsArray = bResultIsArray;
		Result.bIsStaticArray = bResultIsStaticArray;

		if (bResultIsArray)
		{
			if (bResultIsStaticArray)
			{
				const int32 Len = umkaGetArrayLen(CompiledResultType);
				const int32 ItemSize = umkaGetTypeSize(umkaGetBaseType(CompiledResultType));
				ReadArrayResult(StaticArrayResult.GetData(), Len, ItemSize, ResultType, Result);
			}
			else
			{
				// vmCall overwrites *fn->result with REG_RESULT after the call, so read the
				// header filled through the hidden out-parameter directly.
				ReadDynArrayResult(DynArrayResult, ResultType, Result);
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
					Result.IntValue = Vm.Context.result->intVal;
					break;
				case EUEmkaValueType::UInt:
					Result.IntValue = static_cast<int64>(Vm.Context.result->uintVal);
					break;
				// real32 results use realVal - real32Val is not used in result slots
				case EUEmkaValueType::Real:
				case EUEmkaValueType::Real32:
					Result.RealValue = Vm.Context.result->realVal;
					break;
				case EUEmkaValueType::Str:
					if (Vm.Context.result->ptrVal)
					{
						Result.StringValue = UTF8_TO_TCHAR(static_cast<const char*>(Vm.Context.result->ptrVal));
					}
					break;
				default:
					break;
			}
		}
	}
	else if (!bSuccess)
	{
		Error = FormatUmkaError(Vm.VM, TEXT("Umka runtime error"));
		UE_LOG(LogUEmka, Error, TEXT("[%s] %s: %s"), *GetPathNameSafe(Caller), *FunctionName, *Error);
	}

	return bSuccess;
}

// -------------------------------------------------------------------------
// Multi-return helpers (shared with RunUmkaInlineMulti)
// -------------------------------------------------------------------------

bool UUEmkaFunctionLibrary::RunUmkaInlineMulti(UObject* Caller, const FString& Script, const FString& FunctionName, const TArray<FUEmkaScriptParam>& Params, const FString& ResultTypes, TArray<FUEmkaScriptParam>& Results, FString& Error)
{
	// Parse "type:arrayKind:enumByteSize" triples. arrayKind is 0 for scalar,
	// 1 for []T, and 2 for [N]T. The third field is retained for serialized compatibility;
	// compiled Umka type metadata now provides the authoritative layout and enum width.
	TArray<EUEmkaValueType> RetTypes;
	TArray<int32> RetArrayKinds;
	{
		TArray<FString> Parts;
		ResultTypes.ParseIntoArray(Parts, TEXT(","), true);
		for (const FString& P : Parts)
		{
			TArray<FString> KV;
			P.TrimStartAndEnd().ParseIntoArray(KV, TEXT(":"), true);
			if (KV.IsEmpty())
			{
				continue;
			}
			RetTypes.Add(static_cast<EUEmkaValueType>(FCString::Atoi(*KV[0])));
			const int32 ParsedArrayKind = KV.Num() > 1 ? FCString::Atoi(*KV[1]) : 0;
			RetArrayKinds.Add(ParsedArrayKind == 2 ? 2 : (ParsedArrayKind != 0 ? 1 : 0));
		}
	}

	if (RetTypes.IsEmpty())
	{
		Error = "No result types provided";
		return false;
	}

	FUmkaScopedVM Vm(Script, FunctionName);
	if (!Vm.IsValid())
	{
		Error = Vm.Error;
		return false;
	}

	// Push parameters - ArrayHeaders must outlive umkaCall() (Umka holds raw pointers)
	TArray<FUmkaDynArrayHeader> ArrayHeaders;
	if (!PushUmkaParams(Vm.VM, Vm.Context, Params, ArrayHeaders, Error))
	{
		return false;
	}

	// Multi-return values are represented by an Umka expression-list struct. Query its exact
	// size, field offsets, and field types instead of reproducing private layout rules here.
	const UmkaType* CompiledResultType = umkaGetResultType(Vm.Context.params, Vm.Context.result);
	if (umkaGetFieldCount(CompiledResultType) != RetTypes.Num())
	{
		Error = TEXT("Result metadata does not match the compiled Umka multi-return signature");
		return false;
	}

	TArray<int32> FieldOffsets;
	TArray<const UmkaType*> FieldTypes;
	FieldOffsets.SetNum(RetTypes.Num());
	FieldTypes.SetNum(RetTypes.Num());
	for (int32 i = 0; i < RetTypes.Num(); ++i)
	{
		FieldOffsets[i] = umkaGetFieldOffsetByIndex(CompiledResultType, i);
		FieldTypes[i] = umkaGetFieldTypeByIndex(CompiledResultType, i);
		const bool bCompiledStaticArray = umkaIsStaticArrayType(FieldTypes[i]);
		const bool bCompiledDynArray = umkaIsDynArrayType(FieldTypes[i]);
		const int32 ExpectedArrayKind = bCompiledStaticArray ? 2 : (bCompiledDynArray ? 1 : 0);
		if (FieldOffsets[i] < 0 || !FieldTypes[i] || RetArrayKinds[i] != ExpectedArrayKind)
		{
			Error = FString::Printf(TEXT("Result %d array-kind metadata does not match the compiled Umka signature"), i + 1);
			return false;
		}
	}

	TArray<uint8> StructBuffer;
	StructBuffer.SetNumZeroed(FMath::Max(umkaGetTypeSize(CompiledResultType), 1));

	if (Vm.Context.result)
	{
		Vm.Context.result->ptrVal = StructBuffer.GetData();
	}

	// Redirect CRT stdout around umkaCall so printf() output lands in UE_LOG.
	// printf is a VM builtin (not overridable via umkaAddFunc) so we capture at the fd level.
	#if UEMKA_CAPTURE_STDOUT
	FScopeLock StdoutCaptureLock(&GUmkaStdoutCaptureMutex);
	FUmkaStdoutCapture StdoutCapture;
	#endif

	const bool bSuccess = umkaCall(Vm.VM, &Vm.Context) == 0;

	#if UEMKA_CAPTURE_STDOUT
	StdoutCapture.FlushToLog(FunctionName);
	#endif

	if (bSuccess)
	{
		const uint8* Base = StructBuffer.GetData();
		Results.SetNum(RetTypes.Num());
		for (int32 i = 0; i < RetTypes.Num(); ++i)
		{
			const EUEmkaValueType T = RetTypes[i];
			const bool bIsArr = RetArrayKinds[i] != 0;
			const bool bIsStaticArray = RetArrayKinds[i] == 2;
			const uint8* FieldPtr = Base + FieldOffsets[i];
			FUEmkaScriptParam& R = Results[i];
			R.Type = T;
			R.bIsArray = bIsArr;
			R.bIsStaticArray = bIsStaticArray;

			if (bIsArr)
			{
				if (bIsStaticArray)
				{
					ReadArrayResult(
						FieldPtr,
						umkaGetArrayLen(FieldTypes[i]),
						umkaGetTypeSize(umkaGetBaseType(FieldTypes[i])),
						T,
						R);
				}
				else
				{
					ReadDynArrayResult(*reinterpret_cast<const FUmkaDynArrayHeader*>(FieldPtr), T, R);
				}
			}
			else
			{
				switch (T)
				{
					case EUEmkaValueType::Int:    R.IntValue = *reinterpret_cast<const int64*>(FieldPtr);  break;
					case EUEmkaValueType::Enum:
						switch (umkaGetTypeSize(FieldTypes[i]))
						{
							case 1:  R.IntValue = *reinterpret_cast<const uint8*>(FieldPtr);  break;
							case 2:  R.IntValue = *reinterpret_cast<const uint16*>(FieldPtr); break;
							case 4:  R.IntValue = *reinterpret_cast<const uint32*>(FieldPtr); break;
							default: R.IntValue = *reinterpret_cast<const int64*>(FieldPtr);  break;
						}
						break;
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
		Error = FormatUmkaError(Vm.VM, TEXT("Umka runtime error"));
		UE_LOG(LogUEmka, Error, TEXT("[%s] %s: %s"), *GetPathNameSafe(Caller), *FunctionName, *Error);
	}

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

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeIntParam(const EUEmkaValueType Type, const int64 Value)
{
	FUEmkaScriptParam P;
	P.Type = Type;
	P.IntValue = Value;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeBoolParam(const bool Value)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Bool;
	P.IntValue = Value ? 1 : 0;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeRealParam(const double Value)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Real;
	P.RealValue = Value;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeReal32Param(const float Value)
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

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeIntArrayParam(const EUEmkaValueType Type, const TArray<int32>& Values, const bool bIsStaticArray)
{
	FUEmkaScriptParam P;
	P.Type = Type;
	P.bIsArray = true;
	P.bIsStaticArray = bIsStaticArray;
	P.IntArrayValue.SetNum(Values.Num());
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		P.IntArrayValue[i] = Values[i];
	}
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeByteArrayParam(const EUEmkaValueType Type, const TArray<uint8>& Values, const bool bIsStaticArray)
{
	FUEmkaScriptParam P;
	P.Type = Type;
	P.bIsArray = true;
	P.bIsStaticArray = bIsStaticArray;
	P.IntArrayValue.SetNum(Values.Num());
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		P.IntArrayValue[i] = Values[i];
	}
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeInt64ArrayParam(const EUEmkaValueType Type, const TArray<int64>& Values, const bool bIsStaticArray)
{
	FUEmkaScriptParam P;
	P.Type = Type;
	P.bIsArray = true;
	P.bIsStaticArray = bIsStaticArray;
	P.IntArrayValue = Values;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeRealArrayParam(const TArray<double>& Values, const bool bIsStaticArray)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Real;
	P.bIsArray = true;
	P.bIsStaticArray = bIsStaticArray;
	P.RealArrayValue = Values;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeReal32ArrayParam(const TArray<float>& Values, const bool bIsStaticArray)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Real32;
	P.bIsArray = true;
	P.bIsStaticArray = bIsStaticArray;
	P.Real32ArrayValue = Values;
	return P;
}

FUEmkaScriptParam UUEmkaFunctionLibrary::MakeStrArrayParam(const TArray<FString>& Values, const bool bIsStaticArray)
{
	FUEmkaScriptParam P;
	P.Type = EUEmkaValueType::Str;
	P.bIsArray = true;
	P.bIsStaticArray = bIsStaticArray;
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

TArray<uint8> UUEmkaFunctionLibrary::GetByteArrayResult(const FUEmkaScriptParam& Result)
{
	TArray<uint8> Out;
	Out.SetNum(Result.IntArrayValue.Num());
	for (int32 i = 0; i < Result.IntArrayValue.Num(); ++i)
	{
		Out[i] = static_cast<uint8>(Result.IntArrayValue[i]);
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
