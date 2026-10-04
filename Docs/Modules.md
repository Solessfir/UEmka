# Reusable Scripts and Modules

[Return to README](../README.md)

Create a **UEmka Script Asset** in the Content Browser and edit its **Source** in the asset's Details panel. Assign it to a node's **Script Asset** property to reuse the same script in multiple graphs. The node displays the asset's source read-only and provides a link to open the asset. Clearing the asset restores the node's inline script.

## Module paths and imports

Each asset has a **Module Path** and an **Imports** list. An empty path uses the asset name plus `.um`. Explicit paths must be relative `.um` filenames with forward slashes, no empty or `.`/`..` segments, and at most 255 UTF-8 bytes. Paths identify sources within the shared virtual module root; import strings resolve relative to the importing module.

For example, give a helper asset the path `lib/helpers.um` and source:

```umka
fn twice*(value: int): int { return value * 2 }
```

Give the main asset the path `main.um`, add the helper asset to its **Imports**, and use:

```umka
import helpers = "lib/helpers.um"

fn calculate*(value: int = 7): int {
    return helpers::twice(value)
}
```

Imports may reference assets that import further assets. Missing references, cycles, and conflicting module paths produce errors. Imported types used by generated wrappers must be accessible through the main module's import aliases. Changing a referenced asset refreshes loaded nodes and marks their Blueprints for recompilation.

The compiled Blueprint keeps a hard reference to its source asset, and assets retain their transitive imports. Source text and module dependencies remain available in cooked builds. Asset-backed scripts load imports from these assets and Umka's embedded standard modules; they do not fall back to loose source files on disk.

## Selecting a function

The function selector chooses any exported function in the script. **Automatic (first export)** preserves the original behavior. A missing, private, or differently capitalized selection produces a compile error. Select the node to configure its script asset and native struct pins in the Details panel.

See [Writing Scripts](WritingScripts.md) for exported signatures, [Host Functions](HostFunctions.md) for native modules, and [Execution and Debugging](ExecutionAndDebugging.md) for session import rules.
