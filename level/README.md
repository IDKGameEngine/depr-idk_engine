# Level build and playtest pipeline

Build and install inside `steamrt4` with the usual workspace `./build.sh`.
The build produces `mapc`, `mapedit`, and the game executable, and compiles an
example into `content/level/playground.idklvl`. No additional dependencies are
needed; the playtest controller uses the project's existing Jolt dependency.

## In the editor

Open `level/playground.idkmap` in the installed `mapedit`, or create a map with
terrain, one spawn/entry marker, and one exit marker. Choose **Build & Play**.
It builds the current document, including unsaved edits, and launches the game
installed beside `mapedit`. The source map is unchanged. Only one playtest runs
at a time. **Stop playtest**, game exit, and editor shutdown release the child
process and remove its temporary level snapshot.

**Build level** writes the runtime output path shown above the button. Relative
paths use the editor's content directory. This action replaces the `.idklvl`
artifact; choose a separate path from the source map. A failed build or write
preserves the previous artifact. Saving an incomplete source map is still
allowed, but building requires entry, exit, and at least one terrain object.

## Command line

From the workspace root inside `steamrt4`:

```bash
./build-debug/mapc build-debug/content/level/playground.idkmap /tmp/playground.idklvl
./build-debug/game --level /tmp/playground.idklvl
```

`mapc source.idkmap destination.idklvl [--force]` returns 0 on success, 1 on a
build/I/O error, and 2 for invalid arguments. Use `--force` to replace an
existing output. The compiler refuses to overwrite the source, including through
a hard link or symlink. Paths are relative to the calling directory; the game
resolves its level argument before the engine switches to the content directory.

Running the game without `--level` retains the model demo. With a custom
`IDK_APP_NAME`, use that executable's name; `mapedit` picks it up automatically
at build time. `--help` prints usage. `--frames N`, together with `--level`,
runs a finite number of rendered frames for automated startup checks.

## Controls and locations

| Input | Action |
| --- | --- |
| Left click in the game | Capture mouse for looking |
| Mouse | Look |
| W/A/S/D | Walk |
| Left Shift | Run |
| Space | Jump while grounded |
| Tab | Release/capture mouse |
| R | Restart at entry |
| Escape | Close playtest and return to the editor |

The entry marker's saved position becomes the player's **feet**. The capsule
is 1.8 metres tall with radius 0.3 metres; the camera is 1.6 metres above the
feet. Place the entry above a walkable surface with room for the capsule.
Entry yaw and pitch control the initial view; the player remains upright.
Entry marker scale is an editing aid and does not change player dimensions.

All terrain shapes have static triangle collision, including transformed,
nonuniformly scaled, and mirrored shapes. Planes collide from both sides. The
controller supports gravity, ground contact, slopes, stepping, and jumping.
Falling 50 metres below the lowest terrain vertex restarts the player.

An orange translucent box shows the exit volume, using the exit marker's
position, rotation, and scale. Overlapping it with the player's capsule reports
**Exit reached** in the game title and console. R restarts the run. Entry and
exit marker geometry is excluded from terrain and collision.

## Data and validation

The compiler reuses `MapSession` to parse `.idkmap` files. It bakes world-space
positions, normals, tangents, UVs, colours, and mirrored winding into runtime
meshes. The same triangle data is used to create Jolt's static collision mesh
at load time. Runtime loading has no dependency on editor code or the source
map. Builds reject missing locations/terrain, invalid transforms, non-finite
data, out-of-range vertices, degenerate triangles, and capacity overruns.
Terrain scale magnitudes must be at least 0.001; baked positions are bounded
to 100,000 metres per coordinate. Spawn clearance and route reachability are
tested by playtesting rather than inferred by the compiler.

`.idklvl` is versioned, little-endian data with explicit IEEE float32 fields:

- Eight-byte `IDKLVL\r\n` signature, uint32 version (1), uint32 terrain count.
- Entry and exit: position XYZ, rotation XYZ in degrees, signed scale XYZ.
- Per terrain: 48-byte zero-terminated name, RGBA floats, uint32 double-sided
  flag, uint32 vertex count, uint32 index count.
- Per vertex: position XYZ, normal XYZ, UV, tangent XYZW, colour RGBA floats.
- Per triangle: three uint32 indices local to that terrain's vertices.

There are at most 62 terrain objects, 32,768 vertices, and 196,608 indices.
Both build and load use caller-owned arenas; the applications supply 4 MiB.
Failed loads rewind the arena and retain the caller's previous level. File
writes use a temporary file in the output directory and publish atomically.
Jolt owns its collision allocations and uses an 8 MiB update scratch allocator.
The current player implementation owns one Jolt world per process.

This initial pipeline supports primitive terrain and a first-person playtest.
Dynamic props, game scripting, imported asset placement, navigation baking,
and level-to-level transitions are not included.

## Verification

Enable `IDK_BUILD_EDITOR_TESTS=ON`, build `idk_level_tests`, and run:

```bash
ctest --test-dir build-debug/cmake/idk_engine/level --output-on-failure
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build-debug/game --level build-debug/content/level/playground.idklvl --frames 10
```

The CPU tests cover compilation, mirrored geometry, artifact roundtrips,
corrupt/truncated files, arena rollback, source/output preservation, capsule
grounding, wall collision, jumping, restart, plane collision, and exit detection.
The offscreen command also checks real graphics initialization and level upload.
