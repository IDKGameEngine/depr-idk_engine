#pragma once

#include "RenderTypes.hpp"
#include "libidk/Service.hpp"

namespace idk::asset
{
    struct Model;
}

namespace idk::gfx
{
    struct RenderLimits
    {
        static constexpr uint32_t MaxDraws = 8192;
        static constexpr uint32_t MaxModels = 32;
        static constexpr uint32_t MaxModelPrimitives = 128;
        static constexpr uint32_t MaxModelMaterials = 128;
        static constexpr uint32_t MaxModelImages = 128;
        static constexpr uint32_t MaxModelSamplers = 64;
        static constexpr uint32_t MaxModelInstances = 512;
        static constexpr uint32_t MaxModelNodes = 512;
    };

    // One active renderer backed by bounded static storage. No heap ownership.
    // One owning GL thread. Submit during application onPreRender, after the
    // renderer clears its queue. Scene execution precedes editor onMidRender.
    class RenderEngine final: public idk::Service
    {
    public:
        RenderEngine();
        ~RenderEngine() override;
        void onInit(EngineAPI &) override;
        void onShutdown(EngineAPI &) override;
        void onPreRender(EngineAPI &) override;
        void onMidRender(EngineAPI &) override;
        void onPostRender(EngineAPI &) override;

        bool IsInitialized() const;

        MeshHandle CreateMesh(const MeshDesc &);
        TextureHandle CreateTexture(const TextureDesc &);
        SamplerHandle CreateSampler(const SamplerDesc &);
        ShaderHandle CreateShader(const ShaderDesc &);
        // Failure preserves the old program.
        bool ReloadShader(ShaderHandle, const ShaderDesc &);
        MaterialHandle CreateMaterial(const MaterialDesc &);
        void DestroyMesh(MeshHandle);
        void DestroyTexture(TextureHandle);
        void DestroySampler(SamplerHandle);
        void DestroyShader(ShaderHandle);
        void DestroyMaterial(MaterialHandle);

        // Transactional upload: failure releases every partially created resource.
        ModelHandle UploadModel(const idk::asset::Model &);
        void DestroyModel(ModelHandle);
        bool SubmitModel(ModelHandle, const glm::mat4 &transform = glm::mat4(1.0f));

        bool Submit(const DrawItem &);
        void SetCamera(const Camera &);
        void SetClearColor(glm::vec4);
        // Frame-local rectangle in drawable pixels, with OpenGL bottom-left origin.
        // Resets to the full window in onPreRender. An empty rectangle hides the scene.
        void SetViewport(int x, int y, int width, int height);
        ShaderHandle GetDefaultShader() const;
        void *GetWindowHandle() const;
        void *GetContextHandle() const;

    private:
        struct Impl;
        Impl *mImpl = nullptr;
        void release();
    };
}
