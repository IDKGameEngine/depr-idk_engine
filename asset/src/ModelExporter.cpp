#include "idk/asset/ModelExporter.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unistd.h>

namespace idk::asset
{
    namespace
    {
        struct Json
        {
            std::span<char> buffer;
            size_t size = 0;
            bool ok = true;

            void text(std::string_view value)
            {
                if (!ok || value.size() > buffer.size() - size)
                {
                    ok = false;
                    return;
                }
                if (!value.empty())
                    std::memcpy(buffer.data() + size, value.data(), value.size());
                size += value.size();
            }

            void number(uint32_t value)
            {
                char bytes[32];
                auto result = std::to_chars(bytes, bytes + sizeof(bytes), value);
                if (result.ec != std::errc{})
                {
                    ok = false;
                    return;
                }
                text({bytes, size_t(result.ptr - bytes)});
            }

            void number(float value)
            {
                char bytes[64];
                auto result = std::to_chars(bytes, bytes + sizeof(bytes), value, std::chars_format::general,
                                            std::numeric_limits<float>::max_digits10);
                if (result.ec != std::errc{} || !std::isfinite(value))
                {
                    ok = false;
                    return;
                }
                text({bytes, size_t(result.ptr - bytes)});
            }

            void quote(std::string_view value)
            {
                text("\"");
                constexpr char digits[] = "0123456789abcdef";
                for (unsigned char c : value)
                {
                    if (c == '"')
                        text("\\\"");
                    else if (c == '\\')
                        text("\\\\");
                    else if (c < 32)
                    {
                        char escape[] = {'\\', 'u', '0', '0', digits[c >> 4], digits[c & 15]};
                        text({escape, sizeof(escape)});
                    }
                    else
                    {
                        char byte = char(c);
                        text({&byte, 1});
                    }
                }
                text("\"");
            }

            void vector(const float *values, unsigned count)
            {
                text("[");
                for (unsigned i = 0; i < count; ++i)
                {
                    if (i)
                        text(",");
                    number(values[i]);
                }
                text("]");
            }
        };

        struct Geometry
        {
            const Primitive *primitive = nullptr;
            uint32_t vertexOffset = 0, indexOffset = 0, indexCount = 0;
            glm::vec3 minimum{0}, maximum{0};
        };

        struct Group
        {
            std::span<const uint32_t> primitives;
        };

        struct Exporter
        {
            const Model &model;
            const char *path;
            idk::Arena &scratch;
            ExportError &error;
            std::span<Geometry> geometry{};
            std::span<uint32_t> primitiveGeometry{}, nodeGroup{};
            std::span<Group> groups{};
            uint32_t geometryCount = 0, groupCount = 0, binarySize = 0;

            bool fail(ExportErrorCode code, const char *message)
            {
                error.code = code;
                std::snprintf(error.message, sizeof(error.message), "%s", message);
                return false;
            }

            bool memory() { return fail(ExportErrorCode::OutOfMemory, "Export scratch arena exhausted"); }

            bool io(const char *operation)
            {
                error.code = errno == EEXIST ? ExportErrorCode::AlreadyExists : ExportErrorCode::Io;
                std::snprintf(error.message, sizeof(error.message), "%s: %s", operation, std::strerror(errno));
                return false;
            }

