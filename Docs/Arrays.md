# Arrays

Dynamic and fixed-size Umka arrays cross the node boundary as Blueprint arrays while retaining their native Umka ABI.

[Plugin overview](../README.md) | [Writing scripts](WritingScripts.md) | [Structs and maps](StructsAndMaps.md) | [Unreal types](UnrealTypes.md)

## Array pins

Prefix any supported scalar or struct type with `[]` or `[N]` to get an array pin. A fixed-size input must contain exactly `N` elements; otherwise execution fails with an error reporting the required and supplied lengths. Constant expressions in `[N]` are supported because the final length is read from the compiled Umka type.

```umka
fn double*(nums: []int): []int {
    res := make([]int, len(nums))
    for i, v in nums {
        res[i] = v * 2
    }
    return res
}
```

| Umka array type | Blueprint array pin |
|-----------------|---------------------|
| `[]int` | Array of Integer64 |
| `[]int8` `[]int16` `[]int32` | Array of Integer |
| `[]uint8` `[]char` | Array of Byte |
| `[]uint16` | Array of Integer |
| `[]uint32` | Array of Integer64 |
| `[]uint` | Array of Integer64 |
| `[]bool` | Array of Boolean |
| `[]real` | Array of Double |
| `[]real32` | Array of Float |
| `[]str` | Array of String |
| `[]MyEnum` (user-defined enum) | Array for its integer base |
| `[]MyStruct` | Array of generated Blueprint structs |

The same mapping applies to fixed-size forms such as `[4]int` and `[Count]real`. Fixed-size return values are copied from Umka's inline array storage back into the Blueprint array.

See [scalar and enum mappings](WritingScripts.md#scalar-types), [generated struct types](StructsAndMaps.md#native-struct-pins), and [native Unreal types](UnrealTypes.md).

## Multiple array returns

Array types are supported in return tuples:

```umka
fn splitEven*(nums: []int): ([]int, []int) {
    evens := make([]int, 0)
    odds  := make([]int, 0)
    for _, v in nums {
        if v % 2 == 0 {
            evens = append(evens, v)
        } else {
            odds = append(odds, v)
        }
    }
    return evens, odds
}
```

This generates `ReturnValue1`: Array of Integer64 and `ReturnValue2`: Array of Integer64. See [multiple return values](WritingScripts.md#multiple-return-values) for naming and flattening rules.
