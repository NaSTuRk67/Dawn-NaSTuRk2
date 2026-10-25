# Blender interchange port (Dawn Studio)

NOT COMPILED OR TESTED. These files were written by reading and writing only. No build, no run, and no unit test has been done. Do not treat any behaviour below as verified.

Source (C#, read only): `sunrisestudio/src/SunriseStudio.Interchange/` (`AssetBundleModels.cs`, `WorldSceneExport.Geometry.cs`, `AssetBundle.Export.cs`, `AssetBundle.Import.cs`).
Target: `Dawn/src/tools/studio/blender/` in this output folder.

## (a) Files written

All paths are relative to this output folder.

- `Dawn/src/tools/studio/blender/blender_interchange.h`: format types (`Vertex`, `Matrix4`, `MeshObject`, `Document`, `Status`, `Result`) and the public functions `validate`, `serialize`, `parse`, `write_file`, `read_file`.
- `Dawn/src/tools/studio/blender/blender_interchange.cpp`: the token-based text reader and writer, plus validation. Standard library only.
- `Dawn/src/tools/studio/blender/blender_scene_bridge.h`: declares `collect_scene_objects` and `apply_scene_objects`. Both are stubs.
- `Dawn/src/tools/studio/blender/blender_scene_bridge.cpp`: stub bodies. Both return `Status::Unsupported`.
- `Dawn/src/tools/studio/blender/blender_panel.h`: declares `panel::draw()`.
- `Dawn/src/tools/studio/blender/blender_panel.cpp`: ImGui panel with a path field, an Export button and an Import button. It follows the style of `Dawn/src/client/ui/player/player_panel.cpp`.
- `NOTES.md`: this file.

## (b) What was ported

The port covers only the part of the C# interchange that carries mesh vertices, indices and object transforms.

| C# source | Port |
| --- | --- |
| `BundleGeometry.Positions`, `Normals`, `TexCoords` (per-vertex float arrays) | `Vertex::position`, `normal`, `uv` (first UV set only) |
| `BundleGeometry.Indices` (`uint[]`) | `MeshObject::indices`, a triangle list |
| `BundleMesh.Transform` (`float[]`, carried per mesh) | `MeshObject::matrix`, 16 floats, row-major. Translation sits in elements 12, 13 and 14, as in `System.Numerics.Matrix4x4`. |
| `BundleMesh.Id` | `MeshObject::name` |
| Dedupe of geometry by hash (`WorldSceneExport.Geometry.cs`) | Not ported. Each object is written in full. |

The C# `BundleMesh.Transform` length was not verified. The 16-float row-major layout is this port's choice, not a copy of the C# layout.

Validation mirrors the C# bounds in spirit and adds checks the C# side does not need in this form: object count, per-object vertex and index limits, finite floats, index range, and a triangle count that is a multiple of three.

## The `.dsi` file format (this port's own design)

UTF-8 text, LF line endings. Parsing is whitespace-token based, so line breaks inside a list are allowed but not significant. Floats are written with 9 significant digits, which round-trips a 32-bit float. Numbers use the classic C locale.

```
DAWN-STUDIO-INTERCHANGE 1
objects <K>
object n:<escaped name>
matrix <m11 m12 ... m44>                    (16 floats, row-major)
vertices <N>
<px py pz  nx ny nz  u v>                   (N lines, 8 floats each)
indices <M>
<i0 i1 i2 ...>                              (M unsigned ints, M a multiple of 3)
... repeat "object" blocks K times ...
end
```

- Names are written as `n:` followed by the name, with every byte outside `A-Z a-z 0-9 _ - .` written as `%XX`. An empty name is `n:`.
- Anything after `end` is an error. A missing `end` marks the file as truncated.
- Limits: 4096 objects. 4 M (`1 << 22`) vertices and 16 M (`1 << 24`) indices per object. Names up to 256 bytes. Files up to 1 GiB.
- Version: 1. Any other version returns `Status::Unsupported`.

## (c) What was stubbed or skipped

Stubbed (named stubs, clearly marked in code):

- `bridge::collect_scene_objects` (`blender_scene_bridge.cpp`). Dawn has no scene-object accessor for this feature. It returns `Unsupported`, so the Export button does not write a file and shows the message instead.
- `bridge::apply_scene_objects` (`blender_scene_bridge.cpp`). Dawn has no scene sink. The Import button still parses and validates the file, then shows that nothing was applied.

Skipped, not ported:

- Textures, materials, shader programs and DDS data (`BundleTexture`, `BundleMaterial*`, `BundleShader*`).
- Skinning (`Joints`, `Weights`, `Skinned`, `SkinSha256`) and skeletons or rigs.
- Tangents and normals beyond the plain normal stream (`NativeMeshBasis`, tangent frames).
- Vertex colours, the second UV set (`TexCoords1`, detail UVs), and the Z/W lanes of the UV stream.
- Vertex motion and vertex programs (`BundleVertexMotion`, `NativeVertexMotion`).
- Animations, pose and control data (`BundleAnimation`).
- Particles, effects, lights, sky and water (`WorldSceneExport.Effects`, `.Lights`, `.Sky`, `.Water`).
- The C# manifest: SHA-256 hashes, `AssetBundleManifest`, the import and export mode split, and collision and detail UV sections.
- LOD selection, part visibility and render stages.

The Blender side is not part of this port. The task said not to reimplement Blender's add-on. The `.dsi` file is meant to be read by a separate Blender add-on, which is not included here.

## Other gaps and things to check

- Build wiring is not done. `Dawn.vcxproj` (or the project file the tree uses) must list the three `.cpp` files and the include path must reach `imgui.h`. Nothing in the source tree was changed.
- Nothing calls `panel::draw()`. The caller must invoke it inside an active Core UI frame, the same way `client/ui/player` is drawn.
- Dawn's `core::path` helpers were not used. They are Windows wide-path based and tied to the artifact directory. The panel takes a narrow `char` path.
- Writes are not atomic. `write_file` truncates and writes the target directly. A crash mid-write leaves a damaged file, which `read_file` then rejects as truncated. Dawn's `temporary_sibling` helper only removes stale files and was not used.
- Import and export read or write the path typed into the panel. There is no file dialog.
- `std::ifstream` and `std::ofstream` are used. No Windows-specific calls are used.
- Error paths use `std::string` messages, which may allocate. The public functions catch exceptions, but `bad_alloc` inside an error message is not handled separately.
- Dawn's warnings-as-errors policy has not been checked against this code. No `-Wall` or `/W4` review was done beyond reading.