            bool validate()
            {
                if (!path || !*path || !model.primitives.size() || !model.nodes.size() ||
                    model.primitives.size() > UINT32_MAX || model.nodes.size() > UINT32_MAX ||
                    model.materials.size() > UINT32_MAX)
                    return fail(ExportErrorCode::InvalidInput, "Export requires a nonempty model and destination path");
                if (!model.images.empty() || !model.textures.empty())
                    return fail(ExportErrorCode::Unsupported, "This exporter supports untextured static models");
                for (const auto &m : model.materials)
                {
                    for (auto slot : {m.baseColorTexture, m.normalTexture, m.metallicRoughnessTexture,
                                      m.occlusionTexture, m.emissiveTexture})
                        if (slot != NoIndex)
                            return fail(ExportErrorCode::Unsupported,
                                        "Textured materials are not supported by this exporter");
                    for (unsigned i = 0; i < 4; ++i)
                        if (!std::isfinite(m.baseColor[i]) || m.baseColor[i] < 0 || m.baseColor[i] > 1)
                            return fail(ExportErrorCode::InvalidInput, "Base colour must be finite and in [0,1]");
                    for (unsigned i = 0; i < 3; ++i)
                        if (!std::isfinite(m.emissive[i]) || m.emissive[i] < 0 || m.emissive[i] > 1)
                            return fail(ExportErrorCode::InvalidInput, "Emissive factor must be in [0,1]");
                    if (!std::isfinite(m.metallic) || !std::isfinite(m.roughness) || m.metallic < 0 || m.metallic > 1 ||
                        m.roughness < 0 || m.roughness > 1 || !std::isfinite(m.alphaCutoff) || m.alphaCutoff < 0)
                        return fail(ExportErrorCode::InvalidInput, "Invalid material factors");
                }
                for (const auto &p : model.primitives)
                {
                    size_t count = p.indices.empty() ? p.vertices.size() : p.indices.size();
                    if (p.vertices.empty() || p.vertices.size() > UINT32_MAX || count > UINT32_MAX || !count ||
                        count % 3 || (p.material != NoIndex && p.material >= model.materials.size()))
                        return fail(ExportErrorCode::InvalidInput, "Invalid primitive counts/material");
                    for (const auto &v : p.vertices)
                    {
                        for (unsigned i = 0; i < 3; ++i)
                            if (!std::isfinite(v.position[i]) || !std::isfinite(v.normal[i]))
                                return fail(ExportErrorCode::InvalidInput, "Non-finite vertex data");
                        if (std::abs(glm::length(v.normal) - 1) > .01f)
                            return fail(ExportErrorCode::InvalidInput, "Normals must be normalized");
                        for (unsigned i = 0; i < 2; ++i)
                            if (!std::isfinite(v.uv[i]))
                                return fail(ExportErrorCode::InvalidInput, "Non-finite UV");
                        for (unsigned i = 0; i < 4; ++i)
                            if (!std::isfinite(v.color[i]) || v.color[i] < 0 || v.color[i] > 1 ||
                                !std::isfinite(v.tangent[i]))
                                return fail(ExportErrorCode::InvalidInput, "Invalid vertex colour/tangent");
                        if (p.hasTangents &&
                            (std::abs(glm::length(glm::vec3(v.tangent)) - 1) > .01f || std::abs(v.tangent.w) != 1))
                            return fail(ExportErrorCode::InvalidInput, "Tangents must be normalized with W +/-1");
                    }
                    for (auto index : p.indices)
                        if (index >= p.vertices.size())
                            return fail(ExportErrorCode::InvalidInput, "Index exceeds vertex count");
                }
                for (size_t i = 0; i < model.nodes.size(); ++i)
                {
                    const auto &node = model.nodes[i];
                    if (node.parent != NoIndex && node.parent >= i)
                        return fail(ExportErrorCode::InvalidInput, "Nodes must be in parent-before-child order");
                    for (auto primitive : node.primitives)
                        if (primitive >= model.primitives.size())
                            return fail(ExportErrorCode::InvalidInput, "Invalid node primitive");
                    const auto &m = node.localTransform;
                    for (unsigned c = 0; c < 4; ++c)
                        for (unsigned r = 0; r < 4; ++r)
                            if (!std::isfinite(m[c][r]))
                                return fail(ExportErrorCode::InvalidInput, "Non-finite node transform");
                    if (std::abs(m[0][3]) > .0001f || std::abs(m[1][3]) > .0001f || std::abs(m[2][3]) > .0001f ||
                        std::abs(m[3][3] - 1) > .0001f)
                        return fail(ExportErrorCode::InvalidInput, "Node matrix must be affine");
                    for (unsigned c = 0; c < 3; ++c)
                        for (unsigned d = c + 1; d < 3; ++d)
                        {
                            float lengths = glm::length(glm::vec3(m[c])) * glm::length(glm::vec3(m[d]));
                            if (lengths > .000001f &&
                                std::abs(glm::dot(glm::vec3(m[c]), glm::vec3(m[d]))) / lengths > .001f)
                                return fail(ExportErrorCode::Unsupported, "Sheared node transforms are not supported");
                        }
                }
                return true;
            }

