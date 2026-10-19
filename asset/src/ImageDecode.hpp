#pragma once
#include "idk/asset/ModelImporter.hpp"

namespace idk::asset::detail
{
    bool DecodeImage(std::span<const uint8_t> bytes, idk::Arena &output, idk::Arena &scratch, Image &image,
                     ImportError &error);
}
