# Level build and playtest pipeline

Build and install inside `steamrt4` with the usual workspace `./build.sh`.
The build produces `mapc`, `mapedit`, and the game executable, and compiles
examples into `content/level/playground.idklvl` and `content/level/finish.idklvl`.
No additional dependencies are needed; the playtest controller uses the project's
existing Jolt dependency.

## Parkour series

The normal build also compiles and installs five editable parkour courses. Their
exits link in the order below, then lead to `level/finish.idklvl`, which completes
the run. All destination levels are installed together, so **Build & Play** can
start from any course.

| Map in `content/level` | Course | Jumps |
| --- | --- | --- |
| `parkour_01_first_steps.idkmap` | Broad stepping stones, low rises, and round landings | 8 |
| `parkour_02_zigzag.idkmap` | Alternating diagonal jumps and smaller landing pads | 9 |
| `parkour_03_spiral.idkmap` | Sixteen ascending jumps around a square spiral | 16 |
| `parkour_04_balance.idkmap` | Narrow beams, lateral offsets, and right-angle turns | 9 |
| `parkour_05_finale.idkmap` | Rising pads, a sprint gap, and a final balance section | 10 |

Launch `./build-debug/mapedit`, enter `level/parkour_01_first_steps.idkmap` in
**Map file**, and choose **Open**, then **Build & Play**. The same workflow works
for each map. To publish edits for later transitions, set the build output to
the matching `level/parkour_01_first_steps.idklvl` path and choose **Build level**;
**Build & Play** uses a temporary snapshot of the current map.

Platforms are numbered in route order in the object list. Yellow stripes mark
landing centres, green marks the spawn, and orange arches mark the exits. Face
the next platform, walk to the edge, then jump; use Shift to run across the long
gap after the finale's rest deck. R restarts the current course after a miss.
The wide rest decks are safe places to stop; they do not change the spawn.

To play the full chain directly from the workspace root:

```bash
./build-debug/game --level build-debug/content/level/parkour_01_first_steps.idklvl
```

Editable originals live in `idk_engine/level/examples/parkour_*.idkmap`. The build
replaces installed copies from these originals, so save lasting edits back to
the source map before rebuilding the workspace. Release builds install the same
series under `build-release/content/level`.

## In the editor

Open `level/playground.idkmap` in the installed `mapedit`, or create a map with
terrain, one spawn/entry marker, and one exit marker. Choose **Build & Play**.
It builds the current document, including unsaved edits, and launches the game
installed beside `mapedit`. The source map is unchanged. Only one playtest runs
at a time. **Stop playtest**, game exit, and editor shutdown release the child
process and remove its temporary level snapshot.

Select the exit marker and set **Next level** to a compiled path such as
`level/finish.idklvl`. Leaving it empty marks the end of the run. Destinations
are relative to the game's level root (the installed `content` directory by
default), regardless of the source map or temporary playtest snapshot's location.
Build each destination map to its runtime path before playing. The compiler
validates path syntax, while the game checks existence and contents when the
transition is requested; levels can therefore reference one another in any build
order. The installed playground leads to the finish example's elevated floor.

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

For levels outside installed content, set a common destination root explicitly:

```bash
./build-debug/game --level /tmp/my-levels/start.idklvl --level-root /tmp/my-levels
```

`--level-root` requires `--level`. Destinations must fit in 255 bytes, use `/`
separators, and end in `.idklvl`. Absolute paths, empty path components, `.`/`..`
components, control characters, backslashes, and colons are rejected. The runtime
also rejects symlinks resolving outside the root. The initial `--level` may live
outside this root, allowing editor snapshots to reference installed levels.

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

Movement uses a Source-inspired acceleration model: 6 m/s normally and 10 m/s
with Shift, quick ground acceleration, and friction that stops the player
promptly after releasing movement. Diagonal input has the same target speed.
Jumping preserves horizontal momentum; releasing movement in the air does not
stop the player, and air strafing provides limited directional control.
Gravity is 20 m/s² and jump speed is 7.2 m/s, giving roughly a 1.25-metre rise
and a 0.7-second jump on flat ground. Movement and collision are substepped at
up to 1/120 second to keep the response consistent across fixed update rates.