            bool prepare()
            {
                geometry = scratch.array<Geometry>(model.primitives.size());
                primitiveGeometry = scratch.array<uint32_t>(model.primitives.size());
                groups = scratch.array<Group>(model.nodes.size());
                nodeGroup = scratch.array<uint32_t>(model.nodes.size());
                if (scratch.failed())
                    return memory();
                for (size_t i = 0; i < model.primitives.size(); ++i)
                {
                    const auto &p = model.primitives[i];
                    uint32_t found = 0;
                    for (; found < geometryCount; ++found)
                    {
                        const auto &other = *geometry[found].primitive;
                        if (p.vertices.data() == other.vertices.data() && p.vertices.size() == other.vertices.size() &&
                            p.indices.data() == other.indices.data() && p.indices.size() == other.indices.size())
                            break;
                    }
                    primitiveGeometry[i] = found;
                    if (found < geometryCount)
                        continue;
                    auto &g = geometry[geometryCount++];
                    g.primitive = &p;
                    uint64_t bytes = uint64_t(p.vertices.size()) * 64;
                    uint64_t indexBytes = uint64_t(p.indices.empty() ? p.vertices.size() : p.indices.size()) * 4;
                    if (uint64_t(binarySize) + bytes + indexBytes > UINT32_MAX)
                        return fail(ExportErrorCode::Unsupported, "GLB exceeds 32-bit size limit");
                    g.vertexOffset = binarySize;
                    g.indexOffset = binarySize + uint32_t(bytes);
                    g.indexCount = uint32_t(indexBytes / 4);
                    binarySize += uint32_t(bytes + indexBytes);
                    g.minimum = g.maximum = p.vertices[0].position;
                    for (const auto &v : p.vertices)
                    {
                        g.minimum = glm::min(g.minimum, v.position);
                        g.maximum = glm::max(g.maximum, v.position);
                    }
                }
                for (size_t i = 0; i < model.nodes.size(); ++i)
                {
                    const auto &refs = model.nodes[i].primitives;
                    nodeGroup[i] = NoIndex;
                    if (refs.empty())
                        continue;
                    uint32_t found = 0;
                    for (; found < groupCount; ++found)
                    {
                        if (refs.size() != groups[found].primitives.size())
                            continue;
                        if (std::equal(refs.begin(), refs.end(), groups[found].primitives.begin()))
                            break;
                    }
                    nodeGroup[i] = found;
                    if (found == groupCount)
                        groups[groupCount++].primitives = refs;
                }
                if (!groupCount)
                    return fail(ExportErrorCode::InvalidInput, "Scene contains no mesh instances");
                return true;
            }

            void writeScene(Json &writer)
            {
                writer.text("{\"asset\":{\"version\":\"2.0\",\"generator\":\"idk shape "
                            "editor\"},\"scene\":0,\"scenes\":[{\"nodes\":[");
                bool comma = false;
                for (uint32_t i = 0; i < model.nodes.size(); ++i)
                    if (model.nodes[i].parent == NoIndex)
                    {
                        if (comma)
                            writer.text(",");
                        writer.number(i);
                        comma = true;
                    }
                writer.text("]}]");
            }

            void writeBuffers(Json &writer)
            {
                writer.text(",\"buffers\":[{\"byteLength\":");
                writer.number(binarySize);
                writer.text("}],\"bufferViews\":[");
                for (uint32_t i = 0; i < geometryCount; ++i)
                {
                    if (i)
                        writer.text(",");
                    const auto &g = geometry[i];
                    writer.text("{\"buffer\":0,\"byteOffset\":");
                    writer.number(g.vertexOffset);
                    writer.text(",\"byteLength\":");
                    writer.number(g.indexOffset - g.vertexOffset);
                    writer.text(",\"byteStride\":64,\"target\":34962},{\"buffer\":0,\"byteOffset\":");
                    writer.number(g.indexOffset);
                    writer.text(",\"byteLength\":");
                    writer.number(g.indexCount * 4);
                    writer.text(",\"target\":34963}");
                }
                writer.text("]");
            }

