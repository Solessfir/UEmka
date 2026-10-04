// Copyright Solessfir 2026. All Rights Reserved.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "umka_api.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEmkaNativeMapAfterFailureTest, "UEmka.Runtime.NativeMapAfterFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEmkaNativeMapAfterFailureTest::RunTest(const FString& Parameters)
{
	for (const bool bRun : {false, true})
	{
		const FString Script = TEXT("fn Map*(M: map[int8]int, Count: int = -3): int { return len(M) }\n")
			TEXT("fn Fail*(): int { Zero := 0; return 1 / Zero }\n")
			+ FString(bRun ? TEXT("fn main() { Fail() }") : TEXT("fn main() {}"));
		const FTCHARToUTF8 Source(*Script);
		Umka* Vm = umkaAlloc();
		if (!TestNotNull(TEXT("Native VM allocated"), Vm)) continue;
		if (!TestTrue(TEXT("Native VM initialized"), umkaInit(Vm, "native-map.um", Source.Get(), 10000, nullptr, 0, nullptr, false, false, nullptr))
			|| !TestTrue(TEXT("Native map fixture compiles"), umkaCompile(Vm)))
		{
			umkaFree(Vm);
			continue;
		}

		const UmkaType* FunctionType = umkaGetFuncType(Vm, nullptr, "Map");
		const UmkaType* MapType = umkaGetFuncParamTypeByIndex(FunctionType, 0);
		UmkaStackSlot Default = {};
		TestFalse(TEXT("Required map has no default"), umkaGetFuncParamDefaultValue(FunctionType, 0, &Default));
		TestTrue(TEXT("Scalar default metadata exists"), umkaGetFuncParamDefaultValue(FunctionType, 1, &Default));
		TestEqual(TEXT("Scalar default metadata preserves value"), static_cast<int64>(Default.intVal), int64{-3});
		UmkaMap Map = {};
		umkaMakeMap(Vm, &Map, MapType);
		for (const int64 Key : {int64{2}, int64{1}})
		{
			UmkaStackSlot KeySlot = {};
			KeySlot.intVal = Key;
			TestNotNull(TEXT("Native map item created"), umkaEnsureMapItem(Vm, &Map, KeySlot));
		}
		TArray<int64> Keys;
		TestTrue(TEXT("Native visitor completes"), umkaVisitMap(&Map, [](UmkaStackSlot Key, const void*, void* User)
		{
			static_cast<TArray<int64>*>(User)->Add(Key.intVal);
			return true;
		}, &Keys));
		TestTrue(TEXT("Native visitor follows key order"), Keys == TArray<int64>({1, 2}));
		int32 Visits = 0;
		TestFalse(TEXT("Native visitor supports early stop"), umkaVisitMap(&Map, [](UmkaStackSlot, const void*, void* User)
		{
			++*static_cast<int32*>(User);
			return false;
		}, &Visits));
		TestEqual(TEXT("Early stop invokes visitor once"), Visits, 1);

		UmkaFuncContext Context = {};
		if (!TestTrue(TEXT("Failing function resolved"), umkaGetFunc(Vm, nullptr, "Fail", &Context)))
		{
			umkaDecRef(Vm, Map.root);
			umkaFree(Vm);
			continue;
		}
		TestTrue(TEXT("VM failure returns an error"), (bRun ? umkaRun(Vm) : umkaCall(Vm, &Context)) != 0);
		TestFalse(TEXT("Failed VM is dead"), umkaAlive(Vm));
		const auto* Root = Map.root;
		UmkaStackSlot InvalidKey = {};
		InvalidKey.intVal = 1000;
		TestNull(TEXT("Dead VM rejects map insertion"), umkaEnsureMapItem(Vm, &Map, InvalidKey));
		umkaMakeMap(Vm, &Map, MapType);
		TestTrue(TEXT("Dead VM preserves existing map allocation"), Map.root == Root);
		TestEqual(TEXT("Dead VM preserves existing map entries"), umkaGetMapLen(&Map), 2);
		umkaDecRef(Vm, Map.root);
		umkaFree(Vm);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
