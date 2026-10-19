#include "idk/gfx/RenderEngine.hpp"
#include "idk/asset/Model.hpp"
#include "idk/gfx/slang/slang.hpp"
#include "idk/EngineAPI.hpp"
#include "libidk/dsa/ResourceManager.hpp"
#include "libidk/dsa/FixedList.hpp"
#include "libidk/gpu/gl.hpp"
#include "DefaultShaders.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <bit>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <limits>
#include <cmath>
#include <thread>
#include <tuple>
#include <utility>

namespace idk::gfx
{
    namespace
    {
        void error(const char *message)
        {
            idk::vlog(idk::LogType::ERROR, "OpenGL renderer", "%s", message);
        }

        void errorf(const char *format, ...)
        {
            char message[1024];
            va_list args;
            va_start(args, format);
            std::vsnprintf(message, sizeof(message), format, args);
            va_end(args);
            error(message);
        }

        GLenum textureWrap(TextureWrap mode)
        {
            return mode == TextureWrap::Repeat           ? GL_REPEAT
                   : mode == TextureWrap::MirroredRepeat ? GL_MIRRORED_REPEAT
                                                         : GL_CLAMP_TO_EDGE;
        }

        bool finite(const glm::mat4 &matrix)
        {
            for (unsigned c = 0; c < 4; ++c)
                for (unsigned r = 0; r < 4; ++r)
                    if (!std::isfinite(matrix[c][r]))
                        return false;
            return true;
        }

        [[maybe_unused]] void APIENTRY debugMessage(GLenum, GLenum type, GLuint, GLenum severity, GLsizei,
                                                    const GLchar *message, const void *)
        {
            if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
                return;
            idk::vlog(type == GL_DEBUG_TYPE_ERROR ? idk::LogType::ERROR : idk::LogType::WARN, "OpenGL", "%s", message);
        }

        enum class ObjectKind
        {
            Buffer,
            VertexArray,
            Texture,
            Sampler,
            Program
        };

        struct GlObject
        {
            GLuint id = 0;
            ObjectKind kind = ObjectKind::Buffer;
            GlObject() = default;

            explicit GlObject(ObjectKind type) : kind(type)
            {
                switch (kind)
                {
                    case ObjectKind::Buffer:
                        glCreateBuffers(1, &id);
                        break;
                    case ObjectKind::VertexArray:
                        glCreateVertexArrays(1, &id);
                        break;
                    case ObjectKind::Texture:
                        glCreateTextures(GL_TEXTURE_2D, 1, &id);
                        break;
                    case ObjectKind::Sampler:
                        glCreateSamplers(1, &id);
                        break;
                    case ObjectKind::Program:
                        id = glCreateProgram();
                        break;
                }
            }

            ~GlObject()
            {
                if (!id)
                    return;
                switch (kind)
                {
                    case ObjectKind::Buffer:
                        glDeleteBuffers(1, &id);
                        break;
                    case ObjectKind::VertexArray:
                        glDeleteVertexArrays(1, &id);
                        break;
                    case ObjectKind::Texture:
                        glDeleteTextures(1, &id);
                        break;
                    case ObjectKind::Sampler:
                        glDeleteSamplers(1, &id);
                        break;
                    case ObjectKind::Program:
                        glDeleteProgram(id);
                        break;
                }
            }

            GlObject(const GlObject &) = delete;
            GlObject &operator=(const GlObject &) = delete;

            GlObject(GlObject &&other) noexcept : id(std::exchange(other.id, 0)), kind(other.kind) {}

            GlObject &operator=(GlObject &&other) noexcept
            {
                if (this == &other)
                    return *this;
                GlObject previous(std::move(*this));
                id = std::exchange(other.id, 0);
                kind = other.kind;
                return *this;
            }
        };

        struct Mesh
        {
            GlObject vao, vertices, indices;
            GLsizei count = 0;
            bool indexed = false;
        };

        struct Texture
        {
            GlObject object;
        };

        struct Sampler
        {
            GlObject object;
        };

        struct Shader
        {
            GlObject object;
        };

        struct Material
        {
            MaterialDesc desc;
        };

        struct ModelInstance
        {
            uint32_t primitive;
            glm::mat4 transform;
        };

        struct ModelResource
        {
            idk::FixedList<MeshHandle, RenderLimits::MaxModelPrimitives> meshes;
            idk::FixedList<MaterialHandle, RenderLimits::MaxModelMaterials> materials;
            idk::FixedList<TextureHandle, RenderLimits::MaxModelImages> textures;
            idk::FixedList<SamplerHandle, RenderLimits::MaxModelSamplers> samplers;
            idk::FixedList<uint32_t, RenderLimits::MaxModelPrimitives> meshMaterials;
            idk::FixedList<ModelInstance, RenderLimits::MaxModelInstances> instances;
        };

        struct DrawRecord
        {
            DrawItem item;
            uint32_t program = 0, order = 0;
            float depth = 0;
            bool alpha = false;
        };

        struct DrawLess
        {
            bool operator()(const DrawRecord &a, const DrawRecord &b) const
            {
                if (a.alpha != b.alpha)
                    return !a.alpha;
                if (a.alpha)
                {
                    if (a.depth != b.depth)
                        return a.depth < b.depth;
                    return a.order < b.order;
                }
                return std::tie(a.program, a.item.material.id, a.item.mesh.id, a.order) <
                       std::tie(b.program, b.item.material.id, b.item.mesh.id, b.order);
            }
        };
        template <class T, uint32_t N = 1024>
        using Pool = idk::ResourceManager<T, N>;