            void writeAccessors(Json &writer)
            {
                writer.text(",\"accessors\":[");
                constexpr unsigned components[] = {3, 3, 2, 4, 4};
                constexpr uint32_t offsets[] = {0, 12, 24, 32, 48};
                constexpr const char *types[] = {"VEC3", "VEC3", "VEC2", "VEC4", "VEC4"};
                for (uint32_t i = 0; i < geometryCount; ++i)
                {
                    const auto &g = geometry[i];
                    for (unsigned stream = 0; stream < 6; ++stream)
                    {
                        if (i || stream)
                            writer.text(",");
                        writer.text("{\"bufferView\":");
                        writer.number(i * 2 + uint32_t(stream == 5));
                        writer.text(",\"componentType\":");
                        writer.number(stream == 5 ? 5125u : 5126u);
                        writer.text(",\"count\":");
                        writer.number(stream == 5 ? g.indexCount : uint32_t(g.primitive->vertices.size()));
                        writer.text(",\"type\":");
                        writer.quote(stream == 5 ? "SCALAR" : types[stream]);
                        if (stream != 5)
                        {
                            writer.text(",\"byteOffset\":");
                            writer.number(offsets[stream]);
                        }
                        if (stream == 0)
                        {
                            writer.text(",\"min\":");
                            writer.vector(&g.minimum.x, components[stream]);
                            writer.text(",\"max\":");
                            writer.vector(&g.maximum.x, 3);
                        }
                        writer.text("}");
                    }
                }
                writer.text("]");
            }

            void writeMeshes(Json &writer)
            {
                writer.text(",\"meshes\":[");
                for (uint32_t i = 0; i < groupCount; ++i)
                {
                    if (i)
                        writer.text(",");
                    writer.text("{\"primitives\":[");
                    for (size_t k = 0; k < groups[i].primitives.size(); ++k)
                    {
                        if (k)
                            writer.text(",");
                        uint32_t p = groups[i].primitives[k], base = primitiveGeometry[p] * 6;
                        writer.text("{\"attributes\":{\"POSITION\":");
                        writer.number(base);
                        writer.text(",\"NORMAL\":");
                        writer.number(base + 1);
                        writer.text(",\"TEXCOORD_0\":");
                        writer.number(base + 2);
                        writer.text(",\"COLOR_0\":");
                        writer.number(base + 4);
                        if (model.primitives[p].hasTangents)
                        {
                            writer.text(",\"TANGENT\":");
                            writer.number(base + 3);
                        }
                        writer.text("},\"indices\":");
                        writer.number(base + 5);
                        writer.text(",\"mode\":4");
                        if (model.primitives[p].material != NoIndex)
                        {
                            writer.text(",\"material\":");
                            writer.number(model.primitives[p].material);
                        }
                        writer.text("}");
                    }
                    writer.text("]}");
                }
                writer.text("]");
            }

            void writeNodes(Json &writer)
            {
                writer.text(",\"nodes\":[");
                for (uint32_t i = 0; i < model.nodes.size(); ++i)
                {
                    if (i)
                        writer.text(",");
                    const auto &n = model.nodes[i];
                    writer.text("{\"name\":");
                    writer.quote(n.name);
                    writer.text(",\"matrix\":");
                    writer.vector(&n.localTransform[0][0], 16);
                    if (nodeGroup[i] != NoIndex)
                    {
                        writer.text(",\"mesh\":");
                        writer.number(nodeGroup[i]);
                    }
                    bool first = true;
                    for (uint32_t child = i + 1; child < model.nodes.size(); ++child)
                        if (model.nodes[child].parent == i)
                        {
                            writer.text(first ? ",\"children\":[" : ",");
                            writer.number(child);
                            first = false;
                        }
                    if (!first)
                        writer.text("]");
                    writer.text("}");
                }
                writer.text("]");
            }

            void writeMaterials(Json &writer)
            {
                if (!model.materials.empty())
                    writer.text(",\"materials\":[");
                for (size_t i = 0; i < model.materials.size(); ++i)
                {
                    if (i)
                        writer.text(",");
                    const auto &m = model.materials[i];
                    writer.text("{\"name\":");
                    writer.quote(m.name);
                    writer.text(",\"pbrMetallicRoughness\":{\"baseColorFactor\":");
                    writer.vector(&m.baseColor.x, 4);
                    writer.text(",\"metallicFactor\":");
                    writer.number(m.metallic);
                    writer.text(",\"roughnessFactor\":");
                    writer.number(m.roughness);
                    writer.text("},\"emissiveFactor\":");
                    writer.vector(&m.emissive.x, 3);
                    writer.text(",\"alphaMode\":");
                    writer.quote(m.alphaMode == AlphaMode::Blend  ? "BLEND"
                                 : m.alphaMode == AlphaMode::Mask ? "MASK"
                                                                  : "OPAQUE");
                    writer.text(",\"alphaCutoff\":");
                    writer.number(m.alphaCutoff);
                    writer.text(",\"doubleSided\":");
                    writer.text(m.doubleSided ? "true" : "false");
                    writer.text("}");
                }
                if (!model.materials.empty())
                    writer.text("]");
            }

