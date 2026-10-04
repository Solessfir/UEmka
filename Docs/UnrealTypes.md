# Unreal types

The built-in `ue.um` module provides Umka types that map directly to Unreal struct pins.

[Plugin overview](../README.md) | [Writing scripts](WritingScripts.md) | [Arrays](Arrays.md) | [Structs and maps](StructsAndMaps.md)

Import `ue.um` to use `ue::Vector`, `ue::Rotator`, `ue::LinearColor`, `ue::Quat`, and `ue::Transform`, including supported arrays, maps, and tuple results:

| Umka type | Unreal type | Fields |
| --- | --- | --- |
| `ue::Vector` | `FVector` | `X`, `Y`, `Z` |
| `ue::Rotator` | `FRotator` | `Pitch`, `Yaw`, `Roll` |
| `ue::LinearColor` | `FLinearColor` | `R`, `G`, `B`, `A` |
| `ue::Quat` | `FQuat` | `X`, `Y`, `Z`, `W` |
| `ue::Transform` | `FTransform` | `Rotation`, `Translation`, `Scale3D` |

```umka
import "ue.um"

fn move*(Position: ue::Vector, Offset: ue::Vector): ue::Vector {
    return ue::Vector{Position.X + Offset.X, Position.Y + Offset.Y, Position.Z + Offset.Z}
}
```

Vector, Rotator, and Quat fields use `real`; LinearColor fields use `real32`. Transform contains `Rotation`, `Translation`, and `Scale3D`, and converts through Unreal's public transform accessors.

Aliases retain these native mappings. Independent structs with matching names or fields remain user-defined structs.
