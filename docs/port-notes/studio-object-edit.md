# Object edit port (SunriseStudio to Dawn): NOT COMPILED OR TESTED

Status: NOT COMPILED OR TESTED. Nothing here is built, linked or run inside Dawn.
The only checks run were:

- `g++ 13 -std=c++20 -fsyntax-only -Wall -Wextra -Wpedantic` and `clang++ -fsyntax-only`
  on the three `.cpp` files, against Dawn's vendored ImGui headers (`Dawn/vendor/imgui`).
  Those checks read headers only. They produced no objects.
- A throwaway harness in the scratchpad (outside this folder) that exercised the pure rules in
  `object_edit_math.h` and `object_edit_model.cpp`: the degree and quaternion round trip, transform
  and bounds edit building, refusals and validation. It passed. It does not cover the panel or the backend.

Treat the port as unverified until it is added to `Dawn.vcxproj` and built.

## Scope

Source (read only): `SunriseStudio.Authoring/SceneEdits.cs`, `TransformHandles.cs` (reference only),
`SunriseStudio.Projects/ObjectEdits.cs`, `SunriseStudio.Editor/StudioForm.Selection.cs`,
`StudioForm.ObjectNames.cs`, `StudioForm.Collision.cs`, `StudioWorkspace.ObjectEdits.cs`,
`StudioWorkspace.Collision.cs`, `SunriseStudio.Scene/SceneObjectPlacement.cs`,
`TransformChange.cs`, `Collision/CollisionScene.cs`, `Collision/InlineTriggerShape.cs`,
`SunriseStudio.Viewport/AssetViewport.Collision.cs`.

Output root: `studio-object-edit/`. Code mirrors the Dawn tree at `Dawn/src/tools/studio/object_edit/`.
Namespace: `dawn::tools::studio::object_edit`, matching the `dawn::client::ui::player` pattern of
`player_panel.h/.cpp`.

## (a) Files written

All paths are relative to this folder (`studio-object-edit/`).

1. `Dawn/src/tools/studio/object_edit/object_edit_math.h`: header only. `Vec3`, `Quat`, finiteness, normalize,
   inclusive containment, and the degree and quaternion conversions.
2. `Dawn/src/tools/studio/object_edit/object_edit_model.h`: edit records, layout and kind enums, the
   `StoredRow` and `ObjectEditInfo` snapshots, `EditBuild` results, and the rule function declarations.
3. `Dawn/src/tools/studio/object_edit/object_edit_model.cpp`: the rules: validation, merge, summary,
   `describe_stored`, `make_transform_edit`, `make_bounds_edit`.
4. `Dawn/src/tools/studio/object_edit/object_edit_backend.h`: the abstract `ObjectEditBackend` seam and the
   `UnportedStudioObjectEditBackend` stub class.
5. `Dawn/src/tools/studio/object_edit/object_edit_backend.cpp`: the stub. Every call refuses, and each
   refusal begins with `NOT PORTED:` and names the C# code it stands in for.
6. `Dawn/src/tools/studio/object_edit/object_edit_panel.h`: `PanelState`, `select_object`, `select_collision`, `draw`.
7. `Dawn/src/tools/studio/object_edit/object_edit_panel.cpp`: the Dear ImGui inspector.
8. `NOTES.md`: this file. It sits at the output root, outside the mirrored tree.

Not written: no changes to the Dawn tree itself, and no `Dawn.vcxproj` entry (see section c).

## (b) What was ported

Ported and implemented:

- **Selection.** `select_object` and `select_collision` make the selection exclusive, as the C# inspector does.
  Selection is by key. The viewport picking that chooses the key is not ported.
- **Transform (position, rotation, scale).** `make_transform_edit` matches `SceneEdits.CreateTransformEdit`:
  - a trigger row refuses transforms
  - non-finite values, a near-zero rotation and a zero scale are refused
  - a row with no scale field refuses any scale other than 1
  - a value equal to the stored one is left out
  - a rotation is normalized, and an unchanged rotation is not written
  - a scale is written only for a row that has one

  The panel shows rotation in XYZ degrees, using the same conversion as `StudioForm.Selection.cs`
  (`degrees_from_quaternion`, `quaternion_from_degrees`). An untouched rotation field keeps the stored value.
- **Trigger bounds (the editable "collision bounds").** `make_bounds_edit` matches `SceneEdits.CreateBoundsEdit`:
  finite, ordered corners; a non-negative extrusion; every authored hull point, and its top (point plus extrusion),
  must lie inside the new bounds; an unchanged box gives no edit. The panel refuses to apply bounds when the backend
  cannot supply hull points, so an unreadable hull fails closed.
- **Describe mapping.** `describe_stored` gives the same editability as `SceneEdits.Describe`:
  - trigger volumes: bounds only, with the centre as position
  - every other layout: transform, and scale only when the row stores one