            void writeJson(Json &writer)
            {
                writeScene(writer);
                writeBuffers(writer);
                writeAccessors(writer);
                writeMeshes(writer);
                writeNodes(writer);
                writeMaterials(writer);
                writer.text("}");
            }

            static bool u32(FILE *file, uint32_t value)
            {
                uint8_t bytes[] = {uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
                return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
            }

            static bool scalar(FILE *file, float value) { return u32(file, std::bit_cast<uint32_t>(value)); }

            bool write(FILE *file, const Json &writer)
            {
                static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
                uint32_t jsonSize = uint32_t((writer.size + 3) & ~size_t(3));
                uint64_t total = 12ull + 8 + jsonSize + 8 + binarySize;
                if (total > UINT32_MAX)
                    return fail(ExportErrorCode::Unsupported, "GLB exceeds 32-bit size limit");
                if (!u32(file, 0x46546c67) || !u32(file, 2) || !u32(file, uint32_t(total)) || !u32(file, jsonSize) ||
                    !u32(file, 0x4e4f534a))
                    return false;
                if (std::fwrite(writer.buffer.data(), 1, writer.size, file) != writer.size)
                    return false;
                for (size_t i = writer.size; i < jsonSize; ++i)
                    if (std::fputc(' ', file) == EOF)
                        return false;
                if (!u32(file, binarySize) || !u32(file, 0x004e4942))
                    return false;
                for (uint32_t i = 0; i < geometryCount; ++i)
                {
                    const auto &g = geometry[i];
                    const auto &p = *g.primitive;
                    for (const auto &v : p.vertices)
                    {
                        for (float value : {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y,
                                            v.normal.z, v.uv.x, v.uv.y, v.tangent.x, v.tangent.y, v.tangent.z,
                                            v.tangent.w, v.color.x, v.color.y, v.color.z, v.color.w})
                            if (!scalar(file, value))
                                return false;
                    }
                    for (uint32_t k = 0; k < g.indexCount; ++k)
                        if (!u32(file, p.indices.empty() ? k : p.indices[k]))
                            return false;
                }
                return true;
            }

            bool run(bool overwrite)
            {
                if (!validate() || !prepare())
                    return false;
                size_t available = scratch.capacity() - scratch.mark();
                auto *bytes = static_cast<char *>(scratch.alloc(available, 1));
                if (!bytes || !available)
                    return memory();
                Json writer{{bytes, available}};
                writeJson(writer);
                if (!writer.ok || writer.size > UINT32_MAX - 3)
                    return memory();
                char temporary[1200];
                FILE *file = nullptr;
                static std::atomic<uint32_t> sequence{0};
                for (unsigned attempt = 0; attempt < 16 && !file; ++attempt)
                {
                    int size = std::snprintf(temporary, sizeof(temporary), "%s.idk-%u-%u.tmp", path, unsigned(getpid()),
                                             sequence.fetch_add(1));
                    if (size < 0 || size_t(size) >= sizeof(temporary))
                        return fail(ExportErrorCode::InvalidInput, "Export path is too long");
                    file = std::fopen(temporary, "wbx");
                    if (!file && errno != EEXIST)
                        return io("Opening temporary export");
                }
                if (!file)
                    return io("Opening temporary export");
                char buffer[8192];
                errno = 0;
                bool ok = std::setvbuf(file, buffer, _IOFBF, sizeof(buffer)) == 0 && write(file, writer);
                int writeError = ok ? 0 : (errno ? errno : EIO);
                if (std::fclose(file) != 0)
                {
                    ok = false;
                    writeError = errno ? errno : EIO;
                }
                if (!ok)
                {
                    unlink(temporary);
                    errno = writeError;
                    return error.code != ExportErrorCode::None ? false : io("Writing GLB");
                }
                // Same-directory publish keeps the previous file intact on failure.
                int result = overwrite ? std::rename(temporary, path) : link(temporary, path);
                if (result != 0)
                {
                    int saved = errno;
                    unlink(temporary);
                    errno = saved;
                    return io("Publishing GLB");
                }
                if (!overwrite)
                    unlink(temporary);
                return true;
            }
        };
    }

    bool ExportModelGlb(const Model &model, const char *path, idk::Arena &scratch, ExportError &error, bool overwrite)
    {
        error = {};
        idk::ArenaScope temporary(scratch);
        Exporter exporter{model, path, scratch, error};
        return exporter.run(overwrite);
    }
}
