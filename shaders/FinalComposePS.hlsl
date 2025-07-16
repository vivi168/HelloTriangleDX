// TODO: DRY!!!
// make a second more generic hlsli T.T AND STOP COPY PASTING T.T
cbuffer PerDrawConstants : register(b0)
{
  uint visibilityBufferId;
}

cbuffer FrameConstants : register(b1)
{
  float Time;
  float3 CameraWS;
  float4 FrustumPlanes[6];
  float2 ScreenSize;
};

cbuffer BuffersDescriptorIndices : register(b2)
{
  uint vertexPositionsBufferId;
  uint vertexNormalsBufferId;
  // TODO: tangents
  uint vertexUVsBufferId;

  uint meshletsBufferId;
  uint meshletUniqueIndicesBufferId;
  uint meshletsPrimitivesBufferId;

  uint materialsBufferId;
  uint instancesBufferId;
};

// TODO: TMP DRY T.T
struct Meshlet {
  uint VertCount;
  uint VertOffset;
  uint PrimCount;
  uint PrimOffset;

  uint instanceIndex;
  uint materialIndex;

  float4 boundingSphere;
  uint normalCone;
  float apexOffset;
};

SamplerState s0 : register(s0);

// TODO: TMP just to visualize it's working
float4 main(float4 position : SV_Position) : SV_TARGET
{
  Texture2D<uint> tex = ResourceDescriptorHeap[visibilityBufferId];
  uint value = tex.Load(int3(position.xy, 0));

  uint primitiveId = value & 0x7F;
  uint meshletId = value >> 7;

  StructuredBuffer<Meshlet> meshlets = ResourceDescriptorHeap[meshletsBufferId];
  Meshlet m = meshlets[meshletId];
  uint instanceId = m.instanceIndex;

  uint h = instanceId * 2654435761;
  uint r = (h >> 0) & 0xff;
  uint g = (h >> 8) & 0xff;
  uint b = (h >> 16) & 0xff;
  float4 color = float4(float(r), float(g), float(b), 255.0f) / 255.0f;

  return color;
}
