#include "idk/editor/ShapeDocument.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace idk::editor
{
    glm::mat4 ShapeObject::transform() const
    {
        auto m = glm::translate(glm::mat4(1), position);
        m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        return glm::scale(m, scale);
    }

    bool ShapeObject::setTransform(const glm::mat4 &matrix)
    {
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned r = 0; r < 4; ++r)
                if (!std::isfinite(matrix[c][r]))
                    return false;
        if (std::abs(matrix[0][3]) > .00001f || std::abs(matrix[1][3]) > .00001f || std::abs(matrix[2][3]) > .00001f ||
            std::abs(matrix[3][3] - 1) > .00001f)
            return false;
        glm::vec3 sizes;
        glm::mat3 basis(matrix);
        for (unsigned c = 0; c < 3; ++c)
        {
            sizes[c] = glm::length(basis[c]);
            if (!std::isfinite(sizes[c]) || sizes[c] < 1e-8f)
                return false;
            basis[c] /= sizes[c];
        }
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned d = c + 1; d < 3; ++d)
                if (std::abs(glm::dot(basis[c], basis[d])) > .0001f)
                    return false;
        if (glm::determinant(basis) < 0)
        {
            basis[0] = -basis[0];
            sizes.x = -sizes.x;
        }
        glm::vec3 angles;
        float cy = std::sqrt(basis[0][0] * basis[0][0] + basis[0][1] * basis[0][1]);
        angles.y = std::atan2(-basis[0][2], cy);
        if (cy > 1e-6f)
        {
            angles.x = std::atan2(basis[1][2], basis[2][2]);
            angles.z = std::atan2(basis[0][1], basis[0][0]);
        }
        else
        {
            angles.x = std::atan2(-basis[2][1], basis[1][1]);
            angles.z = 0;
        }
        position = glm::vec3(matrix[3]);
        rotation = glm::degrees(angles);
        scale = sizes;
        return true;
    }

    bool ShapeDocument::initialize(idk::Arena &arena)
    {
        if (mReady)
            return false;
        size_t mark = arena.mark();
        for (uint32_t i = 0; i < ShapeCount; ++i)
            if (!asset::CreateShape(asset::ShapeKind(i), arena, mGeometry[i]))
            {
                mGeometry = {};
                arena.rewind(mark);
                return false;
            }
        mReady = true;
        return true;
    }

    uint32_t ShapeDocument::add(asset::ShapeKind kind)
    {
        if (!mReady || uint32_t(kind) >= ShapeCount)
            return asset::NoIndex;
        for (uint32_t i = 0; i < Capacity; ++i)
            if (!mObjects[i].active)
            {
                auto &object = mObjects[i];
                object = {};
                object.active = true;
                object.shape = kind;
                object.position.y = kind == asset::ShapeKind::Plane ? 0 : .5f;
                object.material.metallic = 0;
                object.material.roughness = .8f;
                object.material.doubleSided = kind == asset::ShapeKind::Plane;
                std::snprintf(object.name, sizeof(object.name), "%s %u", asset::ShapeName(kind), ++mSerial);
                return i;
            }
        return asset::NoIndex;
    }

    uint32_t ShapeDocument::duplicate(uint32_t slot)
    {
        auto *source = get(slot);
        if (!source)
            return asset::NoIndex;
        for (uint32_t next = 0; next < Capacity; ++next)
            if (!mObjects[next].active)
            {
                auto &object = mObjects[next];
                object = *source;
                object.position += glm::vec3(.25f, 0, .25f);
                if (source->isImported())
                {
                    char name[sizeof(object.name)] = {};
                    std::snprintf(name, sizeof(name), "%.*s copy", int(sizeof(name)) - 6,
                                  source->name);
                    std::memcpy(object.name, name, sizeof(name));
                }
                else
                    std::snprintf(object.name, sizeof(object.name), "%s %u", asset::ShapeName(source->shape),
                                  ++mSerial);
                return next;
            }
        return asset::NoIndex;
    }

    bool ShapeDocument::remove(uint32_t slot)
    {
        auto *object = get(slot);
        if (!object)
            return false;
        *object = {};
        return true;
    }

    void ShapeDocument::clear()
    {
        mObjects = {};
    }

    uint32_t ShapeDocument::count() const
    {
        uint32_t count = 0;
        for (const auto &object : mObjects)
            count += object.active;
        return count;
    }

    ShapeObject *ShapeDocument::get(uint32_t slot)
    {
        return slot < Capacity && mObjects[slot].active ? &mObjects[slot] : nullptr;
    }

    const ShapeObject *ShapeDocument::get(uint32_t slot) const
    {
        return slot < Capacity && mObjects[slot].active ? &mObjects[slot] : nullptr;
    }

    const asset::Primitive &ShapeDocument::geometry(asset::ShapeKind kind) const
    {
        return mGeometry[uint32_t(kind)];
    }

    const asset::Primitive &ShapeDocument::geometry(const ShapeObject &object) const
    {
        return object.isImported() ? object.mesh : geometry(object.shape);
    }

    bool ShapeDocument::snapshot(idk::Arena &arena, asset::Model &model) const
    {
        model = {};
        uint32_t size = count();
        if (!mReady || !size)
            return false;
        size_t mark = arena.mark();
        model.primitives = arena.array<asset::Primitive>(size);
        model.materials = arena.array<asset::Material>(size);
        model.nodes = arena.array<asset::Node>(size);
        auto refs = arena.array<uint32_t>(size);
        if (arena.failed())
        {
            arena.rewind(mark);
            model = {};
            return false;
        }
        uint32_t index = 0;
        for (const auto &object : mObjects)
            if (object.active)
            {
                refs[index] = index;
                auto &primitive = model.primitives[index];
                primitive = geometry(object);
                primitive.material = index;
                primitive.name = object.name;
                auto &material = model.materials[index];
                material = object.material;
                material.name = object.name;
                material.baseColor = object.color;
                auto &node = model.nodes[index];
                node.name = object.name;
                node.localTransform = object.transform();
                node.primitives = refs.subspan(index, 1);
                ++index;
            }
        return true;
    }

    uint32_t ShapeDocument::pick(glm::vec3 origin, glm::vec3 direction) const
    {
        uint32_t selected = asset::NoIndex;
        float nearest = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < Capacity; ++i)
            if (const auto *object = get(i))
            {
                auto matrix = object->transform();
                if (std::abs(glm::determinant(matrix)) < 1e-12f)
                    continue;
                auto inverse = glm::inverse(matrix);
                auto start = glm::vec3(inverse * glm::vec4(origin, 1));
                auto ray = glm::vec3(inverse * glm::vec4(direction, 0));
                const auto &mesh = geometry(*object);
                size_t count = mesh.indices.empty() ? mesh.vertices.size() : mesh.indices.size();
                for (size_t k = 0; k < count; k += 3)
                {
                    glm::vec3 a = mesh.vertices[mesh.indices.empty() ? k : mesh.indices[k]].position;
                    glm::vec3 edge1 = mesh.vertices[mesh.indices.empty() ? k + 1 : mesh.indices[k + 1]].position - a;
                    glm::vec3 edge2 = mesh.vertices[mesh.indices.empty() ? k + 2 : mesh.indices[k + 2]].position - a;
                    glm::vec3 p = glm::cross(ray, edge2);
                    float determinant = glm::dot(edge1, p);
                    if (std::abs(determinant) < 1e-8f)
                        continue;
                    glm::vec3 offset = start - a;
                    float u = glm::dot(offset, p) / determinant;
                    if (u < 0 || u > 1)
                        continue;
                    glm::vec3 q = glm::cross(offset, edge1);
                    float v = glm::dot(ray, q) / determinant;
                    if (v < 0 || u + v > 1)
                        continue;
                    float distance = glm::dot(edge2, q) / determinant;
                    if (distance >= 0 && distance < nearest)
                    {
                        selected = i;
                        nearest = distance;
                    }
                }
            }
        return selected;
    }

    bool ShapeDocument::bounds(glm::vec3 &minimum, glm::vec3 &maximum, uint32_t slot) const
    {
        bool found = false;
        for (uint32_t i = 0; i < Capacity; ++i)
            if (const auto *object = get(i))
            {
                if (slot != asset::NoIndex && i != slot)
                    continue;
                auto matrix = object->transform();
                for (const auto &vertex : geometry(*object).vertices)
                {
                    glm::vec3 point(matrix * glm::vec4(vertex.position, 1));
                    if (!found)
                    {
                        minimum = maximum = point;
                        found = true;
                    }
                    else
                    {
                        minimum = glm::min(minimum, point);
                        maximum = glm::max(maximum, point);
                    }
                }
            }
        return found;
    }
} // namespace idk::editor
