#include "ImageDecode.hpp"
#include <cstring>
#include <limits>

namespace
{
    thread_local idk::Arena *decoderArena = nullptr;

    void *imageAlloc(size_t size)
    {
        return decoderArena ? decoderArena->alloc(size, alignof(std::max_align_t)) : nullptr;
    }

    void imageFree(void *)
    {
    }

    void *imageRealloc(void *previous, size_t oldSize, size_t size)
    {
        void *memory = imageAlloc(size);
        if (memory && previous)
            std::memcpy(memory, previous, oldSize < size ? oldSize : size);
        return memory;
    }
}

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MALLOC(size) imageAlloc(size)
#define STBI_REALLOC_SIZED(ptr, oldSize, size) imageRealloc(ptr, oldSize, size)
#define STBI_FREE(ptr) imageFree(ptr)
#include <stb_image.h>
#include <cstdio>

namespace idk::asset::detail
{
    bool DecodeImage(std::span<const uint8_t> bytes, idk::Arena &output, idk::Arena &scratch, Image &image,
                     ImportError &error)
    {
        if (bytes.size() > size_t(std::numeric_limits<int>::max()))
        {
            error.code = ImportErrorCode::Decode;
            std::snprintf(error.message, sizeof(error.message), "Encoded image exceeds decoder limit");
            return false;
        }
        idk::ArenaScope temporary(scratch);
        decoderArena = &scratch;
        int width = 0, height = 0, channels = 0;
        auto *pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 4);
        bool ok = pixels && width > 0 && height > 0;
        if (ok)
        {
            image.width = uint32_t(width);
            image.height = uint32_t(height);
            image.rgba = output.array<uint8_t>(size_t(width) * size_t(height) * 4);
            ok = !image.rgba.empty();
            if (ok)
                std::memcpy(image.rgba.data(), pixels, image.rgba.size());
        }
        if (!ok)
        {
            bool oom = scratch.failed() || output.failed();
            error.code = oom ? ImportErrorCode::OutOfMemory : ImportErrorCode::Decode;
            const char *reason = stbi_failure_reason();
            std::snprintf(error.message, sizeof(error.message), "%s",
                          oom      ? "Image arena exhausted"
                          : reason ? reason
                                   : "Image decoding failed");
        }
        decoderArena = nullptr;
        return ok;
    }
}