        template <class T, class Tag, uint32_t N>
        T *lookup(Pool<T, N> &pool, Handle<Tag> handle)
        {
            return pool.get({handle.id, handle.gen});
        }

        template <class Tag, class T, uint32_t N>
        Handle<Tag> store(Pool<T, N> &pool, T &&value)
        {
            auto handle = pool.createResource(std::move(value));
            if (handle == idk::ResourceHandle<T>::Invalid)
                error("Resource pool is full");
            return {handle.idx, handle.gen};
        }

        GLuint compileStage(GLenum stage, std::string_view source, std::string_view label)
        {
            if (source.empty() || source.size() > size_t(std::numeric_limits<GLint>::max()))
                return 0;
            GLuint shader = glCreateShader(stage);
            const char *data = source.data();
            GLint size = GLint(source.size());
            glShaderSource(shader, 1, &data, &size);
            glCompileShader(shader);
            GLint success = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (success)
                return shader;
            char log[8192]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            errorf("Shader %.*s: %s", int(std::min(label.size(), size_t(255))), label.empty() ? "" : label.data(), log);
            glDeleteShader(shader);
            return 0;
        }

        GlObject linkProgram(const ShaderDesc &desc)
        {
            GLuint vertex = compileStage(GL_VERTEX_SHADER, desc.vertSrc, desc.label);
            if (!vertex)
                return {};
            GLuint fragment = compileStage(GL_FRAGMENT_SHADER, desc.fragSrc, desc.label);
            if (!fragment)
            {
                glDeleteShader(vertex);
                return {};
            }
            GlObject program(ObjectKind::Program);
            glAttachShader(program.id, vertex);
            glAttachShader(program.id, fragment);
            glLinkProgram(program.id);
            glDetachShader(program.id, vertex);
            glDetachShader(program.id, fragment);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            GLint success = GL_FALSE;
            glGetProgramiv(program.id, GL_LINK_STATUS, &success);
            if (!success)
            {
                char log[8192]{};
                glGetProgramInfoLog(program.id, sizeof(log), nullptr, log);
                errorf("Program %.*s: %s", int(std::min(desc.label.size(), size_t(255))),
                       desc.label.empty() ? "" : desc.label.data(), log);
                return {};
            }
            // Generated names are unstable. Validate uniform blocks by numeric binding.
            GLint blocks = 0;
            glGetProgramInterfaceiv(program.id, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &blocks);
            constexpr GLenum properties[] = {GL_BUFFER_BINDING, GL_BUFFER_DATA_SIZE};
            for (GLint i = 0; i < blocks; ++i)
            {
                GLint values[2]{};
                glGetProgramResourceiv(program.id, GL_UNIFORM_BLOCK, GLuint(i), 2, properties, 2, nullptr, values);
                GLint expected = 0;
                if (values[0] == GLint(slang::PerFrameData::BIND_IDX))
                    expected = sizeof(slang::PerFrameData);
                if (values[0] == GLint(slang::PerCameraData::BIND_IDX))
                    expected = sizeof(slang::PerCameraData);
                if (values[0] == GLint(slang::PerDrawData::BIND_IDX))
                    expected = sizeof(slang::PerDrawData);
                if (values[0] == GLint(slang::MaterialData::BIND_IDX))
                    expected = sizeof(slang::MaterialData);
                if (!expected || values[1] != expected)
                {
                    errorf("Unsupported uniform block at binding %d (size %d, expected %d)", values[0], values[1],
                           expected);
                    return {};
                }
            }
            if (!desc.label.empty())
                glObjectLabel(GL_PROGRAM, program.id, GLsizei(std::min(desc.label.size(), size_t(255))),
                              desc.label.data());
            return program;
        }

        struct Context
        {
            SDL_Window *window = nullptr;
            SDL_GLContext gl = nullptr;

            ~Context()
            {
                if (gl)
                    SDL_GL_DestroyContext(gl);
            }
        };
    }

    struct RenderEngine::Impl
    {
        // Declared first: destroyed last, after all GPU resource pools.
        Context context;
        std::thread::id owner = std::this_thread::get_id();
        Pool<Mesh> meshes;
        Pool<Texture> textures;
        Pool<Sampler, 128> samplers;
        Pool<Shader, 256> shaders;
        Pool<Material> materials;
        Pool<ModelResource, RenderLimits::MaxModels> models;
        GlObject frameBuffer, cameraBuffer, drawBuffer, materialBuffer;
        TextureHandle white;
        SamplerHandle linear;
        ShaderHandle defaultShader;
        Camera camera;
        glm::vec4 clearColor{0.05f, 0.05f, 0.08f, 1.0f};
        idk::FixedList<DrawRecord, RenderLimits::MaxDraws> draws;
        std::atomic_flag *occupied = nullptr;
        bool initialized = false;
        int width = 0, height = 0;
        int viewportX = 0, viewportY = 0, viewportWidth = 0, viewportHeight = 0;
        GLint maxTextureSize = 0, maxAttributes = 0, maxStride = 0, maxOffset = 0;
        float time = 0.0f;
        bool accepting = false;

