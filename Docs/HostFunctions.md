# C++ Host Functions

[Return to README](../README.md)

Add `UEmka` and `UmkaLib` to your module dependencies, include `UEmkaHostFunctions.h`, and register a relative `.um` module source with its `FFunction` callbacks through `UEmkaHostFunctions::RegisterModule`. Scripts import that module normally; it is available during Blueprint compilation and cooked execution. Native function names are unique across all registered modules. Builtin module paths and `rtl` function names are reserved.

```cpp
static void GameTwice(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
    umkaGetResult(Params, Result)->intVal = umkaGetParam(Params, 0)->intVal * 2;
}

const TArray<UEmkaHostFunctions::FFunction> Functions = {{TEXT("GameTwice"), &GameTwice}};
FString Error;
const bool bRegistered = UEmkaHostFunctions::RegisterModule(
    TEXT("game.um"), TEXT("fn GameTwice*(Value: int): int"), Functions, Error);
```

Scripts can then `import "game.um"` and call `game::GameTwice(21)`.

## Registration and lifetime

Register modules during application module startup so their declarations are available in the editor and cook commandlet as well as the game. Registration and unregistration happen on the game thread and discard idle sessions. They fail while a VM is active. Sources are copied, but callback code must remain loaded until `UnregisterModule` succeeds. This explicit API does not expose UObjects automatically.

See [Modules](Modules.md) for script asset imports, [Execution and Debugging](ExecutionAndDebugging.md) for sessions and cancellation, and [Testing](Testing.md) for cooked validation.
