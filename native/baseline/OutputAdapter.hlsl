Texture2D<float4> Input : register(t0);
RWTexture2D<float4> Output : register(u0);
cbuffer Parameters : register(b0) { uint Width; uint Height; uint SourceX; uint SourceY; uint DestinationX; uint DestinationY; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x<Width && id.y<Height) Output[id.xy+uint2(DestinationX,DestinationY)]=Input.Load(int3(id.xy+uint2(SourceX,SourceY),0));
}
