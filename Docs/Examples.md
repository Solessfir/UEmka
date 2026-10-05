# Starter script assets

[Plugin overview](../README.md)

These **UEmka Script Assets** demonstrate common gameplay calculations. Blueprint supplies the inputs and applies the results to actors, variables, or UI.

The assets directly in `Content/Umka` contain their script in **Source** and have no external `.um` dependency. Duplicate one into your project's Content folder, then edit its **Source** directly. Each also has a matching source file in the plugin's [Examples](../Examples) folder for optional file import or copying into an inline **Umka Script** node.

Open **FileBacked / HealthColor** to inspect the file-linked example. Its **Source** is read-only and **Import Settings** shows the path to `Examples/HealthColor.um`. Edit that file and choose **Reimport** on the asset to update its code.

To create your own file-backed asset, drag a matching `Examples/*.um` file into your project's Content Browser folder. Its **Source** becomes read-only; edit the linked `.um` file and use **Reimport**. See [Importing source files](Modules.md#importing-source-files) to enable automatic reload from monitored folders.

## Use an example

1. Enable **Show Plugin Content** in the Content Browser's Settings menu, then open **UEmka Content / Umka**. Restart the editor after updating an installation that previously had no plugin content.
2. Duplicate a standalone example into your project's Content folder before editing its **Source**. For the file-backed example, edit the linked `.um` file instead.
3. Add an **Umka Script** node to a Blueprint event graph. Select the node and assign your asset to **Script Asset** in Details.
4. Connect the execution pins and inputs, use the result outputs, and compile the Blueprint.

`MoveWithVelocity` and `RotateYaw` expose regular Vector and Rotator pins. These built-in types always use native pins; the **Use Custom Struct Pins** option only controls custom script structs.

Each asset exports one function, so no function selection is needed. `real` and `[]real` create Double and Double Array pins; Blueprint can convert Float inputs when needed. Except for `StatefulCounter`, the examples keep state in Blueprint variables: write updated values back before the next call.

## Examples

| Asset / function | Inputs | Results | Blueprint use |
| --- | --- | --- | --- |
| `ApplyDamage` | `Health`, `Damage`, `MaxHealth` | New health | Call from **Event AnyDamage**, pass its Damage value and your health variables, then **Set Health**. Negative damage is ignored; health is clamped to `0..MaxHealth`, with a negative maximum treated as zero. |
| `AdvanceCooldown` | `Remaining`, `DeltaSeconds` | `ReturnValue1`: remaining seconds; `ReturnValue2`: ready | From **Event Tick**, update a cooldown variable with `ReturnValue1`. Use `ReturnValue2` in a Branch to allow an ability, and set the cooldown to its duration when the ability fires. |
| `MoveWithVelocity` | `Position`: Vector; `Velocity`: Vector; `DeltaSeconds` | New Vector position | From **Event Tick**, pass **Get Actor Location**, velocity in cm/s, and Delta Seconds. Apply the result with **Set Actor Location**; enable Sweep if collision is needed. |
| `RotateYaw` | `Rotation`: Rotator; `DegreesPerSecond`, `DeltaSeconds` | New Rotator | From **Event Tick**, pass **Get Actor Rotation** and an angular speed. Apply the result with **Set Actor Rotation** for a spinning pickup or platform. Pitch and Roll are preserved. |
| `TotalWeight` | `Weights`: Double Array | Total weight | When inventory changes, pass an array containing one weight per carried item. Store the result or compare it with a carry limit. Empty arrays return zero; negative weights are ignored. |
| `StatefulCounter` | None | `int32` call count | Call from a repeated event and enable **Preserve Script State** in the node's Details. Each node instance keeps its own `Calls` global; pass `true` to **Reset Script State** when starting a new run. The reset call returns `1`. Without preservation, every call returns `1`. |
| `FileBacked/HealthColor` | `Health`, `MaxHealth` (default `100`) | Linear Color | When health changes, pass the result to a widget's **Set Color and Opacity** or a material color parameter. Zero health is red, full health is green, intermediate values blend linearly. Health is clamped; `MaxHealth <= 0` returns red. |
| `WeightedChoice` | `Weights`: Double Array; `Roll`: Double | Selected `int32` index | Pass item weights and **Random Float in Range** `0..1`, then use the result to index a loot or encounter array. Roll is clamped; nonpositive weights are ignored. Branch on index `>= 0` before indexing because empty arrays or no positive weights return `-1`. |
| `EvaluateQuest` | `Objectives`: Boolean Array | `ReturnValue1`: completed fraction; `ReturnValue2`: all complete | Whenever an objective changes, update its Boolean and call this function. Send the fraction to a progress bar's **Set Percent** and branch on all complete to award the quest. Empty arrays return `0`, `false`. |
| `CalculateAttack` | `Attack`: custom struct with `Damage`, `Armor`: Double; `Critical`: Boolean; `CriticalMultiplier`: Double | Custom struct with `Damage`: Double; `WasCritical`: Boolean | Enable **Use Custom Struct Pins** on the node to expose a struct input and output you can split into fields. Fill the attack fields, call the script, subtract returned Damage from health, and use WasCritical for feedback. Damage and armor clamp to zero; multiplier clamps to at least `1`. Critical multiplication happens before armor subtraction. |
| `PatrolMovement` | `Position`: Vector; `Waypoints`: Vector Array; `WaypointIndex`: `int32`; `Speed`, `DeltaSeconds`: Double | `ReturnValue1`: position; `ReturnValue2`: next index | From **Event Tick**, pass actor location, an ordered waypoint array, your index variable, speed in cm/s, and Delta Seconds. **Set Actor Location** from the first output and store the second as your new index. Movement stops at the current waypoint without overshooting and advances through the array circularly, at most one waypoint per call. Invalid indices start at `0`; an empty array returns the original position and `-1`. Nonpositive speed or Delta Seconds keeps the position unchanged. |

