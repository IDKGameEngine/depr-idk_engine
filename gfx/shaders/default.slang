#include "idk/gfx/slang/slang.hpp"

UniformBufferRO(idk::PerCameraData) camera;
UniformBufferRO(idk::PerDrawData) draw;
UniformBufferRO(idk::MaterialData) material;
[[vk::binding(idk::BIND_ALBEDO, 0)]] Sampler2D albedo;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(4)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertmain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(camera.Proj, mul(camera.View, mul(draw.Model, float4(input.position, 1.0))));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmain(VertexOutput input) : SV_Target0
{
    float4 color = albedo.Sample(input.uv) * material.baseColor * input.color;
    if (material.alpha.x == 1.0 && color.a < material.alpha.y) discard;
    if (material.alpha.x != 2.0) color.a = 1.0;
    return color;
}
