# Execution and Debugging

[Return to README](../README.md)

Write an exported function in an **Umka Script** node, connect its typed pins, and execute the node:

```umka
fn twice*(value: int): int {
    return value * 2
}
```

See [Writing Scripts](WritingScripts.md) for supported signatures and [Modules](Modules.md) for reusable assets and function selection.

## Runtime status

Enable **Expose Runtime Status** in Details to add `Success` and `Error` output pins. Execution continues through `Then` on either outcome; failed calls clear their value outputs. Runtime errors include an Umka call stack.

Status outputs use `RuntimeSuccess` or `RuntimeError` when a return field already uses `Success` or `Error`; a numeric suffix resolves further collisions.

## Instruction limits and cancellation

**Execution Options > Max Instructions** limits VM instructions per call, including loops, fibers, and nested calls. Zero keeps execution unlimited. The C++ `CancelExecution(Caller, SessionId)` API can request cancellation from another thread; the VM checks at the first instruction and every 1,024 instructions. Neither control interrupts a native callback or builtin while it is blocked.

## Persistent sessions

Enable **Use Session** to reuse the compiled VM and preserve global state between calls. Sessions belong to a caller and node GUID, so separate Blueprint instances and nodes remain independent. Changing the script, selected function, or any asset module source rebuilds the VM. A fatal runtime error also rebuilds it on the next call. Session imports must come from script assets, builtin modules, or registered host modules; loose file imports are disabled in this mode. Fresh execution remains the default.

```umka
var Calls: int

fn count*(): int {
    Calls++
    return Calls
}
```

With the default fresh execution, each call returns `1`. With **Use Session** enabled, repeated calls on the same node and Blueprint instance return `1`, `2`, `3`, and so on.

Use `ResetRuntimeSession(Caller, SessionId)` to discard an idle session, or `ResetAllRuntimeSessions()` to discard idle sessions and cancel active calls. Dead owners are cleaned after garbage collection; world cleanup and engine shutdown reset sessions. Session VMs cannot execute concurrently or reenter the same session.

## Script logging

`printf` output from your script is captured and forwarded to the Unreal Output Log under the `LogUEmka` category, prefixed with the function name:

```umka
fn foo*(x: int): int {
    printf("input: %d\n", x)
    return x * 2
}
```

```text
LogUEmka: [foo] input: 42
```

Captured output is limited to at most 64 KB per execution; the operating system's pipe capacity can lower this limit. Excess output is dropped and a truncation warning is logged. Concurrent script calls serialize stdout capture so their output keeps the correct function prefix.

## Error handling

**Compile-time:** The node validates your script every time you compile the Blueprint. Errors are reported in the compiler results panel and the error line is highlighted red in the editor.

**Runtime:** If initialization, parameter validation, or script execution fails, the error is logged to the Output Log under the `LogUEmka` category, including the calling Blueprint path and function name. Failed executions clear their outputs so values from an earlier call cannot be reused.

See [Host Functions](HostFunctions.md) for C++ callback registration and lifetime rules, and [Testing](Testing.md) for validation coverage.
