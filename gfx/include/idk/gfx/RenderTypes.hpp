#pragma once

#include "Handle.hpp"
#include <glm/glm.hpp>
#include <cstddef>
#include <span>
#include <optional>
#include <string_view>

namespace idk::gfx
{
    enum class VertexAttribType : uint8_t
    {
        Float,
        Float2,
        Float3,
        Float4,
        UByte4Norm
    };

    struct VertexAttribute
    {
        uint32_t location = 0;
        VertexAttribType type = VertexAttribType::Float3;
        uint32_t offset = 0;
    };

    // Input spans/pointers are consumed synchronously by Create*.
    struct MeshDesc
    {
        const void *vertexData = nullptr;
        size_t vertexCount = 0;
        size_t vertexStride = 0;
        const uint32_t *indices = nullptr;
        size_t indexCount = 0;
        std::span<const VertexAttribute> attributes;
    };

    enum class TexFormat : uint8_t
    {
        R8,
        RGB8,
        RGBA8,
        SRGBA8,
        RGBA16F,
        RGBA32F
    };

    struct TextureDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        TexFormat format = TexFormat::RGBA8;
        // Tightly packed rows; floating point formats accept float components.
        const void *pixels = nullptr;
        bool genMipmaps = true;
    };

    enum class TextureFilter : uint8_t
    {
        Nearest,
        Linear
    };
    enum class TextureWrap : uint8_t
    {
        Repeat,
        ClampToEdge,
        MirroredRepeat
    };
    enum class TextureMinFilter : uint8_t
    {
        Default,
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear
    };

    struct SamplerDesc
    {
        TextureFilter filter = TextureFilter::Linear;
        TextureWrap wrap = TextureWrap::Repeat;
        std::optional<TextureWrap> wrapT; // Defaults to wrap for existing callers.
        TextureMinFilter minFilter = TextureMinFilter::Default;
    };

    struct ShaderDesc
    {
        std::string_view vertSrc;
        std::string_view fragSrc;
        std::string_view label;
    };

    enum class BlendMode : uint8_t
    {
        Opaque,
        Alpha,
        Mask
    };

    struct MaterialDesc
    {
        ShaderHandle shader;
        TextureHandle texture; // Invalid selects a white fallback texture.
        SamplerHandle sampler; // Invalid selects a linear repeat sampler.
        glm::vec4 baseColor{1.0f};
        BlendMode blend = BlendMode::Opaque;
        float alphaCutoff = 0.5f;
        bool depthTest = true;
        bool depthWrite = true;
        bool doubleSided = false;
    };

    struct Camera
    {
        glm::mat4 view{1.0f};
        // OpenGL's default -1..1 clip depth; no Vulkan Y flip.
        glm::mat4 proj{1.0f};
    };

    struct DrawItem
    {
        MeshHandle mesh;
        MaterialHandle material;
        glm::mat4 transform{1.0f};
    };
}
