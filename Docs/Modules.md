# Reusable Scripts and Modules

[Return to README](../README.md)

Create a **UEmka Script Asset** in the Content Browser and edit its syntax-highlighted **Source** in the asset's Details panel. Assign it to a node's **Script Asset** property to reuse the same script in multiple graphs. The node displays the asset's source read-only and provides a link to open the asset. Clearing the asset restores the node's inline script.

New assets created from the asset menu start with empty **Source**. Add an exported function before assigning them to a node.

## Importing source files

Drag a `.um` file into the Content Browser, or use **Import**, to create a UEmka Script Asset bound to that file. Its syntax-highlighted **Source** is read-only: edit the `.um` file, then right-click the asset and choose **Reimport** to reload it. The source filename appears under **Import Settings**. Reimport preserves **Module Path** and **Imports**, and refreshes loaded Blueprint nodes. Missing source files leave the last imported code intact.

For automatic reload, enable **Editor Preferences > Loading & Saving > Auto Reimport > Monitor Content Directories**. Source files must be inside a monitored directory. Add external folders, such as this plugin's `Examples` folder, to **Directories to Monitor** and set their **Mount Point** to `/Game/`. This mount point controls where new assets are created; existing assets keep their location. Save the asset and recompile its Blueprint after the source changes.

The asset initially uses the source file's basename with a `.um` extension as its **Module Path**, so renaming the asset does not change its virtual filename. Assets created from the asset menu have no source-file binding and remain editable in Details.

Imports of other script files require corresponding script assets in the asset's **Imports** list. Importing a file copies its source; it does not automatically import its dependencies. Built-in modules such as `ue.um` remain available without additional assets.

## Module paths and imports

Each asset has a **Module Path** and an **Imports** list. An empty path uses the asset name plus `.um`. Explicit paths must be relative `.um` filenames with forward slashes, no empty or `.`/`..` segments, and at most 255 UTF-8 bytes. Details shows an error for invalid text and keeps the previous value when an invalid edit is committed. Paths identify sources within the shared virtual module root; import strings resolve relative to the importing module. This virtual path can be edited independently of the linked source file shown in **Import Settings**.

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

The function selector appears when a script exports multiple functions and lists their names. The first export is selected by default. Scripts with one export show no selector. A missing, private, or differently capitalized selection produces a compile error. Select the node to configure its script asset and native struct pins in the Details panel.

The Details panel's **Selected Function** dropdown lists exports from the attached asset, or the inline script when no asset is assigned. Assigning an asset selects its first export. Editing the source preserves the selected function while it still exists, otherwise it selects the first available export. Existing invalid selections loaded from a saved Blueprint still produce a compile error until a valid function is selected or the source is edited.

See [Writing Scripts](WritingScripts.md) for exported signatures, [Host Functions](HostFunctions.md) for native modules, and [Execution and Debugging](ExecutionAndDebugging.md) for session import rules.
