# Testing

[Return to README](../README.md)

## Editor automation

Run the `UEmka` group in Unreal Editor's Automation window, or launch the editor with:

```text
-unattended -nullrhi -nosound -ExecCmds="Automation RunTests UEmka;SoftQuit"
```

The suite covers every supported scalar type and enum integer base, aliases, dynamic and fixed arrays, empty containers, integer boundaries, Unicode strings, compiler-evaluated defaults, nested structs, mixed tuples, native scalar and struct-valued maps, native struct arrays, and optional native individual struct pins. Blueprint tests compile and execute supported mappings through connected input and output pins.

Additional tests cover native default editing/splitting, exported function selection, module graphs and imported types, malformed composite data, rejected signatures, runtime failures and output resets, logging and concurrent capture, graph reconstruction, transaction undo/redo, compiled Blueprint and native struct package save/load, embedded editor input, and syntax highlighting.

Execution tests also cover optional status outputs, native Unreal struct identity and defaults, VM instruction limits across fibers and nested calls, cancellation, persistent sessions and invalidation, managed value ownership, runtime stack traces, and host callback lifetimes.

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

## Scope

These are integration and regression tests for the plugin's supported interfaces, rather than exhaustive tests of the Umka language or a measured line-coverage guarantee. Platform-specific logging behavior still needs validation on each supported platform.

See [Writing Scripts](WritingScripts.md), [Execution and Debugging](ExecutionAndDebugging.md), [Host Functions](HostFunctions.md), and [Limitations](Limitations.md) for the behavior under test.
