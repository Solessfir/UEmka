# Writing scripts

An Umka Script node calls an exported function and creates Blueprint pins from its signature. This page covers scalar types, defaults, and return values.

[Plugin overview](../README.md) | [Arrays](Arrays.md) | [Structs and maps](StructsAndMaps.md) | [Unreal types](UnrealTypes.md)

[Exports](#exports) | [Scalar types](#scalar-types) | [Multiple return values](#multiple-return-values) | [Defaults](#default-parameters) | [Fibonacci](#example---fibonacci)

## Exports

`*` is Umka's export marker. It makes an identifier visible outside its module, similar to Go's uppercase convention. The embedding API can only find exported functions.

```umka
fn helper(x: int): int {  // private - usable inside the script only
    return x * 2
}

fn run*(x: int): int {    // exported - the node calls this
    return helper(x)
}
```

- The function the node calls must have `*`.
- Internal helpers do not need `*`.
- `main` is reserved as the zero-parameter program entry point. Never name your exported function `main`.

## Scalar types

| Umka type | Blueprint pin |
|-----------|---------------|
| `int` | Integer64 |
| `int8` `int16` `int32` | Integer |
| `uint8` `char` | Byte |
| `uint16` | Integer |
| `uint32` | Integer64 |
| `uint` | Integer64 |
| `bool` | Boolean |
| `real` | Double |
| `real32` | Float |
| `str` | String |
| User-defined `enum` type | Pin for its integer base (Integer64 by default) |

A function with no return type produces no output pin.

### Enums and integer ranges

Enum pins preserve the signedness and range of their declared integer base. The pin label shows the enum name:

```umka
type Direction = enum { North; East; South; West }

fn opposite*(d: Direction): Direction {
    return Direction((int(d) + 2) % 4)
}
```

This generates an Integer64 input labeled `d (Direction)` and an Integer64 output labeled `Direction`.

The type definition must be present for Umka to compile. Its named constants (`North`, `East`, etc.) are available anywhere in the script body. Enums at the pin boundary, including struct fields, require a named type declaration.

Enum arrays use the same base mapping: `[]Direction` becomes an Array of Integer64. An explicit `enum (uint8)` base uses Byte pins, while `enum (int8)` uses Integer pins and preserves negative values.

Integer inputs outside the compiled Umka type's range fail with a runtime error instead of truncating. `uint32` uses Integer64 to represent `0..4294967295`. Blueprint has no unsigned 64-bit pin, so `uint` preserves all 64 bits in Integer64: negative pin values represent the unsigned upper half.

Existing Blueprint connections to enum or `uint32` pins may need reconnecting after their pin types change. Recompile affected Blueprints after updating the plugin.

### Aliases and raw strings

Valid scripts resolve type aliases through the Umka compiler. Aliases of supported scalars, enums, arrays, structs, and maps use the same pins as their underlying types. Unknown types and aliases of unsupported shapes produce compile errors. During incomplete edits, a source-based preview remains available for explicitly declared supported types.

Raw strings enclosed in backticks can span lines. Their contents, including quotes, comment markers, and backslashes, remain literal and do not affect function discovery or syntax highlighting.

## Multiple return values

Wrap return types in parentheses to get one output pin per value:

```umka
fn minmax*(a: int, b: int): (int, int) {
    if a < b {
        return a, b
    }
    return b, a
}
```

This generates two Integer64 outputs named `ReturnValue1` and `ReturnValue2`. Further values use `ReturnValue3`, etc.

| Umka syntax | Output pins |
|-------------|-------------|
| `fn foo*(): (int, str)` | `ReturnValue1`: Integer64, `ReturnValue2`: String |
| `fn foo*(): (real32, bool)` | `ReturnValue1`: Float, `ReturnValue2`: Boolean |

A single-element list such as `(int)` is treated the same as a plain `int` return. Structs in a return tuple are recursively flattened into fields in declaration order by default. Arrays and maps remain single output pins. See [array tuple returns](Arrays.md#multiple-array-returns) and [native struct pins](StructsAndMaps.md#native-struct-pins).

## Default parameters

Unconnected inputs use defaults evaluated by the Umka compiler, including constant expressions, enums, strings, arrays, and supported structs. Scalar defaults appear directly on the pins. Defaults for struct parameters apply to their flattened fields or native struct pin. Connecting an input or editing its default overrides the script default.

```umka
fn scale*(value: real, factor: real = 2): real {
    return value * factor
}
```

The `factor` input starts at `2`. Leave it unconnected to use that default, edit the pin value, or connect another Double value.

## Example - Fibonacci

Computes the N-th Fibonacci number:

```umka
fn fib*(n: int): int {
    if n <= 1 {
        return n
    }
    a := 0
    b := 1
    for i := 2; i <= n; i++ {
        t := a + b
        a = b
        b = t
    }
    return b
}
```

This generates one Integer64 input (`n`) and one Integer64 output.
