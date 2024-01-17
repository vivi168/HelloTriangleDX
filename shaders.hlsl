Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

cbuffer ConstantBuffer0 : register(b0)
{
    float4 color;
};

cbuffer ConstantBuffer1 : register(b1)
{
    float4x4 WorldViewProj;
};

struct VS_INPUT
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

VS_OUTPUT VSMain(VS_INPUT input)
{
    VS_OUTPUT output;
    output.pos = mul(float4(input.pos, 1.0f), WorldViewProj);
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_TARGET
{
    return t0.Sample(s0, input.texCoord) * color;
}
