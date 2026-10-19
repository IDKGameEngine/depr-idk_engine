# OpenGL renderer

The renderer requires OpenGL 4.6 core and SDL3. Vulkan sources remain available
but are not part of the active renderer target.

## Build environment

Build inside the Steam Linux Runtime container. From the workspace root, run
`./env.sh` to enter `steamrt4`, then run `./build.sh` in that shell. Do not reuse
the same CMake build directory between host and container builds: cached system
library paths and compiler artifacts belong to the environment that created them.
After an accidental host build, configure with `cmake --fresh --preset debug
-DIDK_APP_NAME=game -DIDK_POLY_DIR=/path/to/idk` from `idk_build` inside the
container, clean the build artifacts, then run the normal build script.

## Building shaders

Slang is a build dependency, not a runtime dependency. CMake searches the `bin` directories under
`CMAKE_PREFIX_PATH`, as well as `PATH`, for `slangc`. The existing build presets
set the prefix to `idk_build/local`. A custom
installation can be selected with `-DIDK_SLANGC=/absolute/path/to/slangc`.
The compiler must have execute permission. Verified with Slang 2026.13.1 and
2026.20.

`shaders/default.slang` is compiled separately for its vertex and fragment entry
points, targeting GLSL 4.60 with column-major matrix storage. The resulting GLSL
is embedded into the engine. Include dependencies are tracked by Slang depfiles;
changing a shared header rebuilds the shader. No generated GLSL is maintained by
hand. OpenGL compiles and links the embedded GLSL at startup.

For additional shaders, use `idk_compile_slang(output source entry stage)` from
`cmake/Slang.cmake`, then load the generated GLSL into `CreateShader`. The current
shader interface supports graphics programs with the uniform blocks below.
`ReloadShader` accepts replacement GLSL and preserves the old program on failure;
a file watcher or runtime Slang compiler is not included.

## Resource and frame API

Create meshes, textures, samplers, shaders, and materials during application
`onInit`. Descriptors are consumed synchronously, so temporary vertex/index
arrays and GLSL strings are safe. GPU objects are owned by fixed-capacity libidk
resource pools. The renderer uses bounded static storage and permits one active
instance per process. Initialization failure logs an error and requests engine
shutdown; `IsInitialized()` reports success. Shutdown releases the static slot. Public handles include a slot and generation; `isValid()` only
checks the invalid sentinel. The renderer checks pool liveness before use.

The default vertex layout is location 0 = float3 position, location 1 = float2 UV.
Location 4 is an optional float4 vertex colour (white when absent). Imported
models also supply normals at location 2 and tangents at location 3.
Meshes use triangle lists and optional uint32 indices. Textures accept tightly
packed CPU rows; RGBA16F/RGBA32F uploads use float components. Use `SRGBA8` for
sRGB colour textures and linear formats for data textures. An invalid material
texture selects a white fallback; an invalid sampler selects linear repeat
sampling. Custom samplers support all six glTF minification filters, nearest/linear
magnification, and independent repeat/clamp/mirrored-repeat wrapping per axis.

```cpp
// onInit: retain handles in the application.
idk::gfx::MaterialDesc desc;
desc.shader = renderer.GetDefaultShader();
desc.baseColor = {0.3f, 0.8f, 0.6f, 1.0f};
material = renderer.CreateMaterial(desc);

// onPreRender: renderer pre-render runs first and clears its submission queue.
renderer.SetCamera({camera.getView(), camera.getProj()});
renderer.Submit({mesh, material, transform.GetModelMatrix()});

// onShutdown: application shutdown runs before renderer/context shutdown.
renderer.DestroyMaterial(material);
renderer.DestroyMesh(mesh);
```

Use an explicitly initialized `idk::Camera`; its default constructor does not
initialize projection parameters. Camera matrices use OpenGL's default -1..1
clip depth and a right-handed view, with no Vulkan Y flip. Set the projection
aspect ratio from the actual drawable pixel dimensions on resize.

