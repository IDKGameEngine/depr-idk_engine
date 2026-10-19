
#pragma once

#include <cstdint>
#include <limits>

namespace idk::gfx
{
    // Opaque, type-safe reference to a GPU resource. Never contains a raw
    // GL id -- RenderManager is the only thing that knows how to turn one
    // of these into an actual OpenGL object, via its internal resource
    // pools (see detail/Pool.hpp). Everything outside idk/gfx/*.cpp should
    // only ever see Handle<T>, never a GLuint.
    template <typename Tag>
    struct Handle
    {
        uint32_t id = std::numeric_limits<uint32_t>::max();
        uint32_t gen = std::numeric_limits<uint32_t>::max();

        // Only the pool can establish liveness. Generation zero is valid in libidk.
        bool isValid() const { return id != std::numeric_limits<uint32_t>::max(); }

        friend bool operator==(const Handle &, const Handle &) = default;
    };

    struct MeshTag
    {};

    struct TextureTag
    {};

    struct ShaderTag
    {};

    struct MaterialTag
    {};

    struct RenderTargetTag
    {};

    struct SamplerTag
    {};

    struct ModelTag
    {};

    using MeshHandle = Handle<MeshTag>;
    using TextureHandle = Handle<TextureTag>;
    using ShaderHandle = Handle<ShaderTag>;
    using MaterialHandle = Handle<MaterialTag>;
    using RenderTargetHandle = Handle<RenderTargetTag>;
    using SamplerHandle = Handle<SamplerTag>;
    using ModelHandle = Handle<ModelTag>;
}
