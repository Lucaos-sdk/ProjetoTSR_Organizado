Texture2D<float4> InputColor : register(t0);
Texture2D<float> InputDepth : register(t1);
Texture2D<float2> InputMotion : register(t2);
RWTexture2D<float4> Color : register(u0);
RWTexture2D<float4> Geometry : register(u1);
cbuffer Parameters : register(b0) {
    uint Width; uint Height; float ScaleX; float ScaleY;
    float RemoveX; float RemoveY; float DepthA; float DepthB;
    uint LinearDepth; uint FixedCamera; float ColorScale; uint Padding;
    uint OriginX; uint OriginY; uint Pad2; uint Pad3;
};
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x>=Width || id.y>=Height) return;
    uint2 source=id.xy+uint2(OriginX,OriginY);
    float4 c=InputColor.Load(int3(source,0));
    c.rgb*=ColorScale;
    Color[id.xy]=c;
    float d=InputDepth.Load(int3(source,0));
    float2 motion=InputMotion.Load(int3(source,0))*float2(ScaleX,ScaleY)-float2(RemoveX,RemoveY);
    float z=LinearDepth ? d : DepthB/(d-DepthA);
    bool valid=isfinite(d) && (LinearDepth || (d>=0 && d<=1)) && isfinite(z) && z>0 && all(isfinite(motion));
    Geometry[id.xy]=valid ? float4(z,motion,FixedCamera?z:0) : float4(0,0,0,0);
}
