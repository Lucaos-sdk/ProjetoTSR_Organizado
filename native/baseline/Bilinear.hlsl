// Linear scene color, Float4 structured buffers, row-major pixels.
// Deliberately independent of the unfinished neural tensor packing.
StructuredBuffer<float4> Source : register(t0);
RWStructuredBuffer<float4> Destination : register(u0);
cbuffer Dimensions : register(b0) { uint SrcWidth; uint SrcHeight; uint DstWidth; uint DstHeight; };
float4 ReadClamped(int2 p)
{
    p = clamp(p, int2(0, 0), int2(SrcWidth, SrcHeight) - 1);
    return Source[p.y * SrcWidth + p.x];
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= DstWidth || id.y >= DstHeight) return;
    float2 p = (float2(id.xy) + 0.5) * float2(SrcWidth, SrcHeight) / float2(DstWidth, DstHeight) - 0.5;
    int2 lo = int2(floor(p));
    float2 f = frac(p);
    float4 top = lerp(ReadClamped(lo), ReadClamped(lo + int2(1, 0)), f.x);
    float4 bottom = lerp(ReadClamped(lo + int2(0, 1)), ReadClamped(lo + int2(1, 1)), f.x);
    Destination[id.y * DstWidth + id.x] = lerp(top, bottom, f.y);
}