The renderer executes in `onMidRender`, before the editor's ImGui rendering, and
swaps in `onPostRender`. Submissions outside the pre-render collection phase are
rejected. All renderer operations belong to the thread that initialized its
context. Resource deletion after submission is supported: stale queued draws
are discarded, including draws whose material dependencies were destroyed.
Materials do not retain references to their dependencies. The default shader
and fallback resources live until renderer shutdown.

Opaque draws are grouped by program/material/mesh. Alpha draws follow opaque
ones and are sorted back-to-front using transformed object origins in view
space. Alpha materials disable depth writes and use straight-alpha blending. Masked
materials discard below `alphaCutoff` and write depth; opaque materials ignore
alpha. Negative-determinant model transforms reverse front-face winding.
This is object-level transparency sorting, so intersecting transparent meshes
still need a more advanced strategy. Depth testing and back-face culling are
enabled by default and can be controlled per material. GL state is established
at each scene pass; the small binding cache only lives within that pass so
editor rendering cannot invalidate it between frames.

## Imported models

`idkengine_assets` exposes a graphics-independent CPU scene and a glTF/GLB
importer. See [the asset pipeline](../asset/README.md) for the supported profile,
conversion dependencies, and upload example. `UploadModel` owns GPU resources
until `DestroyModel` or renderer shutdown. `SubmitModel` submits all instances
with their accumulated node transforms and an optional instance-root transform.
CPU model arena storage may be reset immediately after upload.

The new renderer and asset targets compile with `-fno-exceptions`. Named functions
and a comparator replace callbacks/lambda captures; frame submissions use a bounded
`FixedList` and in-place sorting with submission order as a final key. There is no
per-frame engine heap allocation. Resource creation/upload returns invalid handles
on failure; submission returns false on invalid data or capacity exhaustion.
Model metadata uses the explicit budgets in `RenderLimits`. Shader logs use bounded
buffers. SDL, ImGui and the GL driver retain their own memory-management behavior.

## Shader ABI

Shared definitions are in `include/idk/gfx/slang/slang.hpp`:

| Resource | Binding | Layout |
| --- | --- | --- |
| PerFrameData | UBO 1 | 32 bytes, std140 |
| PerCameraData | UBO 2 | View + Proj, 128 bytes, std140 |
| PerDrawData | UBO 3 | Model, 64 bytes, std140 |
| MaterialData | UBO 4 | baseColor + alpha mode/cutoff, 32 bytes, std140 |
| Albedo | Texture unit 5 | Slang Sampler2D / GLSL sampler2D |

Use `UniformBufferRO` for these blocks and `mul(matrix, vector)` for transforms.
Slang may represent the equivalent transform as row-major matrices and
vector-matrix multiplication in emitted GLSL; do not rewrite that generated code.
C++ size/offset assertions and the rendering integration test check the shared
contract. Program linking validates active uniform-block sizes by binding,
without relying on generated symbol names. Custom shaders must keep member
layouts consistent with the shared structs; size checks alone cannot establish
semantic compatibility. Unsupported uniform blocks are rejected. Storage-buffer,
image, compute-dispatch, and arbitrary material parameter APIs are future work.

OpenGL buffer/image/texture bindings use independent namespaces. The built-in
shader uses distinct low compiler binding numbers to avoid Vulkan-style Slang
binding collisions. The renderer checks its required binding limits at startup.
Avoid scalar layouts and Vulkan-only shader features when targeting OpenGL.

## Validation

Configure with `-DIDK_BUILD_GFX_TESTS=ON`, build `idk_gfx_smoke`, then run:

```bash
ctest --test-dir build-debug/cmake/idk_engine/gfx --output-on-failure
```

On systems with SDL's offscreen driver and Mesa software rendering:

```bash
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
    ctest --test-dir build-debug/cmake/idk_engine/gfx --output-on-failure
```

The test creates a real GL context and reads rendered pixels to check camera/model
transforms, texture/material bindings, depth testing, alpha sorting, failed shader
replacement, and stale handle rejection.
It also checks tightly packed RGB uploads, invalid indices, and GL errors.

This first implementation draws directly into the window framebuffer and uses
ordinary buffer updates. HDR targets/tone mapping, a render graph, GPU culling,
indirect draws, and persistent mapped upload rings are not implemented yet.