        ~Impl()
        {
            if (context.gl)
                SDL_GL_MakeCurrent(context.window, context.gl);
        }

        bool checkThread() const
        {
            if (owner == std::this_thread::get_id())
                return true;
            error("RenderEngine called from a non-owning thread");
            return false;
        }
    };

    static_assert(sizeof(slang::PerFrameData) == 32);
    static_assert(sizeof(slang::PerCameraData) == 128);
    static_assert(offsetof(slang::PerCameraData, Proj) == 64);
    static_assert(sizeof(slang::PerDrawData) == 64);
    static_assert(sizeof(slang::MaterialData) == 32);

    RenderEngine::RenderEngine() = default;

    RenderEngine::~RenderEngine()
    {
        release();
    }

    bool RenderEngine::IsInitialized() const
    {
        return mImpl && mImpl->initialized;
    }

    void RenderEngine::release()
    {
        if (!mImpl || !mImpl->checkThread())
            return;
        auto *occupied = mImpl->occupied;
        mImpl->~Impl();
        mImpl = nullptr;
        occupied->clear();
    }

    void RenderEngine::onInit(EngineAPI &api)
    {
        if (mImpl)
        {
            error("RenderEngine already initialized");
            api.quit();
            return;
        }
        alignas(Impl) static std::byte storage[sizeof(Impl)];
        static std::atomic_flag occupied = ATOMIC_FLAG_INIT;
        if (occupied.test_and_set())
        {
            error("Only one renderer may be active");
            api.quit();
            return;
        }
        mImpl = new (storage) Impl{};
        mImpl->occupied = &occupied;
        auto &state = *mImpl;
        state.context.window = static_cast<SDL_Window *>(api.mVideo->getWindowHandle());
        if (!state.context.window)
        {
            error("No video window");
            release();
            api.quit();
            return;
        }
        state.context.gl = SDL_GL_CreateContext(state.context.window);
        if (!state.context.gl || !SDL_GL_MakeCurrent(state.context.window, state.context.gl))
        {
            errorf("Creating GL context: %s", SDL_GetError());
            release();
            api.quit();
            return;
        }
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) || !GLAD_GL_VERSION_4_6)
        {
            error("OpenGL 4.6 core is required");
            release();
            api.quit();
            return;
        }
