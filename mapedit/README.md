# Basic level editor

`mapedit` is built and installed alongside `editor` by the normal workspace
`./build.sh` command. Launch `./build-debug/mapedit` in the same environment used
for `editor` (Steam Runtime via `./env.sh` when needed).

The editor starts with an empty level. Add **Box**, **Sphere**, **Cylinder**, or
**Plane** terrain/environment objects. Use **Spawn / entry (green)** and
**Exit (orange)** to add the two level locations. Each location is unique;
pressing its button again selects the existing marker. The coloured marker
meshes are editing aids, and their centres are the saved location coordinates.
Markers can be moved, rotated, scaled, renamed, deleted, and placed again.

Select an object in the list or by clicking its mesh. Edit position, rotation,
scale, name, and terrain colour in the inspector, or use the transform gizmo.
The ground grid uses metre units. **Snap** exposes move, angle, and scale steps;
**Local axes** chooses the gizmo coordinate system. **Frame level** fits the
whole level in view.

| Input | Action |
| --- | --- |
| Left click | Select a mesh |
| Right drag | Orbit the camera |
| Middle drag | Pan |
| Wheel | Zoom |
| W / E / R | Move / rotate / scale gizmo |
| F | Focus selection |
| Ctrl+D | Duplicate selected terrain |
| Delete | Remove selection |

Keyboard viewport controls apply while the pointer is over the viewport and
text input is inactive. Entry and exit cannot be duplicated. The level supports
64 objects total, including markers.

Enter a path in **Map file** and choose **Save** or **Open**. The default is
`level.idkmap`. Relative paths use the installed `content` directory, matching
the engine's working directory. Parent directories must already exist. Saving
over a file requires confirmation; writes use a temporary file in the same
directory and atomically publish it. Failed saves preserve the existing file.
Failed opens preserve the current level, and successful opens frame the scene.
**New level** and opening over an existing scene require discard confirmation.
Save before closing the application to keep your work.

Maps can be saved while incomplete, including empty maps or maps missing one or
both locations. The sidebar shows which locations are set. The versioned text
format stores each object's role, primitive type, name, position, XYZ Euler
rotation in degrees, signed nonzero scale, and RGBA colour. Marker identity is
independent of its name. Files do not contain generated mesh buffers.

The `IDKMAP 1` format begins with `objects N`, followed by N records:

```text
object 0 0
name Floor
position 0 -0.25 0
rotation 0 0 0
scale 12 0.5 12
colour 0.65 0.72 0.9 1
```

Roles are terrain=0, entry=1, exit=2. Primitive types are box=0, sphere=1,
cylinder=2, plane=3. Entry uses a cylinder and exit uses a box. Names occupy a
single line of at most 47 bytes; numbers must be finite. Files contain LF or
CRLF line endings and end after the final record's newline. Unknown versions,
duplicate markers, invalid records, and more than 64 objects are rejected.
This initial tool edits primitive level layouts; game-side loading, collision,
model placement, and playtesting are future work.

`MapSession` owns the map roles and file operations. Geometry, rendering,
selection, camera controls, and gizmos are shared with the model editor through
`ShapeDocument`, `ShapeSession`, `ScenePreview`, and `EditorViewport`. The map
uses a fixed 64 KiB arena for primitive geometry. CPU commands and file parsing
use bounded storage and no exceptions.

Enable `IDK_BUILD_EDITOR_TESTS=ON` to build `idk_mapedit_tests`. Run it directly
or with `ctest --test-dir build-debug/cmake/idk_engine/mapedit --output-on-failure`.
It checks shape and marker commands, slot reuse, picking, full/empty map
roundtrips, reflected transforms, overwrite protection, failed saves, malformed
inputs, duplicate marker rejection, and transactional load rollback.
