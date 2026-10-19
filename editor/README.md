# Shape model editor

Build inside Steam Runtime using `./env.sh`, followed by `./build.sh` from the
workspace root. Launch `./build-debug/editor` from the same container. The editor
starts with a box; **New model** clears the document after confirmation.

The separate [mapedit level editor](../mapedit/README.md) builds alongside this
tool and places terrain, spawn/entry, and exit locations.

## Code structure

| Component | Responsibility |
| --- | --- |
| [EditorApplication](src/Editor.cpp) | Initializes services, builds each frame, and executes UI actions after drawing. |
| [EditorSession](src/EditorSession.cpp) | Owns the CPU document, selection, status, and arena-backed import/export operations. |
| [ShapeSession](src/ShapeSession.cpp) | Shares document, selection, status, and shape commands between model and map editors. |
| [EditorUI](src/EditorUI.cpp) | Draws the sidebar and confirmation dialogs; owns file-picker and export dialog state. |
| [EditorViewport](src/EditorViewport.cpp) | Owns the orbit camera and transform tools; handles navigation, shortcuts, picking, and gizmos. |
| [ScenePreview](src/ScenePreview.cpp) | Owns shader, mesh, and material handles; uploads, caches, submits, and releases GPU resources. |
| [ShapeDocument](src/ShapeDocument.cpp) / [ShapeImport](src/ShapeImport.cpp) | Store bounded objects and implement geometry operations and import validation without a UI or renderer dependency. |
| [ImportFileDialog](src/ImportFileDialog.cpp) | Publishes asynchronous SDL picker results to a process-lifetime mailbox. |

`EditorApplication::onPreRender` starts the GUI frame, draws the UI, executes its
returned `EditorAction`, then submits the preview. This keeps file operations and
whole-document replacement outside widget construction. Sidebar sections and
viewport interactions each have separate, named functions.

Import has three explicit steps: `EditorSession::prepareImport` creates a candidate
document in the inactive geometry bank; `ScenePreview::rebuild` uploads a complete
replacement resource set; `EditorSession::commitImport` publishes the candidate.
Either preparation or upload can fail without replacing the live scene.
`EditorSession::Storage` groups all CPU memory budgets in one caller-owned block;
the application supplies static storage to keep those document spans alive.

## Controls

- Add boxes, spheres, closed cylinders, and planes. Each starts at one metre
  across; solid shapes start resting on the ground plane.
- Select from the shape list or click the mesh in the viewport. Edit its name,
  position, Euler rotation in degrees, nonzero scale, and colour in the sidebar.
- Use the ImGuizmo handles to move, rotate, or scale. Toggle local/world axes and
  snapping in the sidebar. Scaling always uses local axes to retain a valid TRS
  transform. W/E/R switch tools while the pointer is over the viewport.
- Right drag orbits, middle drag pans, and the wheel zooms. F focuses the selected
  shape. Ctrl+D duplicates it with a small offset; Delete removes it. The sidebar
  also has Duplicate, Delete, and Focus buttons.
- Enter an export path and choose **Export GLB**. Relative paths start in the
  installed `content` directory, so the default creates `content/model/shape-model.glb`.
  The destination directory must already exist. Existing files require explicit
  replacement confirmation. Failed writes preserve the previous file.
- Choose **Import...** in the menu bar to open SDL3's native file picker, filtered
  for GLB/glTF models. After choosing a file, confirm **Import** to add its meshes
  to the scene, or enable **Replace current model** to replace the document.
  **Choose file...** changes the selection. The picker starts in `content` and
  remembers the last selected file. Cancelling the picker leaves the model intact.
  Imports commit only after parsing, copying, and GPU uploads succeed; failures
  preserve the existing document. Successful imports frame the scene automatically.

Imported models can be selected, renamed, transformed, recoloured, duplicated,
deleted, combined with basic shapes, and exported again. Material factors,
double-sided flags, alpha modes, and vertex colours survive import/export.
The inspector exposes double-sided and alpha settings as well as RGBA colour.
Signed scales preserve mirrored meshes; zero scales are prevented during editing.

Import currently accepts untextured static triangle meshes under the engine's
glTF profile. Each mesh primitive instance becomes an object; parent transforms
are folded into its world transform, so imported hierarchy/grouping is flattened.
Sheared or zero-scale world transforms are rejected. Names use the editor's
47-byte name limit. Textured, animated, and skinned models are rejected explicitly.
The document supports 64 objects and at most 512 source nodes per imported scene.

Exports are standard static GLB 2.0 files compatible with `asset::ImportModel`.
They retain named shape nodes, transforms, UVs, normals, tangents, and material
colours. Geometry buffers are shared when shapes reuse the same source mesh.
Planes use double-sided materials. Preview lighting is only a viewport aid.
This is a tool for assembling shapes; it does not perform mesh boolean operations,
vertex editing or texture authoring.

`ShapeDocument` keeps 64 stable object slots. The editor uses a 64 KiB arena for
the four shared CPU meshes and 512 KiB of scratch for export, with four shared
GPU meshes and cached materials. Imports use two alternating 8 MiB geometry banks,
an 8 MiB parser output arena, and 16 MiB of parser scratch. Each import compacts
retained imported geometry into the other bank, reclaiming deleted geometry and
deduplicating identical meshes. CPU and GPU instances share geometry.
The document, geometry generator, importer, and exporter
use no exceptions, lambdas, `std::function`, or growing containers. ImGui,
ImGuizmo, libc file streams, and graphics drivers manage their own storage.

`CreateShape` and `ExportModelGlb` live in `idkengine_assets`, so the future asset
conversion tool can use them without SDL or OpenGL. `ShapeDocument::snapshot`
creates arena-owned descriptors which borrow names and geometry from the live
document; consume them before changing or releasing the document.

Enable `IDK_BUILD_EDITOR_TESTS=ON` to build `idk_shape_tests`. It checks triangle
winding, normal/tangent frames, shape selection, capacity limits, arena rollback,
GLB roundtrips, hierarchy/instancing, and file preservation on failed exports.
`idk_editor_import_tests` also checks GLB/glTF import and re-export, compaction,
parser arena lifetime, reflected transforms, gimbal lock, imported picking,
capacity, unsupported inputs, and rollback after partial import failures.
`idk_import_dialog_tests` checks asynchronous selection, cancellation, errors,
path limits, and callback storage lifetime. Native dialog results are copied into
fixed storage and consumed on the editor thread; SDL callbacks never access
ImGui, the renderer, or the editor application's lifetime.
`idk_editor_preview_tests` checks session commands and scene pixels, failed mesh
uploads, material-pool exhaustion, and GPU cleanup/reuse. It requires an OpenGL
4.6 context, like the renderer smoke test. Run with `SDL_VIDEODRIVER=offscreen`
when a visible window is unnecessary.

Formatting rules are scoped to this directory in `.clang-format`.
