// Linear scene color, Float4 structured buffers, row-major pixels.
// Deliberately independent of the unfinished neural tensor packing.
#ifdef TSR_TEXTURES
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);
#else
StructuredBuffer<float4> Source : register(t0);
RWStructuredBuffer<float4> Destination : register(u0);
#endif
cbuffer Dimensions : register(b0) {
    uint SrcWidth; uint SrcHeight; uint DstWidth; uint DstHeight;
    float JitterX; float JitterY;
    uint ReconstructionMode;
};
float4 ReadClamped(int2 p)
{
    p = clamp(p, int2(0, 0), int2(SrcWidth, SrcHeight) - 1);
#ifdef TSR_TEXTURES
    return Source.Load(int3(p, 0));
#else
    return Source[p.y * SrcWidth + p.x];
#endif
}
float4 CubicWeights(float t) {
    float t2=t*t, t3=t2*t;
    return float4(-.5*t+t2-.5*t3, 1-2.5*t2+1.5*t3,
                  .5*t+2*t2-1.5*t3, -.5*t2+.5*t3);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= DstWidth || id.y >= DstHeight) return;
    // Split the center-aligned rational coordinate before converting to float.
    // Dividing a large float coordinate can amplify reciprocal rounding on GPUs.
    // Dimensions are capped at 16384, so this numerator fits in uint32.
    uint2 denominator = 2 * uint2(DstWidth, DstHeight);
    uint2 numerator = (2 * id.xy + 1) * uint2(SrcWidth, SrcHeight);
    uint2 whole = numerator / denominator;
    uint2 remainder = numerator % denominator;
    int2 beforeCenter = int2(remainder < uint2(DstWidth, DstHeight));
    int2 lo = int2(whole) - beforeCenter;
    float2 f = float2(remainder) / float2(denominator) - 0.5 + float2(beforeCenter);
    // Input samples live on the jittered grid; output centers are unjittered.
    // Shift the small fractional part to avoid losing precision at large indices.
    f -= float2(JitterX, JitterY);
    // Cubic clipping chooses a discrete neighborhood. Stabilize near-integer
    // ties so reciprocal rounding cannot select the opposite clipping cell.
    if (ReconstructionMode == 1) {
        float2 nearest=round(f);
        f=float2(abs(f.x-nearest.x)<1e-6 ? nearest.x : f.x,
                 abs(f.y-nearest.y)<1e-6 ? nearest.y : f.y);
    }
    int2 shift = int2(floor(f));
    lo += shift;
    f -= float2(shift);
    float4 a=ReadClamped(lo), b=ReadClamped(lo+int2(1,0));
    float4 c=ReadClamped(lo+int2(0,1)), d=ReadClamped(lo+int2(1,1));
    float4 result=lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
    if (ReconstructionMode == 1) {
        float4 wx=CubicWeights(f.x), wy=CubicWeights(f.y);
        float4 sum=0;
        [unroll] for (int y=0;y<4;++y) {
            [unroll] for (int x=0;x<4;++x)
                sum+=ReadClamped(lo+int2(x-1,y-1))*wx[x]*wy[y];
        }
        // Suppress ringing without assuming [0,1]; preserve HDR and negatives.
        result=clamp(sum,min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
    }
#ifdef TSR_TEXTURES
    Destination[id.xy] = result;
#else
    Destination[id.y * DstWidth + id.x] = result;
#endif
}
