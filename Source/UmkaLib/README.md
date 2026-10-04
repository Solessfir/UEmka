# Vendored Umka

Upstream: https://github.com/vtereshkov/umka-lang

Release: [v1.5.7](https://github.com/vtereshkov/umka-lang/releases/tag/v1.5.7)

Commit: `4f2ce3c6682d6e7870309f57728e46168827f0c5`

License: BSD-2-Clause, retained in [LICENSE](LICENSE).

The release's `src/umka_*.c` and `src/umka_*.h` files are compiled directly by UnrealBuildTool. The standalone CLI, `src/umka.c`, is excluded. `umka_api.h` lives in `Public`; the other source files live in `Private`. Source line endings are normalized to LF.

Local integration additions are confined to nine upstream files:

- `Public/umka_api.h`: 27 API typedefs, declarations, and entries appended to `UmkaAPI`, plus the map visitor and cancellation callback typedefs.
- `Private/umka_api.c`: a direct API-header include, metadata implementations for array shapes, sizes, struct fields, function signatures and defaults, enum bases, declared type names, module paths, and alias identity, plus map construction, insertion, length, and traversal APIs, the filesystem-import switch, and execution-budget configuration. VM call/run error paths restore error-jumper and interpreter nesting so a failed call cannot leave a stale native error target. Runtime source and call-stack lookup handle instruction pointers outside bytecode. Module registration catches compiler errors instead of jumping into a completed initialization call.
- `Private/umka_compiler.h` and `Private/umka_compiler.c`: initialization of those 27 appended API entries and compilation state so freeing a VM before compilation does not execute uninitialized cleanup bytecode. Freeing disables instruction limits and cancellation before cleanup.
- `Private/umka_common.h` and `Private/umka_common.c`: per-VM filesystem-import policy, enabled by default, plus rejection of module paths that would be truncated during normalization.
- `Private/umka_decl.c`: lookup of registered sources relative to the importing module, plus compiler rejection of unregistered imports when filesystem imports are disabled.
- `Private/umka_vm.h`: declarations for two native map allocation/insertion wrappers and per-VM instruction-budget and cancellation state.
- `Private/umka_vm.c`: those wrappers and extraction of the existing map-index insertion body into a shared helper, plus budget and cancellation checks in the instruction dispatch loop. Script indexing retains its existing allocation and reference-count behavior.

Function default values use the same zero-based parameter indices as the signature metadata APIs, excluding the hidden upvalue/receiver and structured result parameters. `umkaGetFuncParamDefaultValue` returns false for required parameters and invalid arguments. Scalar values use the stack-slot representation, including `realVal` for `real32`; structured values use `ptrVal`. Returned pointers belong to compiler storage, remain valid until `umkaFree`, and must not be released or mutated by callers.

Pass a zero-initialized map or an existing valid map to `umkaMakeMap`. It replaces the existing map and allocates an empty native map of the supplied type. `umkaEnsureMapItem` creates a missing key with a zero-initialized value and returns writable value storage; the existing `umkaGetMapItem` remains lookup-only. Insertion retains reference-counted keys. Callers must retain reference-counted values they store and release replaced values, as with other native Umka containers. Invalid arguments and dead VMs return without allocation. Outside a running VM call, runtime allocation/insertion errors are caught and reported through `umkaGetError`; insertion returns null on failure.

`umkaGetMapLen` returns zero for null or empty maps and -1 for an invalid populated map. `umkaVisitMap` traverses entries once in key order with a heap-backed traversal stack, borrowing stack-slot keys and value pointers without retaining them. The visitor must not mutate the map and may return false to stop traversal. The API returns true when traversal completes, including an empty map, and false for invalid arguments, unsupported key decoding, traversal allocation failure, or an early stop.

`umkaSetFileImportsEnabled` changes module-import fallback after initialization and before compilation. Disabling it keeps registered source strings and builtin modules available while rejecting imports that would read a file. This is independent of the runtime filesystem builtin policy passed to `umkaInit`; inline scripts preserve the upstream import behavior, while script assets compile entirely from their retained module graph.

`umkaGetTypeNameInMainModule` resolves declared type names using the compiler's main-module import aliases. Imported names use `alias::Type`; types exported through a visible equivalent alias use that alias. It returns null when no declaration is visible to the main module, and its returned storage belongs to the VM. This allows generated wrappers to refer to imported struct and enum types without parsing import declarations separately.

`umkaGetTypeModulePath` returns the canonical declaration-module path directly from the type's identifier, without substituting structurally equivalent types or aliases. It returns null for anonymous types and invalid arguments. Returned paths belong to the VM and remain valid until `umkaFree`.

`umkaTypeSameDeclaration` checks whether a type shares the compiler declaration identity of the named global type in the specified module. Explicit aliases retain that identity; independently declared types with identical fields do not. Module paths are normalized like registered module paths, so callers may supply relative or canonical absolute paths. Invalid arguments and unknown declarations return false.

Call `umkaSetExecutionBudget` after initialization and before execution. A zero instruction limit disables counting; a null cancellation callback disables cancellation. Each top-level `umkaCall` or `umkaRun` starts with the full instruction budget. Nested native calls and all fibers share that budget. Cancellation is polled before the first instruction and every 1024 dispatched instructions thereafter. The callback must only read cancellation state, must not reenter or mutate the VM, and must synchronize access to state written by other threads. The setter and other VM APIs must run on the VM's owning thread. Limits apply to bytecode dispatch, so an ongoing native callback or blocking builtin must return before cancellation can be observed. Both exhaustion and cancellation use the normal runtime error report with source information and leave the VM dead; recovery requires a fresh VM. Compilation and final cleanup do not consume instruction budgets or poll cancellation.

`UmkaLib.Build.cs` and `Private/UmkaLibModule.cpp` are Unreal integration files. Future updates should replace the full upstream source set and license, then reapply the local additions and validate editor and runtime builds plus the UEmka automation tests. Keep existing upstream API entries in order.
