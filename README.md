# UEmka

**UEmka** embeds [Umka](https://github.com/vtereshkov/umka-lang), a statically typed scripting language, directly into Unreal Engine Blueprint nodes.

Bundled interpreter: [Umka 1.5.7](https://github.com/vtereshkov/umka-lang/releases/tag/v1.5.7).

## Installation

Download `UEmka.zip` from the [releases](https://github.com/Solessfir/UEmka/releases) and extract it into your project's `Plugins` folder.

## Quick start

1. Add an **Umka Script** node to a Blueprint event graph.
2. Enter an exported function in the node's code editor:

```umka
fn twice*(Value: int): int {
    return Value * 2
}
```

The `*` makes the function callable from Blueprint. This example creates an Integer64 input named `Value` and an Integer64 result output.

3. Connect the execution pins, provide `Value`, and use the result output.
4. Compile the Blueprint.

![Example Blueprint](Resources/Screenshot.png)

## Documentation

| Guide | Topics |
| --- | --- |
| [Writing scripts](Docs/WritingScripts.md) | Functions, scalar types, enums, defaults, and multiple results |
| [Arrays](Docs/Arrays.md) | Dynamic and fixed arrays, pin mappings, and examples |
| [Structs and maps](Docs/StructsAndMaps.md) | Flattened fields, native struct pins, and map values |
| [Unreal types](Docs/UnrealTypes.md) | Vector, Rotator, LinearColor, Quat, and Transform |
| [Reusable modules](Docs/Modules.md) | Script assets, imports, and function selection |
| [Execution and debugging](Docs/ExecutionAndDebugging.md) | Error outputs, logging, instruction and heap budgets, cancellation, and session resets |
| [C++ host functions](Docs/HostFunctions.md) | Native callbacks, caller/world context, and safe UObject handles |
| [Limitations](Docs/Limitations.md) | Supported pin shapes and value limits |
| [Testing](Docs/Testing.md) | Editor automation and cooked validation |

See the [Umka language reference](https://github.com/vtereshkov/umka-lang/blob/v1.5.7/doc/lang.md) for language syntax and the [vendoring notes](Source/UmkaLib/README.md) for interpreter integration details.

## License

Licensed under the [MIT License](LICENSE).
