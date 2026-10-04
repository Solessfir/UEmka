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

## Caller and world context

During a game-thread callback, `UEmkaHostFunctions::GetCurrentContext()` returns the active `FCallContext`. `GetCaller()` returns the Blueprint caller, `GetWorld()` returns its world, and `GetSessionId()` returns the configured session ID. A caller without a world returns `nullptr` from `GetWorld()`; calls without a caller still have a context. Caller and world are weak references and are checked when accessed.

The context is available for scalar, multi-return, inline, and asset execution. Nested script calls receive their own context and restore the outer context on return. Outside execution, during compile checks, and on worker threads, `GetCurrentContext()` returns `nullptr`. VM cleanup callbacks receive an empty context with no caller, world, session ID, or usable object handles. Never retain the context pointer or use it after the callback returns.

## Safe UObject handles

`FCallContext::CreateObjectHandle(Object)` returns an opaque `uint64` token, suitable for an Umka `uint`. Zero represents an invalid handle. Use `ResolveObjectHandle(Handle)` within a later callback to retrieve the object, then perform normal Unreal type checks and validation before using it.

```cpp
static void GameCallerHandle(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
    const auto* Context = UEmkaHostFunctions::GetCurrentContext();
    umkaGetResult(Params, Result)->uintVal = Context ? Context->CreateObjectHandle(Context->GetCaller()) : 0;
}

static void GameHasObject(UmkaStackSlot* Params, UmkaStackSlot* Result)
{
    const auto* Context = UEmkaHostFunctions::GetCurrentContext();
    const uint64 Handle = umkaGetParam(Params, 0)->uintVal;
    umkaGetResult(Params, Result)->intVal = Context && Context->ResolveObjectHandle(Handle) != nullptr;
}
```

Register these with declarations `fn GameCallerHandle*(): uint` and `fn GameHasObject*(Handle: uint): bool`. Scripts can store tokens in globals between calls to a retained session. Tokens belong to their compiled VM: another session or caller cannot resolve them, and a session reset, source change, failed execution that discards the VM, or one-shot execution end invalidates them. Handles use weak references and never keep UObjects alive. Collected objects, null handles, and unknown tokens resolve to `nullptr`; tokens are never recycled. Do not convert tokens to pointers. Handles validate identity and lifetime; each callback remains responsible for which objects and operations it permits.

See [Modules](Modules.md) for script asset imports, [Execution and Debugging](ExecutionAndDebugging.md) for sessions and cancellation, and [Testing](Testing.md) for cooked validation.
