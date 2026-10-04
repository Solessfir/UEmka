// Copyright Solessfir 2026. All Rights Reserved.

#include "UEmkaFunctionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS && (PLATFORM_WINDOWS || PLATFORM_UNIX || PLATFORM_MAC) && !NO_LOGGING

#include "Async/Async.h"
#include "EdGraph/EdGraph.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include <cstdio>

#if PLATFORM_UNIX || PLATFORM_MAC
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
class FUEmkaTestLog final : public FOutputDevice
{
public:
	struct FMessage { FString Text; ELogVerbosity::Type Verbosity; };
	FUEmkaTestLog()
	{
		GLog->FlushThreadedLogs();
		GLog->AddOutputDevice(this);
	}
	~FUEmkaTestLog() override { GLog->RemoveOutputDevice(this); }
	bool CanBeUsedOnAnyThread() const override { return true; }
	bool CanBeUsedOnMultipleThreads() const override { return true; }
	void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Category == TEXT("LogUEmka"))
		{
			FScopeLock Lock(&Mutex);
			Messages.Add({Text, Verbosity});
		}
	}
	TArray<FMessage> GetMessages()
	{
		GLog->FlushThreadedLogs();
		FScopeLock Lock(&Mutex);
		return Messages;
	}
private:
	FCriticalSection Mutex;
	TArray<FMessage> Messages;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaStdoutLoggingTest, "UEmka.Runtime.StdoutLogging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaStdoutLoggingTest::RunTest(const FString& Parameters)
{
	FUEmkaTestLog Log;
	FUEmkaScriptParam Result;
	FString Error;
	const FString Text = TEXT("Zażółć 世界 🚀");
	const FString Script = TEXT("fn LogText*(Value: str): int { printf(\"%s\\nsecond\\n\", Value); return 7 }");
	TestTrue(TEXT("Script output capture succeeds"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, TEXT("LogText"),
		{UUEmkaFunctionLibrary::MakeStrParam(Text)}, EUEmkaValueType::Int, false, false, Result, Error));
	TestEqual(TEXT("Logging preserves the return value"), Result.IntValue, int64{7});
	TArray<FUEmkaScriptParam> Results;
	TestTrue(TEXT("Tuple output capture succeeds"), UUEmkaFunctionLibrary::RunUmkaInlineMulti(nullptr,
		TEXT("fn LogTuple*(): (int, str) { printf(\"tuple\\n\"); return 8, \"ok\" }"), TEXT("LogTuple"), {}, TEXT("0:0:8,12:0:8"), Results, Error));
	const TArray<FUEmkaTestLog::FMessage> Messages = Log.GetMessages();
	TestTrue(TEXT("Unicode output is attributed to the function"), Messages.ContainsByPredicate([&Text](const auto& Message) { return Message.Text == TEXT("[LogText] ") + Text; }));
	TestTrue(TEXT("Each line is logged separately"), Messages.ContainsByPredicate([](const auto& Message) { return Message.Text == TEXT("[LogText] second"); }));
	TestTrue(TEXT("Tuple output is attributed to the function"), Messages.ContainsByPredicate([](const auto& Message) { return Message.Text == TEXT("[LogTuple] tuple"); }));
	UObject* Caller = NewObject<UEdGraph>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UEdGraph::StaticClass(), TEXT("UEmkaLogCaller")));
	const FString Attribution = TEXT("[") + Caller->GetPathName() + TEXT("] CallerFailure:");
	AddExpectedErrorPlain(Attribution, EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Failure is attributed to its calling object"), UUEmkaFunctionLibrary::RunUmkaInline(Caller,
		TEXT("fn CallerFailure*(Value: int): int { return 1 / Value }"), TEXT("CallerFailure"),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 0)}, EUEmkaValueType::Int, false, false, Result, Error));
	TestTrue(TEXT("Logged failure includes caller path and function"), Log.GetMessages().ContainsByPredicate([&Attribution](const auto& Message) { return Message.Text.StartsWith(Attribution); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaStdoutRestorationTest, "UEmka.Runtime.StdoutRestoration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaStdoutRestorationTest::RunTest(const FString& Parameters)
{
	FUEmkaTestLog Log;
	FUEmkaScriptParam Result;
	FString Error;
	#if PLATFORM_UNIX || PLATFORM_MAC
	struct stat Before = {}, After = {};
	TestEqual(TEXT("Original stdout is valid"), fstat(fileno(stdout), &Before), 0);
	#endif
	AddExpectedError(TEXT("LogFailure:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("VM exception propagates"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		TEXT("fn LogFailure*(Value: int): int { printf(\"before failure\\n\"); return 1 / Value }"), TEXT("LogFailure"),
		{UUEmkaFunctionLibrary::MakeIntParam(EUEmkaValueType::Int, 0)}, EUEmkaValueType::Int, false, false, Result, Error));
	AddExpectedMessagePlain(TEXT("[LogFlood] printf output"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("Output beyond the capture capacity does not block"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		TEXT("fn LogFlood*(): int { for i := 0; i < 131072; i++ { printf(\"x\") }; return 9 }"), TEXT("LogFlood"), {},
		EUEmkaValueType::Int, false, false, Result, Error));
	TestEqual(TEXT("Flooded script returns normally"), Result.IntValue, int64{9});
	TestEqual(TEXT("Dropped output does not leave stdout in an error state"), ferror(stdout), 0);
	TestTrue(TEXT("Output capture still works after a flood and VM exception"), UUEmkaFunctionLibrary::RunUmkaInline(nullptr,
		TEXT("fn LogRecovery*(): int { printf(\"recovered\\n\"); return 10 }"), TEXT("LogRecovery"), {},
		EUEmkaValueType::Int, false, false, Result, Error));
	#if PLATFORM_UNIX || PLATFORM_MAC
	TestEqual(TEXT("Restored stdout is valid"), fstat(fileno(stdout), &After), 0);
	TestTrue(TEXT("Stdout points at the original file"), Before.st_dev == After.st_dev && Before.st_ino == After.st_ino);
	#endif
	const TArray<FUEmkaTestLog::FMessage> Messages = Log.GetMessages();
	TestTrue(TEXT("Output before a VM exception is preserved"), Messages.ContainsByPredicate([](const auto& Message) { return Message.Text == TEXT("[LogFailure] before failure"); }));
	TestTrue(TEXT("Output after restoration is preserved"), Messages.ContainsByPredicate([](const auto& Message) { return Message.Text == TEXT("[LogRecovery] recovered"); }));
	TestEqual(TEXT("Exactly one warning reports lost output"), Messages.FilterByPredicate([](const auto& Message) { return Message.Text.StartsWith(TEXT("[LogFlood] printf output")); }).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaConcurrentLoggingTest, "UEmka.Runtime.ConcurrentLogging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaConcurrentLoggingTest::RunTest(const FString& Parameters)
{
	FUEmkaTestLog Log;
	TArray<TFuture<bool>> Tasks;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Tasks.Add(Async(EAsyncExecution::ThreadPool, [Index]()
		{
			const FString Name = FString::Printf(TEXT("Concurrent%d"), Index);
			const FString Script = FString::Printf(TEXT("fn %s*(): int { for i := 0; i < 3; i++ { printf(\"marker%d\\n\") }; return %d }"), *Name, Index, Index);
			FUEmkaScriptParam Result;
			FString Error;
			return UUEmkaFunctionLibrary::RunUmkaInline(nullptr, Script, Name, {}, EUEmkaValueType::Int, false, false, Result, Error)
				&& Error.IsEmpty() && Result.IntValue == Index;
		}));
	}
	for (TFuture<bool>& Task : Tasks)
	{
		TestTrue(TEXT("Concurrent execution returns its own result"), Task.Get());
	}
	const TArray<FUEmkaTestLog::FMessage> Messages = Log.GetMessages();
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FString Expected = FString::Printf(TEXT("[Concurrent%d] marker%d"), Index, Index);
		TestEqual(TEXT("Concurrent output retains its function attribution"), Messages.FilterByPredicate([&Expected](const auto& Message) { return Message.Text == Expected; }).Num(), 3);
	}
	TestEqual(TEXT("Concurrent calls do not mix or lose lines"), Messages.Num(), 24);
	return true;
}

#endif
