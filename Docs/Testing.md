# Testing

[Return to README](../README.md)

## Editor automation

Run the `UEmka` group in Unreal Editor's Automation window, or launch the editor with:

```text
-unattended -nullrhi -nosound -ExecCmds="Automation RunTests UEmka;SoftQuit"
```

The suite covers every supported scalar type and enum integer base, aliases, dynamic and fixed arrays, empty containers, integer boundaries, Unicode strings, compiler-evaluated defaults, nested structs, mixed tuples, native scalar and struct-valued maps, native struct arrays, and optional native individual struct pins. Blueprint tests compile and execute supported mappings through connected input and output pins.

Additional tests cover native default editing/splitting, exported function selection, module graphs and imported types, malformed composite data, rejected signatures, runtime failures and output resets, logging and concurrent capture, graph reconstruction, transaction undo/redo, compiled Blueprint and native struct package save/load, embedded editor input, and syntax highlighting.

Execution tests also cover optional status outputs, native Unreal struct identity and defaults, VM instruction and heap limits across fibers and native marshaling, cancellation, persistent sessions and Blueprint resets, managed value ownership, runtime stack traces, host caller/world context, and UObject handle invalidation after reset or garbage collection.

## Cooked validation

Run the editor commandlet in a disposable C++ host project:

```text
-run=UEmkaCookFixture
```

Cook its `Content/UEmkaCookValidation` directory, then run its Development game with:

```text
-ExecCmds="Automation RunTests UEmka.Cooked;SoftQuit"
```

Cooked fixtures exercise the supported interfaces through compiled Blueprint functions. The commandlet creates fixture assets only when explicitly invoked; normal editor automation tests do not create game content.

The disposable host must register `cook/host.um` during cook startup with `fn UEmkaCookHostDouble*(Value: int): int` and a callback returning twice `Value`. Cooking recompiles fixture Blueprints, so these declarations must be available before validation. The fixture commandlet and cooked host test register this module themselves. Modules using native callbacks need `UEmka` and `UmkaLib` in their build dependencies.

## Scope

These are integration and regression tests for the plugin's supported interfaces, rather than exhaustive tests of the Umka language or a measured line-coverage guarantee. Platform-specific logging behavior still needs validation on each supported platform.

## Windows validation - 2026-10-05

Validated in a disposable C++ host with Unreal Engine 5.8.3 (CL 58210709), MSVC 14.51.36260, and Windows SDK 10.0.28000.0:

- Win64 Development Editor and game builds passed.
- All 93 editor automation tests passed, including heap limits, Blueprint resets, host caller/world context, weak UObject handles, and native finalizer isolation.
- Cooking and staging passed. All 4 cooked tests passed, including compiled Blueprint heap limits, session resets, callback execution, assets, and native types.

Automation ran with `-unattended -nullrhi -nosound`. Unreal warned that this MSVC version is newer than its preferred version. The cooked run exported JSON results; its HTML report template was unavailable.

See [Writing Scripts](WritingScripts.md), [Execution and Debugging](ExecutionAndDebugging.md), [Host Functions](HostFunctions.md), and [Limitations](Limitations.md) for the behavior under test.
