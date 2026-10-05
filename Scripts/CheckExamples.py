from pathlib import Path

import unreal


CHECKS = {
    "ApplyDamage": """
        return ApplyDamage(100, 25, 100) == 75 &&
               ApplyDamage(10, 25, 100) == 0 &&
               ApplyDamage(100, -25, 100) == 100 &&
               ApplyDamage(150, 0, 100) == 100 &&
               ApplyDamage(10, 0, -1) == 0
    """,
    "AdvanceCooldown": """
        remaining, ready := AdvanceCooldown(0.25, 0.5)
        running, waiting := AdvanceCooldown(1, 0.25)
        unchanged, ignored := AdvanceCooldown(1, -1)
        expired, available := AdvanceCooldown(-1, 0)
        return remaining == 0 && ready && running == 0.75 && !waiting &&
               unchanged == 1 && !ignored && expired == 0 && available
    """,
    "MoveWithVelocity": """
        position := ue::Vector{10, 20, 30}
        velocity := ue::Vector{100, 0, -20}
        moved := MoveWithVelocity(position, velocity, 0.5)
        unchanged := MoveWithVelocity(position, velocity, -1)
        return moved.X == 60 && moved.Y == 20 && moved.Z == 20 &&
               unchanged.X == 10 && unchanged.Y == 20 && unchanged.Z == 30
    """,
    "RotateYaw": """
        rotation := ue::Rotator{10, 20, 30}
        rotated := RotateYaw(rotation, 90, 0.5)
        unchanged := RotateYaw(rotation, 90, -1)
        return rotated.Pitch == 10 && rotated.Yaw == 65 && rotated.Roll == 30 &&
               unchanged.Pitch == 10 && unchanged.Yaw == 20 && unchanged.Roll == 30
    """,
    "TotalWeight": """
        return TotalWeight([]real{2.5, 1.25, -1}) == 3.75 &&
               TotalWeight([]real{}) == 0
    """,
    "StatefulCounter": """
        first := StatefulCounter()
        second := StatefulCounter()
        third := StatefulCounter()
        return first == 1 && second == 2 && third == 3
    """,
    "HealthColor": """
        empty := HealthColor(0, 100)
        full := HealthColor(100, 100)
        half := HealthColor(50, 100)
        below := HealthColor(-10, 100)
        above := HealthColor(150, 100)
        invalid := HealthColor(50, 0)
        negative := HealthColor(50, -100)
        return empty.R == 1 && empty.G == 0 && empty.B == 0 && empty.A == 1 &&
               full.R == 0 && full.G == 1 && full.B == 0 && full.A == 1 &&
               half.R == 0.5 && half.G == 0.5 &&
               below.R == 1 && below.G == 0 && above.R == 0 && above.G == 1 &&
               invalid.R == 1 && invalid.G == 0 && negative.R == 1 && negative.G == 0
    """,
    "WeightedChoice": """
        weights := []real{1, 3}
        filtered := []real{-2, 0, 1, 3}
        return WeightedChoice(weights, 0) == 0 && WeightedChoice(weights, 0.249) == 0 &&
               WeightedChoice(weights, 0.25) == 1 && WeightedChoice(weights, 1) == 1 &&
               WeightedChoice(weights, -1) == 0 && WeightedChoice(weights, 2) == 1 &&
               WeightedChoice(filtered, 0) == 2 && WeightedChoice(filtered, 1) == 3 &&
               WeightedChoice([]real{}, 0.5) == -1 && WeightedChoice([]real{0, -1}, 0.5) == -1
    """,
    "EvaluateQuest": """
        partial, partialDone := EvaluateQuest([]bool{true, false, true, false})
        full, fullDone := EvaluateQuest([]bool{true, true})
        none, noneDone := EvaluateQuest([]bool{false, false})
        empty, emptyDone := EvaluateQuest([]bool{})
        return partial == 0.5 && !partialDone && full == 1 && fullDone &&
               none == 0 && !noneDone && empty == 0 && !emptyDone
    """,
    "CalculateAttack": """
        normal := CalculateAttack(AttackInput{100, 20, false, 2})
        critical := CalculateAttack(AttackInput{100, 20, true, 2})
        negativeDamage := CalculateAttack(AttackInput{-10, 0, false, 2})
        negativeArmor := CalculateAttack(AttackInput{100, -20, false, 2})
        clampedMultiplier := CalculateAttack(AttackInput{100, 20, true, 0.5})
        blocked := CalculateAttack(AttackInput{10, 20, false, 2})
        return normal.Damage == 80 && !normal.WasCritical &&
               critical.Damage == 180 && critical.WasCritical &&
               negativeDamage.Damage == 0 && negativeArmor.Damage == 100 &&
               clampedMultiplier.Damage == 80 && clampedMultiplier.WasCritical && blocked.Damage == 0
    """,
    "PatrolMovement": """
        position := ue::Vector{0, 0, 0}
        waypoints := []ue::Vector{{10, 0, 0}, {10, 10, 0}}
        moved, movedIndex := PatrolMovement(position, waypoints, 0, 4, 0.5)
        reached, nextIndex := PatrolMovement(position, waypoints, 0, 100, 1)
        wrapped, firstIndex := PatrolMovement(waypoints[0], waypoints, 1, 100, 1)
        clamped, clampedIndex := PatrolMovement(position, waypoints, -1, 0, 1)
        high, highIndex := PatrolMovement(position, waypoints, 20, 0, 1)
        stopped, stoppedIndex := PatrolMovement(position, waypoints, 1, -1, 1)
        unchanged, unchangedIndex := PatrolMovement(position, waypoints, 1, 1, -1)
        empty, emptyIndex := PatrolMovement(position, []ue::Vector{}, 0, 1, 1)
        single, singleIndex := PatrolMovement(position, []ue::Vector{{10, 0, 0}}, 0, 100, 1)
        return moved.X == 2 && moved.Y == 0 && movedIndex == 0 &&
               reached.X == 10 && reached.Y == 0 && nextIndex == 1 &&
               wrapped.X == 10 && wrapped.Y == 10 && firstIndex == 0 &&
               clamped.X == 0 && clampedIndex == 0 && high.X == 0 && highIndex == 0 &&
               stopped.X == 0 && stoppedIndex == 1 && unchanged.X == 0 && unchangedIndex == 1 &&
               empty.X == 0 && emptyIndex == -1 && single.X == 10 && singleIndex == 0
    """,
}


