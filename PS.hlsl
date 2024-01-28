#include "common.hlsli"

Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

cbuffer ConstantBuffer0 : register(b0)
{
    float4 color;
};

float4 main(VS_OUTPUT input) : SV_TARGET
{
    float4 color2 = t0.Sample(s0, input.texCoord) * color;
    
    if (color2.a < 0.01)
        discard;
    
    return color2;
}
