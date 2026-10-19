#pragma once
#include "Model.hpp"
#include "libidk/memory/Arena.hpp"

namespace idk::asset
{
    enum class ShapeKind : uint32_t
    {
        Box,
        Sphere,
        Cylinder,
        Plane,
        Count
    };
    const char *ShapeName(ShapeKind kind);
    // Unit shapes centred at the origin; sphere/cylinder radius and half-height .5.
    // Generated streams live in output. Failure restores its entry mark.
    bool CreateShape(ShapeKind kind, idk::Arena &output, Primitive &primitive);
}
