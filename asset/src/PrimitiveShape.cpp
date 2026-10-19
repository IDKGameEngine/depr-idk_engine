#include "idk/asset/PrimitiveShape.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace idk::asset
{
    namespace
    {
        constexpr uint32_t Segments = 24, Rings = 16;

        void quad(Primitive &p, uint32_t face, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d)
        {
            glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
            glm::vec3 t = glm::normalize(b - a), bitangent = -glm::normalize(d - a);
            glm::vec4 tangent(t, glm::dot(glm::cross(n, t), bitangent) < 0 ? -1 : 1);
            uint32_t first = face * 4;
            p.vertices[first + 0] = {a, n, {0, 1}, tangent, glm::vec4(1)};
            p.vertices[first + 1] = {b, n, {1, 1}, tangent, glm::vec4(1)};
            p.vertices[first + 2] = {c, n, {1, 0}, tangent, glm::vec4(1)};
            p.vertices[first + 3] = {d, n, {0, 0}, tangent, glm::vec4(1)};
            constexpr uint32_t indices[] = {0, 1, 2, 0, 2, 3};
            for (unsigned i = 0; i < 6; ++i)
                p.indices[face * 6 + i] = first + indices[i];
        }

        void box(Primitive &p)
        {
            quad(p, 0, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {.5f, .5f, .5f}, {.5f, -.5f, .5f});
            quad(p, 1, {-.5f, -.5f, .5f}, {-.5f, .5f, .5f}, {-.5f, .5f, -.5f}, {-.5f, -.5f, -.5f});
            quad(p, 2, {-.5f, .5f, -.5f}, {-.5f, .5f, .5f}, {.5f, .5f, .5f}, {.5f, .5f, -.5f});
            quad(p, 3, {-.5f, -.5f, .5f}, {-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, -.5f, .5f});
            quad(p, 4, {-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f});
            quad(p, 5, {.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}, {-.5f, .5f, -.5f}, {.5f, .5f, -.5f});
        }

        void sphere(Primitive &p)
        {
            for (uint32_t row = 0; row <= Rings; ++row)
                for (uint32_t col = 0; col <= Segments; ++col)
                {
                    float u = float(col) / Segments, v = float(row) / Rings;
                    float lon = u * glm::two_pi<float>(), lat = v * glm::pi<float>();
                    glm::vec3 n(std::sin(lat) * std::cos(lon), std::cos(lat), std::sin(lat) * std::sin(lon));
                    if (row == 0)
                        n = {0, 1, 0};
                    if (row == Rings)
                        n = {0, -1, 0};
                    p.vertices[row * (Segments + 1) + col] = {
                        n * .5f, n, {u, v}, {-std::sin(lon), 0, std::cos(lon), 1}, glm::vec4(1)};
                }
            size_t index = 0;
            for (uint32_t row = 0; row < Rings; ++row)
                for (uint32_t col = 0; col < Segments; ++col)
                {
                    uint32_t a = row * (Segments + 1) + col, b = a + Segments + 1;
                    if (row > 0)
                    {
                        p.indices[index++] = a;
                        p.indices[index++] = a + 1;
                        p.indices[index++] = b;
                    }
                    if (row + 1 < Rings)
                    {
                        p.indices[index++] = a + 1;
                        p.indices[index++] = b + 1;
                        p.indices[index++] = b;
                    }
                }
        }

        void cylinder(Primitive &p)
        {
            size_t index = 0;
            for (uint32_t i = 0; i <= Segments; ++i)
            {
                float u = float(i) / Segments, angle = u * glm::two_pi<float>();
                glm::vec3 n(std::cos(angle), 0, std::sin(angle));
                glm::vec4 t(-std::sin(angle), 0, std::cos(angle), 1);
                p.vertices[i * 2] = {glm::vec3(n.x * .5f, -.5f, n.z * .5f), n, {u, 1}, t, glm::vec4(1)};
                p.vertices[i * 2 + 1] = {glm::vec3(n.x * .5f, .5f, n.z * .5f), n, {u, 0}, t, glm::vec4(1)};
                if (i < Segments)
                {
                    uint32_t a = i * 2;
                    for (uint32_t value : {a, a + 1, a + 2, a + 2, a + 1, a + 3})
                        p.indices[index++] = value;
                }
            }
            for (uint32_t cap = 0; cap < 2; ++cap)
            {
                float y = cap ? .5f : -.5f;
                glm::vec3 n(0, cap ? 1 : -1, 0);
                glm::vec4 t(1, 0, 0, cap ? -1 : 1);
                uint32_t center = 2 * (Segments + 1) + cap * (Segments + 2);
                p.vertices[center] = {{0, y, 0}, n, {.5f, .5f}, t, glm::vec4(1)};
                for (uint32_t i = 0; i <= Segments; ++i)
                {
                    float angle = float(i) / Segments * glm::two_pi<float>();
                    float x = std::cos(angle) * .5f, z = std::sin(angle) * .5f;
                    p.vertices[center + 1 + i] = {{x, y, z}, n, {x + .5f, z + .5f}, t, glm::vec4(1)};
                    if (i < Segments)
                    {
                        p.indices[index++] = center;
                        p.indices[index++] = center + 1 + i + (cap ? 1 : 0);
                        p.indices[index++] = center + 1 + i + (cap ? 0 : 1);
                    }
                }
            }
        }
    }

    const char *ShapeName(ShapeKind kind)
    {
        switch (kind)
        {
            case ShapeKind::Box:
                return "Box";
            case ShapeKind::Sphere:
                return "Sphere";
            case ShapeKind::Cylinder:
                return "Cylinder";
            case ShapeKind::Plane:
                return "Plane";
            default:
                return "Invalid shape";
        }
    }

    bool CreateShape(ShapeKind kind, idk::Arena &output, Primitive &primitive)
    {
        primitive = {};
        uint32_t vertices = 0, indices = 0;
        switch (kind)
        {
            case ShapeKind::Box:
                vertices = 24;
                indices = 36;
                break;
            case ShapeKind::Sphere:
                vertices = (Rings + 1) * (Segments + 1);
                indices = (Rings - 1) * Segments * 6;
                break;
            case ShapeKind::Cylinder:
                vertices = (Segments + 1) * 2 + (Segments + 2) * 2;
                indices = Segments * 12;
                break;
            case ShapeKind::Plane:
                vertices = 4;
                indices = 6;
                break;
            default:
                return false;
        }
        size_t mark = output.mark();
        primitive.vertices = output.array<Vertex>(vertices);
        primitive.indices = output.array<uint32_t>(indices);
        if (output.failed())
        {
            output.rewind(mark);
            primitive = {};
            return false;
        }
        primitive.name = ShapeName(kind);
        primitive.hasTangents = true;
        switch (kind)
        {
            case ShapeKind::Box:
                box(primitive);
                break;
            case ShapeKind::Sphere:
                sphere(primitive);
                break;
            case ShapeKind::Cylinder:
                cylinder(primitive);
                break;
            case ShapeKind::Plane:
                quad(primitive, 0, {-.5f, 0, -.5f}, {-.5f, 0, .5f}, {.5f, 0, .5f}, {.5f, 0, -.5f});
                break;
            default:
                break;
        }
        return true;
    }
}
