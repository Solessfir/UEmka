# Structs and maps

Script structs can use flattened fields or native Blueprint struct pins. Maps use native Blueprint Map pins.

[Plugin overview](../README.md) | [Writing scripts](WritingScripts.md) | [Arrays](Arrays.md) | [Unreal types](UnrealTypes.md)

## Flattened structs

Structs declared in the script can appear in an exported function's signature. By default, the node flattens each struct into one pin per field:

```umka
type Vec2 = struct { x, y: real }

fn move*(p: Vec2, d: Vec2, scale: real): Vec2 {
    return Vec2{x: p.x + d.x*scale, y: p.y + d.y*scale}
}
```

This generates Double inputs `p.x`, `p.y`, `d.x`, `d.y`, and `scale`, and Double outputs `x` and `y` for the returned struct's fields.

The node compiles a small wrapper with a flat signature that packs pins into struct values, calls the function, and unpacks the result. The script is unchanged.

Fields may contain supported scalars, arrays, maps, and nested structs. Nested struct fields are recursively flattened: `p.position.x` becomes a scalar pin while `p.samples` remains an array pin.

## Native struct pins

Struct arrays use native Blueprint struct arrays. Their element types are generated from the compiled field layout and saved with the owning Blueprint package. Fields in these native structs can include nested structs, arrays, and maps. Identical field layouts share a generated type within the package.

Enable **Use Custom Struct Pins** in the node's Details panel to pass individual custom structs through one native Blueprint pin. Standard Make/Break and split-pin workflows are available. Custom structs in return tuples then occupy one output pin each. Existing nodes retain flattened custom fields by default. Compiler defaults populate native input pins, including nested fields, and edited defaults override them. Built-in `ue.um` structs such as Vector and Rotator always use their Unreal struct pins, regardless of this option.

Native pins keep related fields together and work well when passing a complete struct. Custom native struct types are generated from the field layout and saved inside the owning Blueprint package; a layout change can change the pin type and require reconnecting links. Flattened pins avoid generated struct types and let scalar fields connect directly to existing logic. Changing this option on a wired node may also require reconnecting its pins.

### Constraints

- Native Blueprint struct field names must be unique without regard to case.
- Flattened input pin names must be unique without regard to case.
- The input name `execute` is reserved for Blueprint execution.
- A generated wrapper supports up to 15 flattened inputs, or 14 when returning arrays, maps, or multiple values.
- The identifier `__uemka_call` is reserved for the generated wrapper.

See [Unreal types](UnrealTypes.md) for the built-in `ue.um` structs that map directly to Unreal types.

## Maps

`map[K]V` becomes a native Blueprint Map pin. Keys can be any supported integer type, a named enum, or `str`. Values can be any supported scalar, named enum, or supported struct. Integer ranges follow the [scalar pin rules](WritingScripts.md#enums-and-integer-ranges).

```umka
fn increment*(counts: map[str]int): map[str]int {
    counts["calls"]++
    return counts
}
```

Maps are also supported in struct fields and return tuples. Struct values use generated native Blueprint structs and may contain nested structs, arrays, and supported maps. Direct container values and floating-point keys cannot cross Blueprint pins.

Blueprint string keys compare without regard to case. An Umka map containing keys that differ only by case cannot be converted and reports a duplicate-key error.