- **Common properties.** The object's display name, with the name key `edit:<object key>`. It saves when the field
  loses focus after an edit. A blank name removes it. Group membership is not ported (see c).
- **Saved edits.** The "Edited: ..." summary, and "Revert object". The summary text matches `ObjectEdit.Summary`.
- **Edit record and validation.** `ObjectEdit`, `ObjectEditTarget` (with its key format), `validate_edit`
  (same checks and order as `ObjectEdit.Validate`), and `merge_edits`.
- **Collision (read only).** The collision section shows kind, role, source, triangle count, instance,
  world position and drawn size, from `CollisionPlacementInfo`. Collision placements are not editable here.

## (c) What was stubbed or skipped

Stubbed. Each is an `UnportedStudioObjectEditBackend` method that refuses or returns nothing:

- `describe`: the decoding of rows from the package catalog and native layouts (`SceneEdits.Describe`,
  `ObjectRows.Locate`, `NativeRows`). This is the largest gap.
- `hull_points`: the trigger hull points (`SceneEdits.CreateBoundsEdit` reads them from the row).
- `saved_edit`: the project's saved object edits (`Project.FindObjectEdit`).
- `apply_edit`, `revert_edit`: the object edit writes, the undo frame, and the project manifest
  (`StudioWorkspace.ApplyObjectEdits`, `RevertObjectEdit`, `MutateProject`).
- `object_name`, `set_object_name`: project name storage (`StudioProject.ObjectNames`).
- `collision_placement`: the collision loader (`CollisionSceneLoader`) and Havok packfile reading.
- `can_edit`: always false, so every edit control is disabled.

Because the stub reports no editable object, the inspector shows the "NOT PORTED" message and
no fields. This is the expected state until a real backend is written.

Skipped:

- **Viewport.** The 3D viewport, rendering, the gizmo (`AssetViewport.Gizmo.cs`, `TransformHandles.cs`), and
  viewport picking and selection (`SceneSelection.cs`, `AssetViewport.CollisionSelection.cs`). The `TransformChange`
  pivot mathematics that the gizmo uses is not ported, because nothing in the inspector path needs it.
- **Collision part edits.** `EditCollisionParts`, `TransformGroup`, the part pivot and the part summary.
- **Groups.** Group create, rename, membership and collision part groups (`StudioWorkspace.ObjectGroups.cs`).
- **Opening and navigation.** "Open asset", reference lists, related objects, mission slots, name evidence,
  and the Shape and Compound links in the collision section.
- **Property facts and effect controls.** The read-only property tables (`StudioUi.Facts`, `WorldInspection`), effect
  controls, live pose observation, and isolation and frame buttons.
- **Undo, transactions and background work.** These are the backend's job. The port does not implement them.
- **Dawn build registration.** The files are not added to `Dawn.vcxproj`, and no include path is set.

## Choices and deviations to review

- **"Collision bounds" is interpreted as trigger volume bounds.** That is the only bounds edit in the C# code.
  Collision placements are read only.
- **No exceptions.** The C# code throws `InvalidDataException` and `NotSupportedException`. The port returns the same
  messages in `EditBuild::error` or as strings instead of throwing. I did not check whether Dawn builds with
  exceptions enabled, so confirm that the choice fits.
- **Dawn has no shared math types.** `Vec3` and `Quat` are defined in this module. Dawn's
  `policy.h` `Vector3` lives in another namespace and has no quaternion. The types are not shared.
- **Panel state is passed in.** `draw(backend, state)` takes its state as an argument, where `player_panel` keeps
  `static` locals. The state must be owned by the caller and kept alive across frames.
- **Fields reload after a successful write or revert.** The C# inspector keeps its fields until the target changes.
  The port reloads the row after a write, so the fields show what was saved.
- **Name is passed untrimmed.** The C# code compares the trimmed draft but saves the raw text. The port does the same.
- **Per-frame calls.** `saved_edit`, and `object_name` when the key changes, run each frame. A real backend may want to cache them.
- **Object key.** Keys use the `SSSSSSSS/Layout/offset-hex` format of `ObjectEditTarget.Key`. This is a string key,
  not a reference to a scene object.
- **Hash field.** `ObjectEditTarget.OriginalSha256` is carried but not checked. The C# code checks it against the
  project manifest, which is not ported.
- **Decorator rows.** The C# code gives decorator payload rows a scale field. The port has no separate decorator path.
  A backend must set `StoredRow::hasScale = true` for decorator rows, or their scale cannot be edited. This is a contract, not a check.
- **Stored row contract.** `describe` must fill `StoredRow` in the same units the C# inspector uses (local for
  placements, world for trigger volumes, payload space for decorators). `describe_stored` does not convert them.

## Next steps

1. Write a real `ObjectEditBackend` that decodes rows and applies edits, or decide not to port editing.
2. Add the files to `Dawn.vcxproj`, build them, and check the `-Wall` results.
3. Add a unit test for the rules in `object_edit_model.cpp`. The scratch harness covers only part of them.
