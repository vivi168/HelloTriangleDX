#include "common.hlsli"

Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

cbuffer ConstantBuffer0 : register(b0)
{
    float4 color;
};

float4 main(VS_OUTPUT input) : SV_TARGET
{
    return t0.Sample(s0, input.texCoord) * color;
}
