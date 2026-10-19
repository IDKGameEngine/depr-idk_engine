// https://shader-slang.org/slang/user-guide/glsl-target-specific
// https://docs.shader-slang.org/en/latest/coming-from-glsl.html
//
// Shared C++ / Slang header — GPU resource binding registry
//
// OpenGL has independent UBO, SSBO, image and texture-unit namespaces.
// Keep bindings low and validate device limits. The built-in material uses
// UBO 2 (camera), UBO 3 (draw), UBO 4 (material), texture unit 5 (albedo).
// Distinct compiler bindings also avoid Vulkan-style Slang binding collisions.
// GPU structs use std140 UBO / std430 SSBO and column-major matrices.

#ifndef IDK_GFX_SLANG_HPP
#define IDK_GFX_SLANG_HPP

#ifdef __cplusplus
#include "defs.hpp"
#define IDK_SLANG_NAMESPACE idk::gfx::slang
#define IDK_GPU_CONST static constexpr
#else
#include "defs.slang"
#define IDK_SLANG_NAMESPACE idk
#define IDK_GPU_CONST static const
#endif

#define UBO_STRUCT(Idx_, Content_)                                                                                     \
    struct UniformBuffer0##Idx_                                                                                        \
    {                                                                                                                  \
        IDK_GPU_CONST uint32_t BIND_IDX = Idx_;                                                                        \
        Content_                                                                                                       \
    }

#define SSBO_STRUCT(Idx_, Content_)                                                                                    \
    struct StorageBuffer0##Idx_                                                                                        \
    {                                                                                                                  \
        IDK_GPU_CONST uint32_t BIND_IDX = Idx_;                                                                        \
        Content_                                                                                                       \
    }

namespace IDK_SLANG_NAMESPACE
{
    IDK_GPU_CONST uint32_t UBO_BIND_BASE = 0;
    IDK_GPU_CONST uint32_t SSBO_BIND_BASE = 0;
    IDK_GPU_CONST uint32_t IMG_BIND_BASE = 0;

    struct GlobalData
    {
        IDK_GPU_CONST uint32_t BIND_IDX = UBO_BIND_BASE + 0;
    };

    struct PerFrameData
    {
        IDK_GPU_CONST uint32_t BIND_IDX = UBO_BIND_BASE + 1;
        float prevTime;
        float currTime;
        float deltaTime;
        float timescale;
        vec2 winSize;
        vec2 padding1;
    };

    struct PerCameraData
    {
        IDK_GPU_CONST uint32_t BIND_IDX = UBO_BIND_BASE + 2;
        mat4 View;
        mat4 Proj;
    };

    struct PerDrawData
    {
        IDK_GPU_CONST uint32_t BIND_IDX = 3;
        mat4 Model;
    };

    struct MaterialData
    {
        IDK_GPU_CONST uint32_t BIND_IDX = 4;
        vec4 baseColor;
        vec4 alpha; // mode: 0 opaque, 1 mask, 2 blend; cutoff in y
    };

    IDK_GPU_CONST uint32_t BIND_ALBEDO = 5;

    IDK_GPU_CONST uint32_t BIND_NBODY_IN = SSBO_BIND_BASE + 4;
    IDK_GPU_CONST uint32_t BIND_NBODY_OUT = SSBO_BIND_BASE + 5;

    struct NBodyVertex
    {
        IDK_GPU_CONST float MIN_MASS = 16.0f;
        IDK_GPU_CONST float MAX_MASS = 512.0f;
        IDK_GPU_CONST uint32_t MAX_BODIES = 1 * 1024;
        IDK_GPU_CONST uint32_t GROUP_SIZE = 32;

        vec4 pos;
        vec4 vel;
        vec4 acc;
    };

    // struct NBodyVertexArray
    // {
    //     NBodyVertex verts[NBodyVertex::MAX_BODIES];
    // };

    IDK_GPU_CONST int AUTOMATA_WIDTH = 1024;

} // namespace IDK_SLANG_NAMESPACE

#endif // IDK_GFX_SLANG_HPP
