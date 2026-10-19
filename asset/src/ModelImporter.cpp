#include "idk/asset/ModelImporter.hpp"
#include "ImageDecode.hpp"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace idk::asset
{
    namespace
    {
        void *parseAlloc(void *user, cgltf_size size)
        {
            return static_cast<idk::Arena *>(user)->alloc(size, alignof(std::max_align_t));
        }

        void parseFree(void *, void *)
        {
        }

        struct Importer
        {
            const char *path;
            idk::Arena &output;
            idk::Arena &scratch;
            Model &model;
            ImportError &error;
            cgltf_data *data = nullptr;
            cgltf_options options{};
            size_t dependencyCount = 0;
            std::span<uint32_t> meshOffsets{};
            std::span<uint8_t> visited{};
            size_t nodeCount = 0;

            bool fail(ImportErrorCode code, const char *message, const char *detail = "")
            {
                error.code = code;
                std::snprintf(error.message, sizeof(error.message), "%s: %s%s", path, message, detail);
                return false;
            }

            bool memory() { return fail(ImportErrorCode::OutOfMemory, "Asset output/scratch arena exhausted"); }

            std::string_view text(const char *value, bool jsonEscaped = false)
            {
                if (!value || !*value)
                    return {};
                size_t length = std::strlen(value);
                auto bytes = output.array<char>(length + 1);
                if (bytes.empty())
                    return {};
                std::memcpy(bytes.data(), value, length + 1);
                if (jsonEscaped)
                    length = cgltf_decode_string(bytes.data());
                return {bytes.data(), length};
            }

            bool dependency(const char *value)
            {
                for (size_t i = 0; i < dependencyCount; ++i)
                    if (model.dependencies[i] == value)
                        return true;
                if (dependencyCount == model.dependencies.size())
                    return memory();
                model.dependencies[dependencyCount++] = text(value);
                if (output.failed())
                    return memory();
                return true;
            }

            static int hex(char c)
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                return -1;
            }

            bool filePath(const char *uri, char (&destination)[1024])
            {
                if (!uri || std::strchr(uri, ':'))
                    return fail(ImportErrorCode::Unsupported, "Expected a local file URI");
                size_t used = 0;
                const char *slash = std::strrchr(path, '/');
                if (*uri != '/' && slash)
                {
                    used = size_t(slash - path + 1);
                    if (used >= sizeof(destination))
                        return fail(ImportErrorCode::InvalidInput, "Path exceeds 1023 bytes");
                    std::memcpy(destination, path, used);
                }
                for (size_t i = 0; uri[i]; ++i)
                {
                    char value = uri[i];
                    if (value == '%')
                    {
                        if (!uri[i + 1] || !uri[i + 2] || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0)
                            return fail(ImportErrorCode::InvalidInput, "Invalid encoded URI");
                        value = char(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
                        i += 2;
                        if (!value)
                            return fail(ImportErrorCode::InvalidInput, "NUL in file URI");
                    }
                    if (used + 1 >= sizeof(destination))
                        return fail(ImportErrorCode::InvalidInput, "Path exceeds 1023 bytes");
                    destination[used++] = value;
                }
                destination[used] = 0;
                return true;
            }

            bool read(const char *file, std::span<const uint8_t> &bytes)
            {
                FILE *stream = std::fopen(file, "rb");
                if (!stream)
                    return fail(ImportErrorCode::Io, "Cannot open ", file);
                bool ok = std::fseek(stream, 0, SEEK_END) == 0;
                long length = ok ? std::ftell(stream) : -1;
                ok = length > 0 && std::fseek(stream, 0, SEEK_SET) == 0;
                auto storage = ok ? scratch.array<uint8_t>(size_t(length)) : std::span<uint8_t>{};
                if (storage.empty())
                    ok = false;
                if (ok)
                    ok = std::fread(storage.data(), 1, storage.size(), stream) == storage.size();
                std::fclose(stream);
                if (!ok)
                    return scratch.failed() ? memory() : fail(ImportErrorCode::Io, "Cannot read ", file);
                bytes = storage;
                return true;
            }

            bool view(const cgltf_texture_view &value)
            {
                if (value.has_transform)
                    return fail(ImportErrorCode::Unsupported, "Texture transforms are not supported");
                if (value.texture && value.texcoord != 0)
                    return fail(ImportErrorCode::Unsupported, "Only TEXCOORD_0 is supported");
                return true;
            }

            template <class T>
            bool coreOnly(const T &object)
            {
                if (object.extensions_count)
                    return fail(ImportErrorCode::Unsupported, "Unsupported extension payload");
                return true;
            }

            bool profile()
            {
                if (!data->asset.version || std::strcmp(data->asset.version, "2.0") ||
                    (data->asset.min_version && std::strcmp(data->asset.min_version, "2.0")))
                    return fail(ImportErrorCode::Unsupported, "Only glTF 2.0 is supported");
                if (data->extensions_required_count)
                    return fail(ImportErrorCode::Unsupported,
                                "Unsupported required extension: ", data->extensions_required[0]);
                if (data->animations_count || data->skins_count)
                    return fail(ImportErrorCode::Unsupported, "Animation and skins are outside the static profile");
                if (data->lights_count || data->variants_count)
                    return fail(ImportErrorCode::Unsupported, "Light/material-variant extensions are not supported");
                if (data->data_extensions_count)
                    return fail(ImportErrorCode::Unsupported, "Unsupported root extension payload");
                if (!coreOnly(data->asset))
                    return false;
                for (size_t i = 0; i < data->buffer_views_count; ++i)
                {
                    if (!coreOnly(data->buffer_views[i]))
                        return false;
                    if (data->buffer_views[i].has_meshopt_compression)
                        return fail(ImportErrorCode::Unsupported, "Meshopt compression is not supported");
                }
                for (size_t i = 0; i < data->accessors_count; ++i)
                {
                    if (!coreOnly(data->accessors[i]))
                        return false;
                }
                for (size_t i = 0; i < data->images_count; ++i)
                {
                    if (!coreOnly(data->images[i]))
                        return false;
                }
                for (size_t i = 0; i < data->samplers_count; ++i)
                {
                    const auto &sampler = data->samplers[i];
                    if (!coreOnly(sampler))
                        return false;
                    if (!validWrap(sampler.wrap_s) || !validWrap(sampler.wrap_t))
                        return fail(ImportErrorCode::InvalidInput, "Invalid sampler wrap mode");
                }
                for (size_t i = 0; i < data->textures_count; ++i)
                {
                    const auto &t = data->textures[i];
                    if (!coreOnly(t))
                        return false;
                    if (t.has_basisu || t.has_webp || !t.image)
                        return fail(ImportErrorCode::Unsupported, "Only core PNG/JPEG textures are supported");
                }
                for (size_t i = 0; i < data->materials_count; ++i)
                {
                    const auto &m = data->materials[i];
                    if (!coreOnly(m))
                        return false;
                    if (m.has_pbr_specular_glossiness || m.has_clearcoat || m.has_transmission || m.has_volume ||
                        m.has_ior || m.has_specular || m.has_sheen || m.has_emissive_strength || m.has_iridescence ||
                        m.has_diffuse_transmission || m.has_anisotropy || m.has_dispersion || m.unlit)
                        return fail(ImportErrorCode::Unsupported, "Material extensions are not supported");
                    if (!view(m.pbr_metallic_roughness.base_color_texture) ||
                        !view(m.pbr_metallic_roughness.metallic_roughness_texture) || !view(m.normal_texture) ||
                        !view(m.occlusion_texture) || !view(m.emissive_texture))
                        return false;
                }
                for (size_t i = 0; i < data->meshes_count; ++i)
                {
                    if (!coreOnly(data->meshes[i]))
                        return false;
                    for (size_t j = 0; j < data->meshes[i].primitives_count; ++j)
                    {
                        const auto &p = data->meshes[i].primitives[j];
                        if (!coreOnly(p))
                            return false;
                        if (p.type != cgltf_primitive_type_triangles || p.targets_count ||
                            p.has_draco_mesh_compression || p.mappings_count)
                            return fail(ImportErrorCode::Unsupported,
                                        "Only static core triangle-list primitives are supported");
                        if (p.indices && p.indices->is_sparse)
                            return fail(ImportErrorCode::Unsupported, "Sparse index accessors are not supported");
                    }
                }
                for (size_t i = 0; i < data->nodes_count; ++i)
                {
                    if (!coreOnly(data->nodes[i]))
                        return false;
                    if (data->nodes[i].has_mesh_gpu_instancing)
                        return fail(ImportErrorCode::Unsupported, "Instancing extension is not supported");
                }
                for (size_t i = 0; i < data->scenes_count; ++i)
                {
                    if (!coreOnly(data->scenes[i]))
                        return false;
                }
                return true;
            }

            bool images()
            {
                model.images = output.array<Image>(data->images_count);
                if (output.failed())
                    return memory();
                for (size_t i = 0; i < data->images_count; ++i)
                {
                    const auto &source = data->images[i];
                    auto &image = model.images[i];
                    idk::ArenaScope temporary(scratch);
                    std::span<const uint8_t> bytes;
                    if (source.buffer_view)
                    {
                        const uint8_t *content = cgltf_buffer_view_data(source.buffer_view);
                        if (!content)
                            return fail(ImportErrorCode::InvalidInput, "Invalid embedded image buffer");
                        bytes = {content, source.buffer_view->size};
                        image.source = text(source.name ? source.name : "embedded image", true);
                    }
                    else if (source.uri && !std::strncmp(source.uri, "data:", 5))
                    {
                        const char *comma = std::strchr(source.uri, ',');
                        if (!comma || comma - source.uri < 7 || std::strncmp(comma - 7, ";base64", 7))
                            return fail(ImportErrorCode::Unsupported, "Expected a base64 image URI");
                        const char *encoded = comma + 1;
                        size_t count = std::strlen(encoded);
                        if (!count || count % 4)
                            return fail(ImportErrorCode::InvalidInput, "Invalid base64 image length");
                        size_t size = count / 4 * 3 - (encoded[count - 1] == '=') - (encoded[count - 2] == '=');
                        void *decoded = nullptr;
                        if (cgltf_load_buffer_base64(&options, size, encoded, &decoded) != cgltf_result_success)
                            return scratch.failed() ? memory() : fail(ImportErrorCode::Decode, "Invalid base64 image");
                        bytes = {static_cast<const uint8_t *>(decoded), size};
                        image.source = text(source.name ? source.name : "data image", true);
                    }
                    else
                    {
                        char file[1024];
                        if (!filePath(source.uri, file) || !dependency(file) || !read(file, bytes))
                            return false;
                        image.source = text(file);
                    }
                    if (output.failed())
                        return memory();
                    if (!detail::DecodeImage(bytes, output, scratch, image, error))
                        return false;
                }
                return true;
            }

            static bool validWrap(cgltf_wrap_mode value)
            {
                return value == cgltf_wrap_mode_repeat || value == cgltf_wrap_mode_clamp_to_edge ||
                       value == cgltf_wrap_mode_mirrored_repeat;
            }

            static Wrap wrap(cgltf_wrap_mode value)
            {
                return value == cgltf_wrap_mode_clamp_to_edge     ? Wrap::ClampToEdge
                       : value == cgltf_wrap_mode_mirrored_repeat ? Wrap::MirroredRepeat
                                                                  : Wrap::Repeat;
            }

            bool materials()
            {
                bool defaultSampler = false, defaultMaterial = false;
                for (size_t i = 0; i < data->textures_count; ++i)
                    defaultSampler |= data->textures[i].sampler == nullptr;
                for (size_t i = 0; i < data->meshes_count; ++i)
                    for (size_t j = 0; j < data->meshes[i].primitives_count; ++j)
                        defaultMaterial |= data->meshes[i].primitives[j].material == nullptr;
                model.samplers = output.array<Sampler>(data->samplers_count + size_t(defaultSampler));
                model.textures = output.array<Texture>(data->textures_count);
                model.materials = output.array<Material>(data->materials_count + size_t(defaultMaterial));
                if (output.failed())
                    return memory();
                for (size_t i = 0; i < data->samplers_count; ++i)
                {
                    const auto &s = data->samplers[i];
                    auto &d = model.samplers[i];
                    d.wrapS = wrap(s.wrap_s);
                    d.wrapT = wrap(s.wrap_t);
                    if (s.mag_filter && s.mag_filter != cgltf_filter_type_nearest &&
                        s.mag_filter != cgltf_filter_type_linear)
                        return fail(ImportErrorCode::InvalidInput, "Invalid magnification filter");
                    d.mag = s.mag_filter == cgltf_filter_type_nearest ? Filter::Nearest : Filter::Linear;
                    switch (s.min_filter)
                    {
                        case cgltf_filter_type_nearest:
                            d.min = MinFilter::Nearest;
                            break;
                        case cgltf_filter_type_linear:
                            d.min = MinFilter::Linear;
                            break;
                        case cgltf_filter_type_nearest_mipmap_nearest:
                            d.min = MinFilter::NearestMipmapNearest;
                            break;
                        case cgltf_filter_type_linear_mipmap_nearest:
                            d.min = MinFilter::LinearMipmapNearest;
                            break;
                        case cgltf_filter_type_nearest_mipmap_linear:
                            d.min = MinFilter::NearestMipmapLinear;
                            break;
                        case cgltf_filter_type_linear_mipmap_linear:
                        case cgltf_filter_type_undefined:
                            d.min = MinFilter::LinearMipmapLinear;
                            break;
                        default:
                            return fail(ImportErrorCode::InvalidInput, "Invalid minification filter");
                    }
                }
                for (size_t i = 0; i < data->textures_count; ++i)
                {
                    auto &d = model.textures[i];
                    const auto &s = data->textures[i];
                    d.image = uint32_t(s.image - data->images);
                    d.sampler = s.sampler ? uint32_t(s.sampler - data->samplers) : uint32_t(data->samplers_count);
                }
                for (size_t i = 0; i < data->materials_count; ++i)
                {
                    auto &d = model.materials[i];
                    const auto &s = data->materials[i];
                    const auto &p = s.pbr_metallic_roughness;
                    d.name = text(s.name, true);
                    if (s.has_pbr_metallic_roughness)
                    {
                        d.baseColor = glm::make_vec4(p.base_color_factor);
                        d.metallic = p.metallic_factor;
                        d.roughness = p.roughness_factor;
                    }
                    d.emissive = glm::make_vec3(s.emissive_factor);
                    if (s.normal_texture.texture)
                        d.normalScale = s.normal_texture.scale;
                    if (s.occlusion_texture.texture)
                        d.occlusionStrength = s.occlusion_texture.scale;
                    d.baseColorTexture = textureIndex(p.base_color_texture);
                    d.metallicRoughnessTexture = textureIndex(p.metallic_roughness_texture);
                    d.normalTexture = textureIndex(s.normal_texture);
                    d.occlusionTexture = textureIndex(s.occlusion_texture);
                    d.emissiveTexture = textureIndex(s.emissive_texture);
                    d.alphaMode = s.alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Mask
                                  : s.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                                           : AlphaMode::Opaque;
                    d.alphaCutoff = s.alpha_cutoff;
                    d.doubleSided = s.double_sided;
                }
                if (output.failed())
                    return memory();
                return true;
            }

            uint32_t textureIndex(const cgltf_texture_view &value) const
            {
                return value.texture ? uint32_t(value.texture - data->textures) : NoIndex;
            }

            bool attribute(const cgltf_attribute &attr, std::span<Vertex> vertices)
            {
                const auto *source = attr.data;
                if (source->count != vertices.size())
                    return fail(ImportErrorCode::InvalidInput, "Mismatched vertex stream lengths");
                unsigned components = unsigned(cgltf_num_components(source->type));
                if ((attr.type == cgltf_attribute_type_position || attr.type == cgltf_attribute_type_normal) &&
                    components != 3)
                    return fail(ImportErrorCode::InvalidInput, "Position/normal must have three components");
                if (attr.type == cgltf_attribute_type_texcoord && components != 2)
                    return fail(ImportErrorCode::InvalidInput, "UV must have two components");
                if (attr.type == cgltf_attribute_type_tangent && components != 4)
                    return fail(ImportErrorCode::InvalidInput, "Tangent must have four components");
                if (attr.type == cgltf_attribute_type_color && components != 3 && components != 4)
                    return fail(ImportErrorCode::InvalidInput, "Colour must have three or four components");
                if (components > 4 || vertices.size() > std::numeric_limits<size_t>::max() / components)
                    return memory();
                idk::ArenaScope temporary(scratch);
                auto floats = scratch.array<float>(vertices.size() * components);
                if (scratch.failed())
                    return memory();
                if (cgltf_accessor_unpack_floats(source, floats.data(), floats.size()) != floats.size())
                    return fail(ImportErrorCode::InvalidInput, "Cannot unpack vertex stream");
                for (size_t i = 0; i < vertices.size(); ++i)
                {
                    const float *f = floats.data() + i * components;
                    for (unsigned c = 0; c < components; ++c)
                        if (!std::isfinite(f[c]))
                            return fail(ImportErrorCode::InvalidInput, "Non-finite vertex attribute");
                    auto &v = vertices[i];
                    switch (attr.type)
                    {
                        case cgltf_attribute_type_position:
                            v.position = glm::make_vec3(f);
                            break;
                        case cgltf_attribute_type_normal:
                            v.normal = glm::make_vec3(f);
                            break;
                        case cgltf_attribute_type_texcoord:
                            v.uv = glm::make_vec2(f);
                            break;
                        case cgltf_attribute_type_tangent:
                            v.tangent = glm::make_vec4(f);
                            break;
                        case cgltf_attribute_type_color:
                            v.color = glm::vec4(f[0], f[1], f[2], components == 4 ? f[3] : 1);
                            break;
                        default:
                            break;
                    }
                }
                return true;
            }

            bool primitive(const cgltf_primitive &source, Primitive &result)
            {
                const cgltf_accessor *positions = cgltf_find_accessor(&source, cgltf_attribute_type_position, 0);
                if (!positions || !positions->count || positions->count > NoIndex)
                    return fail(ImportErrorCode::InvalidInput, "Missing/oversized vertex positions");
                idk::ArenaScope temporary(scratch);
                auto vertices = scratch.array<Vertex>(positions->count);
                if (scratch.failed())
                    return memory();
                bool normals = false;
                for (size_t i = 0; i < source.attributes_count; ++i)
                {
                    const auto &a = source.attributes[i];
                    if (a.index != 0 || a.type == cgltf_attribute_type_custom)
                        continue;
                    if (a.type == cgltf_attribute_type_joints || a.type == cgltf_attribute_type_weights)
                        return fail(ImportErrorCode::Unsupported, "Skin vertex attributes are not supported");
                    if (!attribute(a, vertices))
                        return false;
                    normals |= a.type == cgltf_attribute_type_normal;
                    result.hasTangents |= a.type == cgltf_attribute_type_tangent;
                }
                size_t count = source.indices ? source.indices->count : vertices.size();
                if (!count || count % 3 || count > NoIndex)
                    return fail(ImportErrorCode::InvalidInput, "Invalid triangle index count");
                result.indices = output.array<uint32_t>(count);
                result.vertices = output.array<Vertex>(normals ? vertices.size() : count);
                if (output.failed())
                    return memory();
                if (source.indices)
                {
                    if (cgltf_accessor_unpack_indices(source.indices, result.indices.data(), sizeof(uint32_t), count) !=
                        count)
                        return fail(ImportErrorCode::InvalidInput, "Cannot unpack triangle indices");
                }
                else
                    for (size_t i = 0; i < count; ++i)
                        result.indices[i] = uint32_t(i);
                for (auto index : result.indices)
                    if (index >= vertices.size())
                        return fail(ImportErrorCode::InvalidInput, "Index exceeds vertex count");
                if (normals)
                    std::memcpy(result.vertices.data(), vertices.data(), vertices.size_bytes());
                else
                {
                    for (size_t i = 0; i < count; i += 3)
                    {
                        glm::vec3 a = vertices[result.indices[i]].position,
                                  b = vertices[result.indices[i + 1]].position,
                                  c = vertices[result.indices[i + 2]].position;
                        glm::vec3 normal = glm::cross(b - a, c - a);
                        float length = glm::length(normal);
                        normal = length > 0 ? normal / length : glm::vec3(0, 0, 1);
                        for (size_t j = 0; j < 3; ++j)
                        {
                            result.vertices[i + j] = vertices[result.indices[i + j]];
                            result.vertices[i + j].normal = normal;
                            result.indices[i + j] = uint32_t(i + j);
                        }
                    }
                }
                result.material =
                    source.material ? uint32_t(source.material - data->materials) : uint32_t(data->materials_count);
                return true;
            }

            bool node(const cgltf_node &source, uint32_t parent, unsigned depth)
            {
                if (depth > 128)
                    return fail(ImportErrorCode::Unsupported, "Scene exceeds 128 levels");
                size_t sourceIndex = size_t(&source - data->nodes);
                if (visited[sourceIndex])
                    return fail(ImportErrorCode::InvalidInput, "Cyclic/duplicate node in scene");
                visited[sourceIndex] = 1;
                if (nodeCount == model.nodes.size())
                    return memory();
                uint32_t index = uint32_t(nodeCount++);
                auto &n = model.nodes[index];
                n.name = text(source.name, true);
                n.parent = parent;
                cgltf_node_transform_local(&source, glm::value_ptr(n.localTransform));
                for (unsigned c = 0; c < 4; ++c)
                    for (unsigned r = 0; r < 4; ++r)
                        if (!std::isfinite(n.localTransform[c][r]))
                            return fail(ImportErrorCode::InvalidInput, "Non-finite node transform");
                if (source.mesh)
                {
                    n.primitives = output.array<uint32_t>(source.mesh->primitives_count);
                    if (output.failed())
                        return memory();
                    size_t mesh = size_t(source.mesh - data->meshes);
                    for (size_t i = 0; i < n.primitives.size(); ++i)
                        n.primitives[i] = meshOffsets[mesh] + uint32_t(i);
                }
                if (output.failed())
                    return memory();
                for (size_t i = 0; i < source.children_count; ++i)
                    if (!node(*source.children[i], index, depth + 1))
                        return false;
                return true;
            }

            bool parse()
            {
                options.memory.alloc_func = parseAlloc;
                options.memory.free_func = parseFree;
                options.memory.user_data = &scratch;
                cgltf_result status = cgltf_parse_file(&options, path, &data);
                if (status != cgltf_result_success)
                    return scratch.failed() ? memory() : fail(ImportErrorCode::InvalidInput, "Cannot parse glTF/GLB");
                if (!profile())
                    return false;
                if (cgltf_load_buffers(&options, data, path) != cgltf_result_success)
                    return scratch.failed() ? memory() : fail(ImportErrorCode::Io, "Cannot load glTF buffers");
                if (cgltf_validate(data) != cgltf_result_success)
                    return fail(ImportErrorCode::InvalidInput, "glTF validation failed");
                if (data->nodes_count > NoIndex || data->materials_count >= NoIndex || data->images_count > NoIndex ||
                    data->textures_count > NoIndex || data->samplers_count >= NoIndex)
                    return fail(ImportErrorCode::Unsupported, "Asset exceeds 32-bit index limits");
                if (output.failed())
                    return memory();
                return true;
            }

            bool metadata()
            {
                model.dependencies = output.array<std::string_view>(1 + data->buffers_count + data->images_count);
                model.warnings = output.array<std::string_view>(data->extensions_used_count);
                if (output.failed())
                    return memory();
                if (!dependency(path))
                    return false;
                for (size_t i = 0; i < data->extensions_used_count; ++i)
                    model.warnings[i] = text(data->extensions_used[i]);
                for (size_t i = 0; i < data->buffers_count; ++i)
                    if (data->buffers[i].uri && std::strncmp(data->buffers[i].uri, "data:", 5))
                    {
                        char file[1024];
                        if (!filePath(data->buffers[i].uri, file) || !dependency(file))
                            return false;
                    }
                if (output.failed())
                    return memory();
                return true;
            }

            bool meshes()
            {
                meshOffsets = scratch.array<uint32_t>(data->meshes_count);
                if (scratch.failed())
                    return memory();
                size_t primitiveCount = 0;
                for (size_t i = 0; i < data->meshes_count; ++i)
                {
                    meshOffsets[i] = uint32_t(primitiveCount);
                    if (data->meshes[i].primitives_count > NoIndex - primitiveCount)
                        return fail(ImportErrorCode::Unsupported, "Too many primitives");
                    primitiveCount += data->meshes[i].primitives_count;
                }
                model.primitives = output.array<Primitive>(primitiveCount);
                if (output.failed())
                    return memory();
                for (size_t i = 0; i < data->meshes_count; ++i)
                    for (size_t j = 0; j < data->meshes[i].primitives_count; ++j)
                    {
                        auto &p = model.primitives[meshOffsets[i] + j];
                        p.name = text(data->meshes[i].name, true);
                        if (output.failed())
                            return memory();
                        if (!primitive(data->meshes[i].primitives[j], p))
                            return false;
                    }
                if (output.failed())
                    return memory();
                return true;
            }

            bool scene()
            {
                model.nodes = output.array<Node>(data->nodes_count);
                visited = scratch.array<uint8_t>(data->nodes_count);
                if (output.failed() || scratch.failed())
                    return memory();
                const auto *scene = data->scene ? data->scene : data->scenes_count ? data->scenes : nullptr;
                if (scene)
                {
                    for (size_t i = 0; i < scene->nodes_count; ++i)
                        if (!node(*scene->nodes[i], NoIndex, 0))
                            return false;
                }
                else
                    for (size_t i = 0; i < data->nodes_count; ++i)
                        if (!data->nodes[i].parent && !node(data->nodes[i], NoIndex, 0))
                            return false;
                model.nodes = model.nodes.first(nodeCount);
                if (output.failed())
                    return memory();
                return true;
            }

            bool run()
            {
                if (!parse() || !metadata() || !images() || !materials() || !meshes() || !scene())
                    return false;
                model.dependencies = model.dependencies.first(dependencyCount);
                return true;
            }
        };
    }

    bool ImportModel(const char *path, idk::Arena &output, idk::Arena &scratch, Model &model, ImportError &error)
    {
        model = {};
        error = {};
        if (!path || !*path || &output == &scratch || output.overlaps(scratch))
        {
            error.code = ImportErrorCode::InvalidInput;
            std::snprintf(error.message, sizeof(error.message), "Path must be nonempty and arenas must not overlap");
            return false;
        }
        idk::ArenaScope allocations(output);
        idk::ArenaScope temporary(scratch);
        Importer importer{path, output, scratch, model, error};
        if (!importer.run())
        {
            model = {};
            return false;
        }
        allocations.commit();
        return true;
    }
}
