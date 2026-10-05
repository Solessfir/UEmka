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

Enable **Show Success and Error Pins** in Details to add `Success` and `Error` output pins. Execution continues through `Then` on either outcome; failed calls clear their value outputs. Runtime errors include an Umka call stack.

Status outputs use `RuntimeSuccess` or `RuntimeError` when a return field already uses `Success` or `Error`; a numeric suffix resolves further collisions.

## Instruction limits and cancellation

**Umka > Max Instructions** limits VM instructions per call, including loops, fibers, and nested calls. Zero disables the instruction limit. The C++ `CancelExecution(Caller, SessionId)` API can request cancellation from another thread; the VM checks at the first instruction and every 1,024 instructions. Neither control interrupts a native callback or builtin while it is blocked.

## Preserving script state

Enable **Preserve Script State** under **Umka** to keep script globals between calls on the same node and Blueprint instance. Other nodes and Blueprint instances have independent state, even when they use the same script asset. The compiled VM is reused for these calls. Changing the script, selected function, or any asset module source rebuilds the VM. A fatal runtime error also rebuilds it on the next call. Imports must come from script assets, builtin modules, or registered host modules; loose file imports are disabled in this mode. Fresh execution remains the default.

```umka
var Calls: int

fn count*(): int {
    Calls++
    return Calls
}
```

With the default fresh execution, each call returns `1`. With **Preserve Script State** enabled, repeated calls on the same node and Blueprint instance return `1`, `2`, `3`, and so on.

Enabling **Preserve Script State** adds a **Reset Script State** Boolean input. Set it to `true` to discard this node's state for the current Blueprint instance before that execution. Keep it `false` to preserve state. The input remains independent of script parameters with the same name.

The Blueprint **Reset Runtime Sessions For Caller** node resets all sessions belonging to its `Caller`, which defaults to the current Blueprint instance. It returns the number of sessions reset. **Reset All Runtime Sessions** resets sessions for every caller. The C++ `ResetRuntimeSession(Caller, SessionId)` API resets one node and returns whether it found a session.

Resetting an active session requests cancellation and destroys its VM only after the call unwinds. A reset requested from a native callback takes effect after the callback returns; reentry remains rejected while the old call is active. Dead owners are cleaned after garbage collection; world cleanup and engine shutdown reset sessions. Session VMs cannot execute concurrently or reenter the same session.

## VM heap limits

**Umka > Max Heap Bytes** caps the VM's reserved heap memory, including heap page headers, retained recycled pages, and fiber stacks. Zero disables this memory limit. The budget applies before compilation, while parameters are marshaled, and during script execution. Preserved script state retains its heap between calls, so lowering its budget below its current reservation fails that call.

This limit excludes the compiler's general storage and Unreal-side parameter and result containers. The VM needs memory for its initial stack even when a script allocates nothing, so allow room for that baseline. Exceeding the cap reports `Heap budget exceeded`, clears output values, and rebuilds a failed session on its next call.

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
