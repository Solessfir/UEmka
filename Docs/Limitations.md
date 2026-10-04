# Limitations

[Return to README](../README.md)

## Unsupported Blueprint pin shapes

The following Umka features are not currently supported as Blueprint pins:

- **Directly nested containers** - `[][]int`, arrays of maps, and maps with container values; a struct may contain supported arrays and maps
- **Unsupported map keys** - floating-point, struct, and container keys
- **Structs with unsupported fields** - pointers, interfaces, or function types
- **Closures / function types** - `fn(int): int` cannot be passed as a pin
- **Pointers** - `^type` and `weak ^type` are not supported
- **Pointers inside multi-return** - `fn foo*(): (^int, str)` is not supported
- **Interfaces** - `any` and other interface types cannot be passed as pins
- **Aliases of unsupported types** - resolving an alias does not make an unsupported shape eligible for pins
- **Unknown or undeclared types** - the Umka compiler must resolve exported signature types

These restrictions apply only to the exported function's signature, including its parameters and return type. Otherwise valid Umka features remain available inside the script as local variables, helper types, and intermediate values. For example, a local struct can feed a scalar result:

```umka
type Vec2 = struct { x, y: real }

fn length*(x: real, y: real): real {
    v := Vec2{x, y}
    return sqrt(v.x*v.x + v.y*v.y)
}
```

## Composite bounds

Composite values are limited to 16 nesting levels, 65,536 items per container, 262,144 total values, and 64 MiB of serialized data per value.

See [Writing Scripts](WritingScripts.md) for supported pin types, [Structs and Maps](StructsAndMaps.md) for wrapper constraints, [Execution and Debugging](ExecutionAndDebugging.md) for execution limits and errors, and [Testing](Testing.md) for validation coverage.
