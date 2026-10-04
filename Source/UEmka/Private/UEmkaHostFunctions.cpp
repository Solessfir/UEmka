// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaHostFunctions.h"
#include "UEmkaHostFunctionsInternal.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace
{
struct FRegisteredModule
{
	FString Source;
	TArray<UEmkaHostFunctions::FFunction> Functions;
};

FCriticalSection RegistryMutex;
TMap<FString, FRegisteredModule> RegisteredModules;
int32 LiveVMCount = 0;

bool IsValidModulePath(const FString& Path)
{
	if (Path.IsEmpty() || FTCHARToUTF8(*Path).Length() > 255 || !Path.EndsWith(TEXT(".um"), ESearchCase::CaseSensitive)
		|| Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("\\"))) return false;
	for (const TCHAR Ch : Path)
	{
		if (Ch < TEXT(' ') || Ch == TEXT(':') || Ch == TEXT('*') || Ch == TEXT('?') || Ch == TEXT('"')
			|| Ch == TEXT('<') || Ch == TEXT('>') || Ch == TEXT('|')) return false;
	}
	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("/"), false);
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..")) return false;
	}
	return true;
}

bool IsReservedModulePath(const FString& Path)
{
	return Path == TEXT("ue.um") || Path == TEXT("std.um") || Path == TEXT("fnc.um")
		|| Path == TEXT("mat.um") || Path == TEXT("utf8.um");
}

bool IsValidFunctionName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Len() > 255) return false;
	for (int32 Index = 0; Index < Name.Len(); ++Index)
	{
		const TCHAR Ch = Name[Index];
		const bool bLetter = (Ch >= TEXT('A') && Ch <= TEXT('Z')) || (Ch >= TEXT('a') && Ch <= TEXT('z')) || Ch == TEXT('_');
		if (!bLetter && (Index == 0 || Ch < TEXT('0') || Ch > TEXT('9'))) return false;
	}
	return true;
}

bool CanMutateRegistry(FString& Error)
{
	Error.Reset();
	if (!IsInGameThread())
	{
		Error = TEXT("Umka host modules must be registered or unregistered on the game thread");
		return false;
	}
	UEmkaRuntime::ResetIdleSessions();
	return true;
}
}

bool UEmkaHostFunctions::RegisterModule(const FString& ModulePath, const FString& Source, const TConstArrayView<FFunction> Functions, FString& Error)
{
	if (!CanMutateRegistry(Error)) return false;
	FScopeLock Lock(&RegistryMutex);
	if (LiveVMCount != 0)
	{
		Error = TEXT("Cannot change Umka host modules while a VM is active");
		return false;
	}
	if (!IsValidModulePath(ModulePath) || IsReservedModulePath(ModulePath))
	{
		Error = FString::Printf(TEXT("Invalid or reserved Umka host module path '%s': use a relative .um filename without empty, '.' or '..' segments"), *ModulePath);
		return false;
	}
	if (RegisteredModules.Contains(ModulePath))
	{
		Error = FString::Printf(TEXT("Umka host module '%s' is already registered"), *ModulePath);
		return false;
	}
	for (const TCHAR Ch : Source)
	{
		if (Ch == TEXT('\0'))
		{
			Error = TEXT("Umka host module source cannot contain embedded null characters");
			return false;
		}
	}
	TSet<FString> Names;
	for (const auto& Module : RegisteredModules)
	{
		for (const FFunction& Function : Module.Value.Functions) Names.Add(Function.Name);
	}
	for (const FFunction& Function : Functions)
	{
		if (!IsValidFunctionName(Function.Name) || Function.Name.StartsWith(TEXT("rtl"), ESearchCase::CaseSensitive) || !Function.Callback)
		{
			Error = FString::Printf(TEXT("Invalid or reserved Umka host function '%s': provide an identifier and non-null callback; rtl names are reserved"), *Function.Name);
			return false;
		}
		if (Names.Contains(Function.Name))
		{
			Error = FString::Printf(TEXT("Umka host function '%s' is already registered"), *Function.Name);
			return false;
		}
		Names.Add(Function.Name);
	}
	FRegisteredModule Module;
	Module.Source = Source;
	Module.Functions.Append(Functions.GetData(), Functions.Num());
	RegisteredModules.Add(ModulePath, MoveTemp(Module));
	return true;
}

bool UEmkaHostFunctions::UnregisterModule(const FString& ModulePath, FString& Error)
{
	if (!CanMutateRegistry(Error)) return false;
	FScopeLock Lock(&RegistryMutex);
	if (LiveVMCount != 0)
	{
		Error = TEXT("Cannot change Umka host modules while a VM is active");
		return false;
	}
	if (RegisteredModules.Remove(ModulePath) == 0)
	{
		Error = FString::Printf(TEXT("Umka host module '%s' is not registered"), *ModulePath);
		return false;
	}
	return true;
}

void UEmkaHostFunctions::AcquireSnapshot(FSnapshot& Snapshot)
{
	FScopeLock Lock(&RegistryMutex);
	Snapshot.Modules.Reset();
	Snapshot.Functions.Reset();
	for (const auto& Module : RegisteredModules)
	{
		Snapshot.Modules.Add({Module.Key, Module.Value.Source});
		Snapshot.Functions.Append(Module.Value.Functions);
	}
	++LiveVMCount;
}

void UEmkaHostFunctions::ReleaseSnapshot()
{
	FScopeLock Lock(&RegistryMutex);
	check(LiveVMCount > 0);
	--LiveVMCount;
}