#ifdef IDK_DEBUG
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugMessage, nullptr);
#endif
        if (!SDL_GL_SetSwapInterval(1))
            idk::vlog(idk::LogType::WARN, "OpenGL", "Swap interval: %s", SDL_GetError());
        idk::vlog(idk::LogType::INFO, "OpenGL", "OpenGL: %s", glGetString(GL_VERSION));
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &state.maxTextureSize);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &state.maxAttributes);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &state.maxStride);
        glGetIntegerv(GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET, &state.maxOffset);
        GLint ubos = 0, units = 0;
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &ubos);
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        if (ubos <= GLint(slang::MaterialData::BIND_IDX) || units <= GLint(slang::BIND_ALBEDO))
        {
            error("Insufficient resource bindings");
            release();
            api.quit();
            return;
        }
        state.frameBuffer = GlObject(ObjectKind::Buffer);
        state.cameraBuffer = GlObject(ObjectKind::Buffer);
        state.drawBuffer = GlObject(ObjectKind::Buffer);
        state.materialBuffer = GlObject(ObjectKind::Buffer);
        glNamedBufferStorage(state.frameBuffer.id, sizeof(slang::PerFrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
        glNamedBufferStorage(state.cameraBuffer.id, sizeof(slang::PerCameraData), nullptr, GL_DYNAMIC_STORAGE_BIT);
        glNamedBufferStorage(state.drawBuffer.id, sizeof(slang::PerDrawData), nullptr, GL_DYNAMIC_STORAGE_BIT);
        glNamedBufferStorage(state.materialBuffer.id, sizeof(slang::MaterialData), nullptr, GL_DYNAMIC_STORAGE_BIT);
        constexpr uint8_t white[] = {255, 255, 255, 255};
        state.white = CreateTexture({1, 1, TexFormat::RGBA8, white, false});
        state.linear = CreateSampler({});
        state.defaultShader = CreateShader({detail::DefaultVertex, detail::DefaultFragment, "Default Slang material"});
        if (!state.white.isValid() || !state.linear.isValid() || !state.defaultShader.isValid())
        {
            error("Default rendering resources failed");
            release();
            api.quit();
            return;
        }
        state.initialized = true;
    }

    void RenderEngine::onShutdown(EngineAPI &)
    {
        release();
    }

    MeshHandle RenderEngine::CreateMesh(const MeshDesc &desc)
    {
        if (!mImpl || !mImpl->checkThread())
            return {};
        constexpr auto maxCount = size_t(std::numeric_limits<GLsizei>::max());
        constexpr auto maxBytes = size_t(std::numeric_limits<GLsizeiptr>::max());
        if (!desc.vertexData || !desc.vertexCount || !desc.vertexStride || desc.attributes.empty() ||
            desc.vertexCount > maxCount || desc.indexCount > maxCount || desc.vertexStride > size_t(mImpl->maxStride) ||
            desc.vertexCount > maxBytes / desc.vertexStride || (desc.indexCount && !desc.indices) ||
            desc.indexCount > maxBytes / sizeof(uint32_t))
        {
            error("Invalid mesh data/count/stride");
            return {};
        }

        for (size_t i = 0; i < desc.attributes.size(); ++i)
        {
            const auto &attribute = desc.attributes[i];
            for (size_t j = 0; j < i; ++j)
                if (desc.attributes[j].location == attribute.location)
                {
                    error("Duplicate vertex attribute");
                    return {};
                }
            const unsigned type = unsigned(attribute.type);
            if (type > unsigned(VertexAttribType::UByte4Norm))
                return {};
            const size_t bytes = attribute.type == VertexAttribType::UByte4Norm ? 4 : (type + 1) * sizeof(float);
            if (attribute.location >= uint32_t(mImpl->maxAttributes) || attribute.offset > uint32_t(mImpl->maxOffset) ||
                attribute.offset > desc.vertexStride || bytes > desc.vertexStride - attribute.offset)
            {
                error("Invalid mesh vertex layout");
                return {};
            }
        }
        for (size_t i = 0; i < desc.indexCount; ++i)
            if (desc.indices[i] >= desc.vertexCount)
            {
                error("Mesh index exceeds vertex count");
                return {};
            }
        Mesh mesh;
        mesh.vao = GlObject(ObjectKind::VertexArray);
        mesh.vertices = GlObject(ObjectKind::Buffer);
        glNamedBufferStorage(mesh.vertices.id, GLsizeiptr(desc.vertexCount * desc.vertexStride), desc.vertexData, 0);
        glVertexArrayVertexBuffer(mesh.vao.id, 0, mesh.vertices.id, 0, GLsizei(desc.vertexStride));
        for (const auto &attribute : desc.attributes)
        {
            bool packed = attribute.type == VertexAttribType::UByte4Norm;
            GLint components = packed ? 4 : GLint(attribute.type) + 1;
            glEnableVertexArrayAttrib(mesh.vao.id, attribute.location);
            glVertexArrayAttribFormat(mesh.vao.id, attribute.location, components, packed ? GL_UNSIGNED_BYTE : GL_FLOAT,
                                      packed, attribute.offset);
            glVertexArrayAttribBinding(mesh.vao.id, attribute.location, 0);
        }
        mesh.indexed = desc.indexCount != 0;
        mesh.count = GLsizei(mesh.indexed ? desc.indexCount : desc.vertexCount);
        if (mesh.indexed)
        {
            mesh.indices = GlObject(ObjectKind::Buffer);
            glNamedBufferStorage(mesh.indices.id, GLsizeiptr(desc.indexCount * sizeof(uint32_t)), desc.indices, 0);
            glVertexArrayElementBuffer(mesh.vao.id, mesh.indices.id);
        }
        return store<MeshTag>(mImpl->meshes, std::move(mesh));
    }

    TextureHandle RenderEngine::CreateTexture(const TextureDesc &desc)
    {
        if (!mImpl || !mImpl->checkThread())
            return {};
        if (!desc.width || !desc.height || desc.width > uint32_t(mImpl->maxTextureSize) ||
            desc.height > uint32_t(mImpl->maxTextureSize))
        {
            error("Invalid texture dimensions");
            return {};
        }
        GLenum internal = GL_RGBA8, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
        switch (desc.format)
        {
            case TexFormat::R8:
                internal = GL_R8;
                format = GL_RED;
                break;
            case TexFormat::RGB8:
                internal = GL_RGB8;
                format = GL_RGB;
                break;
            case TexFormat::RGBA8:
                break;
            case TexFormat::SRGBA8:
                internal = GL_SRGB8_ALPHA8;
                break;
            case TexFormat::RGBA16F:
                internal = GL_RGBA16F;
                type = GL_FLOAT;
                break;
            case TexFormat::RGBA32F:
                internal = GL_RGBA32F;
                type = GL_FLOAT;
                break;
            default:
                return {};
        }
        Texture texture{GlObject(ObjectKind::Texture)};
        GLsizei levels = desc.genMipmaps ? GLsizei(std::bit_width(std::max(desc.width, desc.height))) : 1;
        glTextureStorage2D(texture.object.id, levels, internal, GLsizei(desc.width), GLsizei(desc.height));
        if (desc.pixels)
        {
            // Uploads accept tightly packed CPU memory, independently of pixel unpack state.
            GLint alignment, rowLength, skipPixels, skipRows, unpackBuffer;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glTextureSubImage2D(texture.object.id, 0, 0, 0, GLsizei(desc.width), GLsizei(desc.height), format, type,
                                desc.pixels);
            glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(unpackBuffer));
            if (desc.genMipmaps)
                glGenerateTextureMipmap(texture.object.id);
        }
        glTextureParameteri(texture.object.id, GL_TEXTURE_MAX_LEVEL, levels - 1);
        return store<TextureTag>(mImpl->textures, std::move(texture));
    }

    SamplerHandle RenderEngine::CreateSampler(const SamplerDesc &desc)
    {
        if (!mImpl || !mImpl->checkThread())
            return {};
        Sampler sampler{GlObject(ObjectKind::Sampler)};
        bool nearest = desc.filter == TextureFilter::Nearest;
        GLenum minFilter = nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
        switch (desc.minFilter)
        {
            case TextureMinFilter::Default:
                break;
            case TextureMinFilter::Nearest:
                minFilter = GL_NEAREST;
                break;
            case TextureMinFilter::Linear:
                minFilter = GL_LINEAR;
                break;
            case TextureMinFilter::NearestMipmapNearest:
                minFilter = GL_NEAREST_MIPMAP_NEAREST;
                break;
            case TextureMinFilter::LinearMipmapNearest:
                minFilter = GL_LINEAR_MIPMAP_NEAREST;
                break;
            case TextureMinFilter::NearestMipmapLinear:
                minFilter = GL_NEAREST_MIPMAP_LINEAR;
                break;
            case TextureMinFilter::LinearMipmapLinear:
                minFilter = GL_LINEAR_MIPMAP_LINEAR;
                break;
        }
        glSamplerParameteri(sampler.object.id, GL_TEXTURE_MIN_FILTER, minFilter);
        glSamplerParameteri(sampler.object.id, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(sampler.object.id, GL_TEXTURE_WRAP_S, textureWrap(desc.wrap));
        glSamplerParameteri(sampler.object.id, GL_TEXTURE_WRAP_T, textureWrap(desc.wrapT.value_or(desc.wrap)));
        return store<SamplerTag>(mImpl->samplers, std::move(sampler));
    }

    ShaderHandle RenderEngine::CreateShader(const ShaderDesc &desc)
    {
        if (!mImpl || !mImpl->checkThread())
            return {};
        Shader shader{linkProgram(desc)};
        if (!shader.object.id)
            return {};
        return store<ShaderTag>(mImpl->shaders, std::move(shader));
    }

    bool RenderEngine::ReloadShader(ShaderHandle handle, const ShaderDesc &desc)
    {
        if (!mImpl || !mImpl->checkThread())
            return false;
        auto *shader = lookup(mImpl->shaders, handle);
        if (!shader)
            return false;
        GlObject replacement = linkProgram(desc);
        if (!replacement.id)
            return false;
        shader->object = std::move(replacement);
        return true;
    }

    MaterialHandle RenderEngine::CreateMaterial(const MaterialDesc &desc)
    {
        if (!mImpl || !mImpl->checkThread())
            return {};
        if (!lookup(mImpl->shaders, desc.shader) ||
            (desc.texture.isValid() && !lookup(mImpl->textures, desc.texture)) ||
            (desc.sampler.isValid() && !lookup(mImpl->samplers, desc.sampler)))
        {
            error("Material references a stale or invalid resource");
            return {};
        }
        return store<MaterialTag>(mImpl->materials, Material{desc});
    }

    void RenderEngine::DestroyMesh(MeshHandle h)
    {
        if (mImpl && mImpl->checkThread())
        {
            mImpl->meshes.destroyResource({h.id, h.gen});
        }
    }

    void RenderEngine::DestroyMaterial(MaterialHandle h)
    {
        if (mImpl && mImpl->checkThread())
        {
            mImpl->materials.destroyResource({h.id, h.gen});
        }
    }

    void RenderEngine::DestroyTexture(TextureHandle h)
    {
        if (mImpl && mImpl->checkThread())
        {
            if (h != mImpl->white)
                mImpl->textures.destroyResource({h.id, h.gen});
        }
    }

    void RenderEngine::DestroySampler(SamplerHandle h)
    {
        if (mImpl && mImpl->checkThread())
        {
            if (h != mImpl->linear)
                mImpl->samplers.destroyResource({h.id, h.gen});
        }
    }

    void RenderEngine::DestroyShader(ShaderHandle h)
    {
        if (mImpl && mImpl->checkThread())
        {
            if (h != mImpl->defaultShader)
                mImpl->shaders.destroyResource({h.id, h.gen});
        }
    }

    namespace
    {
        void destroyModelResources(RenderEngine &renderer, const ModelResource &model)
        {
            for (auto h : model.materials)
                renderer.DestroyMaterial(h);
            for (auto h : model.meshes)
                renderer.DestroyMesh(h);
            for (auto h : model.textures)
                renderer.DestroyTexture(h);
            for (auto h : model.samplers)
                renderer.DestroySampler(h);
        }

        TextureWrap assetWrap(asset::Wrap value)
        {
            return value == asset::Wrap::Repeat           ? TextureWrap::Repeat
                   : value == asset::Wrap::MirroredRepeat ? TextureWrap::MirroredRepeat
                                                          : TextureWrap::ClampToEdge;
        }

        bool validateModel(const asset::Model &source)
        {
            using asset::NoIndex;
            if (source.primitives.empty() || source.nodes.empty() ||
                source.primitives.size() > RenderLimits::MaxModelPrimitives ||
                source.materials.size() >= RenderLimits::MaxModelMaterials ||
                source.images.size() > RenderLimits::MaxModelImages ||
                source.samplers.size() > RenderLimits::MaxModelSamplers ||
                source.nodes.size() > RenderLimits::MaxModelNodes)
            {
                error("Model is empty or exceeds RenderLimits");
                return false;
            }
            for (const auto &t : source.textures)
                if (t.image >= source.images.size() || t.sampler >= source.samplers.size())
                    return false;
            for (const auto &m : source.materials)
                for (auto index : {m.baseColorTexture, m.normalTexture, m.metallicRoughnessTexture, m.occlusionTexture,
                                   m.emissiveTexture})
                    if (index != NoIndex && index >= source.textures.size())
                        return false;
            return true;
        }

        bool uploadSamplers(RenderEngine &renderer, const asset::Model &source, ModelResource &model)
        {
            for (const auto &s : source.samplers)
            {
                SamplerDesc desc;
                desc.filter = s.mag == asset::Filter::Nearest ? TextureFilter::Nearest : TextureFilter::Linear;
                desc.wrap = assetWrap(s.wrapS);
                desc.wrapT = assetWrap(s.wrapT);
                switch (s.min)
                {
                    case asset::MinFilter::Nearest:
                        desc.minFilter = TextureMinFilter::Nearest;
                        break;
                    case asset::MinFilter::Linear:
                        desc.minFilter = TextureMinFilter::Linear;
                        break;
                    case asset::MinFilter::NearestMipmapNearest:
                        desc.minFilter = TextureMinFilter::NearestMipmapNearest;
                        break;
                    case asset::MinFilter::LinearMipmapNearest:
                        desc.minFilter = TextureMinFilter::LinearMipmapNearest;
                        break;
                    case asset::MinFilter::NearestMipmapLinear:
                        desc.minFilter = TextureMinFilter::NearestMipmapLinear;
                        break;
                    case asset::MinFilter::LinearMipmapLinear:
                        desc.minFilter = TextureMinFilter::LinearMipmapLinear;
                        break;
                }
                auto h = renderer.CreateSampler(desc);
                if (!h.isValid())
                    return false;
                model.samplers.push(h);
            }
            return true;
        }

        bool uploadMaterials(RenderEngine &renderer, const asset::Model &source, ModelResource &model)
        {
            using asset::NoIndex;
            TextureHandle images[RenderLimits::MaxModelImages]{};
            for (const auto &m : source.materials)
            {
                MaterialDesc desc;
                desc.shader = renderer.GetDefaultShader();
                desc.baseColor = m.baseColor;
                desc.blend = m.alphaMode == asset::AlphaMode::Blend  ? BlendMode::Alpha
                             : m.alphaMode == asset::AlphaMode::Mask ? BlendMode::Mask
                                                                     : BlendMode::Opaque;
                desc.alphaCutoff = m.alphaCutoff;
                desc.doubleSided = m.doubleSided;
                if (m.baseColorTexture != NoIndex)
                {
                    const auto &texture = source.textures[m.baseColorTexture];
                    if (!images[texture.image].isValid())
                    {
                        const auto &image = source.images[texture.image];
                        if (!image.width || !image.height ||
                            size_t(image.width) > std::numeric_limits<size_t>::max() / image.height / 4 ||
                            image.rgba.size() != size_t(image.width) * image.height * 4)
                            return false;
                        auto h = renderer.CreateTexture(
                            {image.width, image.height, TexFormat::SRGBA8, image.rgba.data(), true});
                        if (!h.isValid())
                            return false;
                        model.textures.push(h);
                        images[texture.image] = h;
                    }
                    desc.texture = images[texture.image];
                    desc.sampler = model.samplers[texture.sampler];
                }
                auto h = renderer.CreateMaterial(desc);
                if (!h.isValid())
                    return false;
                model.materials.push(h);
            }
            return true;
        }

        bool uploadMeshes(RenderEngine &renderer, const asset::Model &source, ModelResource &model)
        {
            using asset::NoIndex;
            uint32_t fallback = NoIndex;
            constexpr VertexAttribute attributes[] = {
                {0, VertexAttribType::Float3, offsetof(asset::Vertex, position)},
                {1, VertexAttribType::Float2, offsetof(asset::Vertex, uv)},
                {2, VertexAttribType::Float3, offsetof(asset::Vertex, normal)},
                {3, VertexAttribType::Float4, offsetof(asset::Vertex, tangent)},
                {4, VertexAttribType::Float4, offsetof(asset::Vertex, color)},
            };
            for (const auto &p : source.primitives)
            {
                uint32_t material = p.material;
                if (material == NoIndex)
                {
                    if (fallback == NoIndex)
                    {
                        MaterialDesc desc;
                        desc.shader = renderer.GetDefaultShader();
                        auto h = renderer.CreateMaterial(desc);
                        if (!h.isValid())
                            return false;
                        fallback = uint32_t(model.materials.size());
                        model.materials.push(h);
                    }
                    material = fallback;
                }
                else if (material >= source.materials.size())
                    return false;
                if ((p.indices.empty() ? p.vertices.size() : p.indices.size()) % 3)
                    return false;
                auto h = renderer.CreateMesh({p.vertices.data(), p.vertices.size(), sizeof(asset::Vertex),
                                              p.indices.data(), p.indices.size(), attributes});
                if (!h.isValid())
                    return false;
                model.meshes.push(h);
                model.meshMaterials.push(material);
            }
            return true;
        }

        bool buildInstances(const asset::Model &source, ModelResource &model)
        {
            using asset::NoIndex;
            glm::mat4 world[RenderLimits::MaxModelNodes];
            for (size_t i = 0; i < source.nodes.size(); ++i)
            {
                const auto &node = source.nodes[i];
                if (node.parent != NoIndex && node.parent >= i)
                    return false;
                world[i] = node.parent == NoIndex ? node.localTransform : world[node.parent] * node.localTransform;
                if (!finite(world[i]))
                    return false;
                for (auto primitive : node.primitives)
                {
                    if (primitive >= model.meshes.size() || !model.instances.push({primitive, world[i]}))
                        return false;
                }
            }
            return !model.instances.empty();
        }

        bool uploadModelResources(RenderEngine &renderer, const asset::Model &source, ModelResource &model)
        {
            return validateModel(source) && uploadSamplers(renderer, source, model) &&
                   uploadMaterials(renderer, source, model) && uploadMeshes(renderer, source, model) &&
                   buildInstances(source, model);
        }

    }

    ModelHandle RenderEngine::UploadModel(const idk::asset::Model &source)
    {
        if (!mImpl || !mImpl->checkThread())
            return {};
        ModelResource model;
        if (!uploadModelResources(*this, source, model))
        {
            destroyModelResources(*this, model);
            error("Model upload failed: invalid data, capacity, or resource failure");
            return {};
        }
        // Pool copies only bounded, trivially-copyable records; no vector ownership.
        auto handle = store<ModelTag>(mImpl->models, std::move(model));
        if (!handle.isValid())
            destroyModelResources(*this, model);
        return handle;
    }

    void RenderEngine::DestroyModel(ModelHandle handle)
    {
        if (!mImpl || !mImpl->checkThread())
            return;
        auto *model = lookup(mImpl->models, handle);
        if (!model)
            return;
        destroyModelResources(*this, *model);
        mImpl->models.destroyResource({handle.id, handle.gen});
    }

    bool RenderEngine::SubmitModel(ModelHandle handle, const glm::mat4 &transform)
    {
        if (!mImpl || !mImpl->checkThread())
            return false;
        auto *model = lookup(mImpl->models, handle);
        if (!model || !mImpl->accepting || model->instances.size() > RenderLimits::MaxDraws - mImpl->draws.size())
            return false;
        size_t previous = mImpl->draws.size();
        for (const auto &instance : model->instances)
            if (!Submit({model->meshes[instance.primitive], model->materials[model->meshMaterials[instance.primitive]],
                         transform * instance.transform}))
            {
                mImpl->draws.truncate(previous);
                return false;
            }
        return true;
    }

    bool RenderEngine::Submit(const DrawItem &item)
    {
        if (!mImpl || !mImpl->checkThread())
            return false;
        if (!mImpl->accepting || !lookup(mImpl->meshes, item.mesh) || !lookup(mImpl->materials, item.material))
            return false;
        if (!finite(item.transform))
            return false;
        return mImpl->draws.push(DrawRecord{item});
    }

    void RenderEngine::SetCamera(const Camera &camera)
    {
        if (mImpl && mImpl->checkThread() && finite(camera.view) && finite(camera.proj))
            mImpl->camera = camera;
    }

    void RenderEngine::SetClearColor(glm::vec4 color)
    {
        if (mImpl && mImpl->checkThread())
        {
            mImpl->clearColor = color;
        }
    }

    void RenderEngine::SetViewport(int x, int y, int width, int height)
    {
        if (!mImpl || !mImpl->checkThread())
            return;
        auto &s = *mImpl;
        s.viewportX = std::clamp(x, 0, s.width);
        s.viewportY = std::clamp(y, 0, s.height);
        s.viewportWidth = std::clamp(width, 0, s.width - s.viewportX);
        s.viewportHeight = std::clamp(height, 0, s.height - s.viewportY);
    }

    ShaderHandle RenderEngine::GetDefaultShader() const
    {
        return mImpl ? mImpl->defaultShader : ShaderHandle{};
    }

    void *RenderEngine::GetWindowHandle() const
    {
        return mImpl ? mImpl->context.window : nullptr;
    }

    void *RenderEngine::GetContextHandle() const
    {
        return mImpl ? mImpl->context.gl : nullptr;
    }

    void RenderEngine::onPreRender(EngineAPI &api)
    {
        if (!IsInitialized() || !mImpl->checkThread())
            return;
        auto &s = *mImpl;
        s.accepting = false;
        s.draws.clear();
        s.width = s.height = 0;
        if (!SDL_GL_MakeCurrent(s.context.window, s.context.gl))
        {
            error(SDL_GetError());
            api.quit();
            return;
        }
        if (!SDL_GetWindowSizeInPixels(s.context.window, &s.width, &s.height))
        {
            s.width = s.height = 0;
            error(SDL_GetError());
            api.quit();
            return;
        }
        s.accepting = true;
        s.viewportX = s.viewportY = 0;
        s.viewportWidth = s.width;
        s.viewportHeight = s.height;
        slang::PerFrameData frame{};
        frame.prevTime = s.time;
        frame.deltaTime = float(api.getDeltaTimeSec());
        s.time += frame.deltaTime;
        frame.currTime = s.time;
        frame.timescale = 1.0f;
        frame.winSize = glm::vec2(s.width, s.height);
        glNamedBufferSubData(s.frameBuffer.id, 0, sizeof(frame), &frame);
        // Explicitly reset state that affects clears (e.g. after editor rendering).
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, s.width, s.height);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearDepth(1.0);
        glClearColor(s.clearColor.r, s.clearColor.g, s.clearColor.b, s.clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void RenderEngine::onMidRender(EngineAPI &)
    {
        if (!IsInitialized() || !mImpl->checkThread())
            return;
        auto &s = *mImpl;
        s.accepting = false;
        if (s.viewportWidth <= 0 || s.viewportHeight <= 0)
            return;
        // Compact stale records in place and cache sort keys. No temporary storage.
        size_t count = 0;
        for (size_t i = 0; i < s.draws.size(); ++i)
        {
            DrawRecord record = s.draws[i];
            const auto &item = record.item;
            auto *material = lookup(s.materials, item.material);
            if (!lookup(s.meshes, item.mesh) || !material || !lookup(s.shaders, material->desc.shader) ||
                (material->desc.texture.isValid() && !lookup(s.textures, material->desc.texture)) ||
                (material->desc.sampler.isValid() && !lookup(s.samplers, material->desc.sampler)))
                continue;
            record.alpha = material->desc.blend == BlendMode::Alpha;
            record.program = material->desc.shader.id;
            record.depth = (s.camera.view * item.transform * glm::vec4(0, 0, 0, 1)).z;
            if (!std::isfinite(record.depth))
                continue;
            record.order = uint32_t(i);
            s.draws[count++] = record;
        }
        s.draws.truncate(count);
        // Introsort uses no heap; submission order is an explicit final key.
        std::sort(s.draws.begin(), s.draws.end(), DrawLess{});
        slang::PerCameraData camera{s.camera.view, s.camera.proj};
        glNamedBufferSubData(s.cameraBuffer.id, 0, sizeof(camera), &camera);
        glBindBufferBase(GL_UNIFORM_BUFFER, slang::PerFrameData::BIND_IDX, s.frameBuffer.id);
        glBindBufferBase(GL_UNIFORM_BUFFER, slang::PerCameraData::BIND_IDX, s.cameraBuffer.id);
        glBindBufferBase(GL_UNIFORM_BUFFER, slang::PerDrawData::BIND_IDX, s.drawBuffer.id);
        glBindBufferBase(GL_UNIFORM_BUFFER, slang::MaterialData::BIND_IDX, s.materialBuffer.id);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(s.viewportX, s.viewportY, s.viewportWidth, s.viewportHeight);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB); // Linear output for now; HDR/tone mapping is a future pass.
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glFrontFace(GL_CCW);
        glCullFace(GL_BACK);
        glDepthFunc(GL_LESS);
        glDepthRange(0.0, 1.0);
        glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
        glBlendEquation(GL_FUNC_ADD);
        GLuint previousProgram = 0, previousVao = 0;
        MaterialHandle previousMaterial;
        for (const auto &record : s.draws)
        {
            const auto &item = record.item;
            auto &mesh = *lookup(s.meshes, item.mesh);
            const auto &material = lookup(s.materials, item.material)->desc;
            GLuint program = lookup(s.shaders, material.shader)->object.id;
            if (program != previousProgram)
            {
                glUseProgram(program);
                previousProgram = program;
            }
            if (mesh.vao.id != previousVao)
            {
                glBindVertexArray(mesh.vao.id);
                // Meshes without COLOR_0 (including the original API) use white.
                glVertexAttrib4f(4, 1, 1, 1, 1);
                previousVao = mesh.vao.id;
            }
            glFrontFace(glm::determinant(glm::mat3(item.transform)) < 0 ? GL_CW : GL_CCW);
            if (item.material != previousMaterial)
            {
                previousMaterial = item.material;
                (material.depthTest ? glEnable : glDisable)(GL_DEPTH_TEST);
                glDepthMask(material.depthWrite && material.blend != BlendMode::Alpha);
                (material.doubleSided ? glDisable : glEnable)(GL_CULL_FACE);
                (material.blend == BlendMode::Alpha ? glEnable : glDisable)(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                auto texture = material.texture.isValid() ? material.texture : s.white;
                auto sampler = material.sampler.isValid() ? material.sampler : s.linear;
                glBindTextureUnit(slang::BIND_ALBEDO, lookup(s.textures, texture)->object.id);
                glBindSampler(slang::BIND_ALBEDO, lookup(s.samplers, sampler)->object.id);
                slang::MaterialData data{material.baseColor, glm::vec4(material.blend == BlendMode::Alpha  ? 2.0f
                                                                       : material.blend == BlendMode::Mask ? 1.0f
                                                                                                           : 0.0f,
                                                                       material.alphaCutoff, 0, 0)};
                glNamedBufferSubData(s.materialBuffer.id, 0, sizeof(data), &data);
            }
            slang::PerDrawData draw{item.transform};
            glNamedBufferSubData(s.drawBuffer.id, 0, sizeof(draw), &draw);
            if (mesh.indexed)
                glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
            else
                glDrawArrays(GL_TRIANGLES, 0, mesh.count);
        }
        glDepthMask(GL_TRUE);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void RenderEngine::onPostRender(EngineAPI &)
    {
        if (!IsInitialized() || !mImpl->checkThread())
            return;
        if (mImpl->width > 0 && mImpl->height > 0 && !SDL_GL_SwapWindow(mImpl->context.window))
            errorf("Swap window: %s", SDL_GetError());
    }
}