library = unreal.get_default_object(unreal.UEmkaFunctionLibrary)
for name, check in CHECKS.items():
    folder = "FileBacked/" if name == "HealthColor" else ""
    path = f"/UEmka/Umka/{folder}{name}.{name}"
    asset = unreal.load_asset(path)
    assert isinstance(asset, unreal.UEmkaScriptAsset), f"Missing script asset: {path}"
    import_data = asset.get_editor_property("asset_import_data")
    if name != "HealthColor":
        assert import_data is not None and not import_data.extract_filenames(), f"{name}: shipped example must be standalone"
    source = asset.get_editor_property("source")
    source_file = Path(__file__).resolve().parents[1] / "Examples" / f"{name}.um"
    assert source_file.read_text(encoding="utf-8") == source, f"{name}: .um source differs from asset Source"
    source += "\nfn verify*(): bool {\n" + check + "\n}\n"
    # Internal Blueprint helpers have no Python glue; call them through reflection.
    output = library.call_method(
        "RunUmkaInline", (None, source, "verify", [], unreal.UEmkaValueType.BOOL, False, False)
    )
    assert output is not None, f"{name}: compilation or execution failed; see LogUEmka"
    result, error = output
    assert not error, f"{name}: {error}"
    assert library.call_method("GetInt32Result", (result,)) == 1, name
    unreal.log(f"UEmka example passed: {name}")

bound = unreal.load_asset("/UEmka/Umka/FileBacked/HealthColor.HealthColor")
assert isinstance(bound, unreal.UEmkaScriptAsset), "Missing file-backed HealthColor example"
linked_files = bound.get_editor_property("asset_import_data").extract_filenames()
linked_source = Path(__file__).resolve().parents[1] / "Examples" / "HealthColor.um"
assert len(linked_files) == 1 and Path(linked_files[0]).resolve() == linked_source.resolve(), "HealthColor: linked source path does not resolve in this plugin installation"
assert bound.get_editor_property("source") == linked_source.read_text(encoding="utf-8"), "HealthColor: file-backed Source differs from .um"
unreal.log("UEmka file-backed example path and source passed")

counter = unreal.load_asset("/UEmka/Umka/StatefulCounter.StatefulCounter")
other_caller = bound
session_id = unreal.GuidLibrary.new_guid()


def run_counter(caller, options):
    output = library.call_method(
        "RunUmkaAssetConfigured",
        (caller, counter, counter.get_editor_property("source"), "StatefulCounter", [], unreal.UEmkaValueType.INT32, False, False, session_id, options),
    )
    assert output is not None, "StatefulCounter: configured execution failed; see LogUEmka"
    result, error = output
    assert not error, f"StatefulCounter: {error}"
    return library.call_method("GetInt32Result", (result,))


options = unreal.UEmkaExecutionOptions()
options.set_editor_property("use_session", True)
options.set_editor_property("reset_session", True)
assert run_counter(counter, options) == 1, "StatefulCounter: reset must start at one"
options.set_editor_property("reset_session", False)
assert run_counter(counter, options) == 2, "StatefulCounter: preserved session must retain globals"
options.set_editor_property("reset_session", True)
assert run_counter(other_caller, options) == 1, "StatefulCounter: other caller must start its own session"
options.set_editor_property("reset_session", False)
assert run_counter(counter, options) == 3, "StatefulCounter: other caller must not reset the first session"
options.set_editor_property("reset_session", True)
assert run_counter(counter, options) == 1, "StatefulCounter: explicit reset must restart the counter"
fresh_options = unreal.UEmkaExecutionOptions()
assert run_counter(counter, fresh_options) == 1 and run_counter(counter, fresh_options) == 1, "StatefulCounter: default execution must use fresh state"
unreal.log("UEmka counter session checks passed")

unreal.log(f"UEmka starter examples: {len(CHECKS)} passed")
