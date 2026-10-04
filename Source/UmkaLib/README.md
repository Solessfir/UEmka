# Vendored Umka

Upstream: https://github.com/vtereshkov/umka-lang

Release: [v1.5.7](https://github.com/vtereshkov/umka-lang/releases/tag/v1.5.7)

Commit: `4f2ce3c6682d6e7870309f57728e46168827f0c5`

License: BSD-2-Clause, retained in [LICENSE](LICENSE).

The release's `src/umka_*.c` and `src/umka_*.h` files are compiled directly by UnrealBuildTool. The standalone CLI, `src/umka.c`, is excluded. `umka_api.h` lives in `Public`; the other source files live in `Private`. Source line endings are normalized to LF.

Local integration additions are confined to nine upstream files:

- `Public/umka_api.h`: 24 API typedefs, declarations, and entries appended to `UmkaAPI`, plus the map visitor callback typedef.
- `Private/umka_api.c`: a direct API-header include, metadata implementations for array shapes, sizes, struct fields, function signatures and defaults, enum bases, and declared type names, plus map construction, insertion, length, and traversal APIs and the filesystem-import switch. VM call/run error paths restore error-jumper nesting so a failed call cannot leave a stale native error target. Module registration catches compiler errors instead of jumping into a completed initialization call.
- `Private/umka_compiler.h` and `Private/umka_compiler.c`: initialization of those 24 appended API entries and compilation state so freeing a VM before compilation does not execute uninitialized cleanup bytecode.
- `Private/umka_common.h` and `Private/umka_common.c`: per-VM filesystem-import policy, enabled by default, plus rejection of module paths that would be truncated during normalization.
- `Private/umka_decl.c`: lookup of registered sources relative to the importing module, plus compiler rejection of unregistered imports when filesystem imports are disabled.
- `Private/umka_vm.h`: declarations for two native map allocation/insertion wrappers.
- `Private/umka_vm.c`: those wrappers and extraction of the existing map-index insertion body into a shared helper. Script indexing retains its existing allocation and reference-count behavior.

Function default values use the same zero-based parameter indices as the signature metadata APIs, excluding the hidden upvalue/receiver and structured result parameters. `umkaGetFuncParamDefaultValue` returns false for required parameters and invalid arguments. Scalar values use the stack-slot representation, including `realVal` for `real32`; structured values use `ptrVal`. Returned pointers belong to compiler storage, remain valid until `umkaFree`, and must not be released or mutated by callers.

Pass a zero-initialized map or an existing valid map to `umkaMakeMap`. It replaces the existing map and allocates an empty native map of the supplied type. `umkaEnsureMapItem` creates a missing key with a zero-initialized value and returns writable value storage; the existing `umkaGetMapItem` remains lookup-only. Insertion retains reference-counted keys. Callers must retain reference-counted values they store and release replaced values, as with other native Umka containers. Invalid arguments and dead VMs return without allocation. Outside a running VM call, runtime allocation/insertion errors are caught and reported through `umkaGetError`; insertion returns null on failure.

`umkaGetMapLen` returns zero for null or empty maps and -1 for an invalid populated map. `umkaVisitMap` traverses entries once in key order with a heap-backed traversal stack, borrowing stack-slot keys and value pointers without retaining them. The visitor must not mutate the map and may return false to stop traversal. The API returns true when traversal completes, including an empty map, and false for invalid arguments, unsupported key decoding, traversal allocation failure, or an early stop.

`umkaSetFileImportsEnabled` changes module-import fallback after initialization and before compilation. Disabling it keeps registered source strings and builtin modules available while rejecting imports that would read a file. This is independent of the runtime filesystem builtin policy passed to `umkaInit`; inline scripts preserve the upstream import behavior, while script assets compile entirely from their retained module graph.

`umkaGetTypeNameInMainModule` resolves declared type names using the compiler's main-module import aliases. Imported names use `alias::Type`; types exported through a visible equivalent alias use that alias. It returns null when no declaration is visible to the main module, and its returned storage belongs to the VM. This allows generated wrappers to refer to imported struct and enum types without parsing import declarations separately.

`UmkaLib.Build.cs` and `Private/UmkaLibModule.cpp` are Unreal integration files. Future updates should replace the full upstream source set and license, then reapply the local additions and validate editor and runtime builds plus the UEmka automation tests. Keep existing upstream API entries in order.