All time-based examples ignore negative Delta Seconds. Movement is a position calculation; collision and movement replication remain the responsibility of the Blueprint or movement component that applies it.

## Quick checks

| Example | Inputs | Expected result |
| --- | --- | --- |
| `ApplyDamage` | Health `100`, Damage `25`, MaxHealth `100` | `75` |
| `AdvanceCooldown` | Remaining `0.25`, DeltaSeconds `0.5` | `0`, `true` |
| `MoveWithVelocity` | Position `(10, 20, 30)`, Velocity `(100, 0, -20)`, DeltaSeconds `0.5` | `(60, 20, 20)` |
| `RotateYaw` | Rotation `(Pitch=10, Yaw=20, Roll=30)`, DegreesPerSecond `90`, DeltaSeconds `0.5` | `(Pitch=10, Yaw=65, Roll=30)` |
| `TotalWeight` | Weights `[2.5, 1.25, -1]` | `3.75` |
| `StatefulCounter` | Three calls with preserved state | `1`, `2`, `3` |
| `HealthColor` | Health `50`, MaxHealth `100` | Linear Color `(R=0.5, G=0.5, B=0, A=1)` |
| `WeightedChoice` | Weights `[1, 3]`, Roll `0.5` | Index `1` |
| `EvaluateQuest` | Objectives `[true, false, true, false]` | `0.5`, `false` |
| `CalculateAttack` | Damage `100`, Armor `20`, Critical `true`, CriticalMultiplier `2` | Damage `180`, WasCritical `true` |
| `PatrolMovement` | Position `(0, 0, 0)`, Waypoints `[(10, 0, 0), (10, 10, 0)]`, WaypointIndex `0`, Speed `100`, DeltaSeconds `1` | Position `(10, 0, 0)`, next index `1` |

[CheckExamples.py](../Scripts/CheckExamples.py) checks that each `.um` file matches its shipped asset's **Source**, that standalone assets have no file binding, and that the linked HealthColor asset resolves its source file. It also runs these cases and boundary checks through UEmka. Counter checks cover repeated calls, preserved sessions, separate callers, reset, and fresh execution. To run the checker with Unreal's built-in Python plugin on Windows, replace the engine and project paths below as needed:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Path\MyProject\MyProject.uproject" -EnablePlugins=PythonScriptPlugin -run=pythonscript -script="C:\Path\MyProject\Plugins\UEmka\Scripts\CheckExamples.py" -unattended -nullrhi -nosound
```

See [Writing scripts](WritingScripts.md), [Unreal types](UnrealTypes.md), and [Reusable modules](Modules.md) to extend these examples.