Player physics runs at the engine's fixed rate (30 Hz by default). The camera
interpolates the last two physics positions using the fixed timer's remaining
fraction, so movement renders smoothly at the display frame rate. Mouse look
updates every frame. Restarts and level transitions reset both positions to
the spawn, preventing the camera from blending across a teleport.

An orange translucent box shows the exit volume, using the exit marker's
position, rotation, and scale. Entering it with the player's capsule queues the
destination, or reports **Exit reached** if no destination is set. R restarts
the current level. Entry and exit marker geometry is excluded from terrain and
collision. Spawning inside an exit requires leaving and entering it before it
activates, preventing immediate transition loops.

## Transition lifetime

`Playtest` keeps the engine services, `PhysicsWorld`, `Player`, and `LevelRunner`
alive across transitions. Exit detection calls `requestTransition()` during a
fixed update. At the start of application `onPreRender()`, after the renderer has
cleared its previous draw queue, `processPending()` loads the next level into the
inactive CPU arena and prepares its GPU model and collision mesh. Loading is
synchronous and may pause a frame; background streaming is not implemented.

Only after preparation succeeds does the runner replace active terrain, reset
the existing player's pose, velocity and contacts, and release the previous GPU
model. The playtest resets camera orientation and movement at the new entry.
The engine and game process continue running. Level-owned geometry and collision
are replaced; there is no inventory or campaign state yet.

Missing or corrupt files and resource failures retain the active level and player
state. The console and window title report failure. Leave and re-enter the exit
to retry, for example after rebuilding a missing destination. `generation()`
changes only on successful commits; references into `current()` are valid only
until the next successful commit or shutdown. Shut the player down before its
physics world, and shut the runner down while its renderer and world still exist.

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

- Eight-byte `IDKLVL\r\n` signature, uint32 version (2), uint32 terrain count.
- Entry and exit: position XYZ, rotation XYZ in degrees, signed scale XYZ.
- A 256-byte zero-terminated destination path; empty means completion.
- Per terrain: 48-byte zero-terminated name, RGBA floats, uint32 double-sided
  flag, uint32 vertex count, uint32 index count.
- Per vertex: position XYZ, normal XYZ, UV, tangent XYZW, colour RGBA floats.
- Per triangle: three uint32 indices local to that terrain's vertices.

There are at most 62 terrain objects, 32,768 vertices, and 196,608 indices.
Both build and load use caller-owned arenas. The runner supplies two 4 MiB
banks so preparation cannot overwrite the active level, plus 64 KiB of model
descriptor scratch. Version 1 artifacts remain readable with no destination.
Failed loads rewind the arena and retain the caller's previous level. File
writes use a temporary file in the output directory and publish atomically.
Jolt owns its collision allocations and uses an 8 MiB update scratch allocator.
`PhysicsWorld::initialize()` takes a persistent arena for the world and factory
records; the playtest supplies 64 KiB. Reclaim that storage after world shutdown.
The persistent physics world owns active and, during preparation, staged terrain;
the player owns its character controller independently of level data.

This initial pipeline supports primitive terrain and a first-person playtest.
Dynamic props, game scripting, imported asset placement, navigation baking,
and background level streaming are not included.

## Verification

Enable `IDK_BUILD_EDITOR_TESTS=ON`, build `idk_level_tests` and
`idk_level_runner_tests`, and run (the runner test needs OpenGL 4.6):

```bash
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ctest --test-dir build-debug/cmake/idk_engine/level --output-on-failure
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build-debug/game --level build-debug/content/level/playground.idklvl --frames 10
```

The CPU tests cover compilation, mirrored geometry, artifact roundtrips,
corrupt/truncated files, arena rollback, source/output preservation, capsule
grounding, wall collision, movement speed, braking, air momentum and steering,
jump height/timing at 30/60/120 Hz, restart, plane collision, and exit detection.
The offscreen command also checks real graphics initialization and level upload.
The runner test repeatedly swaps rendered and collidable levels, checks queued
commit timing, staged collision isolation, missing/corrupt destinations, root
containment, resource exhaustion rollback, resource reuse, and shutdown.
