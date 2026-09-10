Texture2D<float4> Color : register(t0);
Texture2D<float4> Geometry : register(t1); // current Z, motion X/Y, predicted previous Z.
Texture2D<float4> HistoryColor : register(t2);
Texture2D<float4> HistoryGeometry : register(t3);
RWTexture2D<float4> NextColor : register(u0);
RWTexture2D<float4> NextGeometry : register(u1);
cbuffer Parameters : register(b0) {
    uint Width; uint Height; uint UseHistory; uint Padding;
    float JitterX; float JitterY; float ExposureRatio; float HistoryWeight;
};
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Width || id.y >= Height) return;
    float4 current = Color.Load(int3(id.xy, 0));
    float4 geometry = Geometry.Load(int3(id.xy, 0));
    float mask = 0;
    if (UseHistory && geometry.w > 0) {
        float2 p = float2(id.xy) + geometry.yz + float2(JitterX, JitterY);
        if (all(p >= 0) && all(p <= float2(Width - 1, Height - 1))) {
            int2 lo = int2(floor(p));
            float2 f = frac(p);
            float3 sum = 0;
            bool valid = true;
            [unroll] for (int y = 0; y < 2; ++y) {
                [unroll] for (int x = 0; x < 2; ++x) {
                    float weight = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
                    if (weight > 0) {
                        int2 tap = min(lo + int2(x,y), int2(Width-1, Height-1));
                        float depth = HistoryGeometry.Load(int3(tap,0)).x;
                        if (depth <= 0 || abs(depth - geometry.w) > .01 + .01 * geometry.w) valid = false;
                        sum += weight * HistoryColor.Load(int3(tap,0)).rgb;
                    }
                }
            }
            if (valid) {
                current.rgb = (1 - HistoryWeight) * current.rgb + HistoryWeight * sum * ExposureRatio;
                mask = 1;
            }
        }
    }
    NextColor[id.xy] = current; // Alpha always comes from the current frame.
    NextGeometry[id.xy] = float4(geometry.x, 0, 0, mask);
}
