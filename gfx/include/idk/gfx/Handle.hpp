#pragma once

#include <cstdint>
#include <limits>

namespace idk::gfx
{
    // Typed slot and generation. RenderEngine resolves the handle and checks
    // liveness against its resource pools; clients never hold an OpenGL name.
    template <typename Tag>
    struct Handle
    {
        uint32_t id = std::numeric_limits<uint32_t>::max();
        uint32_t gen = std::numeric_limits<uint32_t>::max();

        // Only the pool can establish liveness. Generation zero is valid in libidk.
        bool isValid() const { return id != std::numeric_limits<uint32_t>::max(); }

        friend bool operator==(const Handle &, const Handle &) = default;
    };

    struct MeshTag;

    struct TextureTag;

    struct ShaderTag;

    struct MaterialTag;

    struct RenderTargetTag;

    struct SamplerTag;

    struct ModelTag;

    using MeshHandle = Handle<MeshTag>;
    using TextureHandle = Handle<TextureTag>;
    using ShaderHandle = Handle<ShaderTag>;
    using MaterialHandle = Handle<MaterialTag>;
    using RenderTargetHandle = Handle<RenderTargetTag>;
    using SamplerHandle = Handle<SamplerTag>;
    using ModelHandle = Handle<ModelTag>;
}
