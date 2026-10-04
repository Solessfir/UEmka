# Vendored Umka

Upstream: https://github.com/vtereshkov/umka-lang

Release: [v1.5.7](https://github.com/vtereshkov/umka-lang/releases/tag/v1.5.7)

Commit: `4f2ce3c6682d6e7870309f57728e46168827f0c5`

License: BSD-2-Clause, retained in [LICENSE](LICENSE).

The release's `src/umka_*.c` and `src/umka_*.h` files are compiled directly by UnrealBuildTool. The standalone CLI, `src/umka.c`, is excluded. `umka_api.h` lives in `Public`; the other source files live in `Private`. Source line endings are normalized to LF.

Local integration additions are confined to three upstream files:

- `Public/umka_api.h`: 17 metadata API typedefs, declarations, and entries appended to `UmkaAPI`.
- `Private/umka_api.c`: a direct API-header include and metadata implementations for array shapes, sizes, struct fields, function signatures, enum bases, and declared type names.
- `Private/umka_compiler.c`: initialization of those 17 appended API entries.

`UmkaLib.Build.cs` and `Private/UmkaLibModule.cpp` are Unreal integration files. Future updates should replace the full upstream source set and license, then reapply the three API additions and validate editor and runtime builds plus the UEmka automation tests. Keep existing upstream API entries in order.
