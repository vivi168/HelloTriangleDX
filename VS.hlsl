#include "common.hlsli"

cbuffer ConstantBuffer1 : register(b1)
{
    float4x4 WorldViewProj;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos, 1.0f), WorldViewProj);
    
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}