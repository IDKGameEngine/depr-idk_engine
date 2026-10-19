# Model assets

`idkengine_assets` is a CPU-only static library built with `-fno-exceptions`.
The importer uses cgltf (C) and a PNG/JPEG decoder with explicit arena allocation
callbacks. It has no Assimp, SDL, OpenGL, growing STL-container, lambda, or
`std::function` dependency.

```text
Authoring files → glTF 2.0 / GLB → ImportModel → asset::Model views
                                                  ├─ UploadModel today
                                                  └─ native asset writer later
```

## Memory and errors

The caller supplies two non-overlapping `idk::Arena` instances over existing memory:

- **Output** holds model arrays, strings, geometry, and decoded pixels. All spans
  and string views in `Model` refer to this memory. Keep it alive until consumers
  finish using the CPU data.
- **Scratch** holds file bytes, parser state, accessor conversion, and image
  decoder working memory. Its original cursor is restored after every import.

There is no heap fallback. `ImportModel` returns `bool` and fills a bounded
`ImportError` with an error code and message. On failure both arena cursors return
to their entry marks and the result model is cleared; existing earlier arena data
is preserved. Failed individual arena allocations do not advance the cursor.
Choose budgets for the asset workload. The example below uses static buffers;
an external conversion tool can supply its own preallocated memory instead.

```cpp
alignas(std::max_align_t) static std::byte modelBytes[4 * 1024 * 1024];
alignas(std::max_align_t) static std::byte scratchBytes[8 * 1024 * 1024];
idk::Arena output(modelBytes), scratch(scratchBytes);
idk::asset::Model cpu;
idk::asset::ImportError error;
if (!idk::asset::ImportModel("model/example.glb", output, scratch, cpu, error))
{
    idk::vlog(idk::LogType::ERROR, "Asset", "%s", error.message);
    return;
}
auto gpu = renderer.UploadModel(cpu);
if (!gpu.isValid()) return;
output.reset(); // Upload is synchronous and retains no references to CPU data.

// During application onPreRender:
renderer.SubmitModel(gpu, rootTransform);
// During application onShutdown:
renderer.DestroyModel(gpu);
```

Asset structures contain views and ordinary data, not an on-disk ABI. Keep the
original authoring files and write an explicit native format in the converter.
`libidk/memory/Arena.hpp` and `libidk/dsa/FixedList.hpp` provide checked storage;
libc file I/O, SDL, ImGui, and graphics drivers may manage their own allocations.
The engine importer/parser/decoder allocations use the supplied arenas, and the
engine renderer's storage and frame processing do not allocate from the heap.

## Shape generation and GLB export

`CreateShape(ShapeKind, Arena&, Primitive&)` generates a unit box, UV sphere,
closed cylinder, or XZ plane with normals, UVs, tangents, colours, and triangle
indices. Geometry belongs to the supplied arena, with no heap fallback. Failure
clears the primitive and restores the entry mark.

`ExportModelGlb(const Model&, const char* path, Arena& scratch, ExportError&,
bool overwrite = false)` writes static, untextured GLB 2.0 files. It preserves
geometry streams, PBR factors, names, node transforms, hierarchy, and instancing.
Repeated primitive spans share buffer views and accessors. Textures and sheared
node matrices are rejected explicitly. Scratch is restored on success and failure.
Keep the input model and its borrowed data alive until export returns.

On Linux, export writes a temporary file beside the destination, then publishes
it atomically. A pre-existing file returns `AlreadyExists` unless `overwrite` is
true. Validation, allocation, write, and publication failures leave the previous
destination intact. Parent directories must already exist. See the
[shape editor](../editor/README.md) for a consumer and roundtrip integration tests.

## Supported profile

- glTF 2.0 JSON and GLB 2.0; external buffers/images, base64 buffers/images, and
  GLB-embedded resources. PNG/JPEG pixels decode to RGBA8. Required extensions
  are rejected explicitly, including unknown required extensions.
- Static triangle lists, optional indices, positions, normals, UV0, supplied
  tangents, and colour zero. Sparse floating-point vertex accessors are unpacked;
  sparse index accessors are rejected. Missing normals generate flat faces,
  splitting vertices when needed. Missing colours are white.
- Parent-before-child nodes retain names, local transforms, and shared primitive
  references. Geometry is not copied per node. The selected/default scene is
  imported. Scene depth is limited to 128 to bound recursion.
- Base colour, metallic/roughness, emissive factors, and their texture references;
  normal scale, occlusion strength, alpha modes/cutoff, and double-sided state.
  Metallic/roughness use B/G channels; occlusion uses R.
- Explicit image/texture/sampler indices preserve resource sharing, all six
  minification filters, nearest/linear magnification, and per-axis wrap modes.
- `dependencies` lists source and referenced local buffer/image paths, with URI
  escapes decoded; paths are retained relative to the supplied source path.
  File paths are limited to 1023 bytes. `warnings` stores optional extension names.

Coordinates are right-handed, +Y up, in metres. UV (0,0) and decoded row zero both
mean the top-left texel. No import-time UV or image flip is performed. Tangent W
retains handedness. Animation, skins, morphs, non-triangle topology, nonzero
texture UV sets, and extension payloads are rejected. `extras` is ignored.

## Rendering and limits

`UploadModel` uses bounded `libidk::FixedList` metadata and existing generational
resource pools. Partial uploads are cleaned up on failure. `ModelHandle` owns GPU
resources until destruction or renderer shutdown; callers can release/reset CPU
storage immediately after upload. `SubmitModel` returns false for stale handles,
invalid input, or draw-capacity exhaustion, without partially submitting a model.

The renderer currently displays **unlit base colour × texture × vertex colour**,
with sRGB decoding, alpha mask/blend, sampler state, and mirrored-instance culling.
PBR parameters/maps are retained in CPU data; full PBR lighting and normal mapping
remain future work. Only base-colour textures are uploaded today.

`RenderLimits` defines the current per-model maxima: 128 primitives, fewer than
128 source materials (one slot reserved for a default), 128 images, 64 samplers,
512 nodes, and 512 instances. There are 32 model slots and 8192 draws per frame.
Exceeding a budget returns an invalid handle/false. Modify explicit limits or use
multiple assets when larger scenes are needed.

The game's original GLB cube demo is installed in
`build-debug/content/model/sample/instanced-cubes.glb`. `PlatformManager` sets the
working directory to `content`, so game paths use `model/sample/...`.

## Validation

Inside `steamrt4`, configure with `-DIDK_BUILD_ASSET_TESTS=ON` and
`-DIDK_BUILD_GFX_TESTS=ON`, build, then run:

```bash
ctest --test-dir build-debug/cmake/idk_engine/asset --output-on-failure
SDL_VIDEODRIVER=offscreen \
    ctest --test-dir build-debug/cmake/idk_engine/gfx --output-on-failure
```

Tests are also compiled without exceptions. They check imported geometry, PBR
metadata, hierarchy/instancing, external and embedded images, pixel orientation,
malformed/unsupported files, both arena exhaustion paths and rollback, fixed-list
and draw-queue overflow, alpha/depth rendering, and GPU independence from reset
CPU storage. Original fixtures/demo can be regenerated with
`python3 asset/tests/GenerateFixtures.py` from `idk_engine`.
