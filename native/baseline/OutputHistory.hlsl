Texture2D<float4> Color : register(t0);
Texture2D<float4> Geometry : register(t1);
Texture2D<float4> HistoryColor : register(t2);
Texture2D<float4> HistoryGeometry : register(t3);
RWTexture2D<float4> NextColor : register(u0);
RWTexture2D<float4> NextGeometry : register(u1);
cbuffer Parameters : register(b0) {
    uint Width; uint Height; uint UseHistory; uint Padding;
    float JitterX; float JitterY; float ExposureRatio; float Decay;
    uint OutWidth; uint OutHeight; uint Pad2; uint Pad3;
};
float4 ReadColor(int2 xy) { return Color.Load(int3(clamp(xy,int2(0,0),int2(Width-1,Height-1)),0)); }
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x>=OutWidth || id.y>=OutHeight) return;
    uint2 denominator=2*uint2(OutWidth,OutHeight);
    uint2 numerator=(2*id.xy+1)*uint2(Width,Height);
    uint2 whole=numerator/denominator, rem=numerator%denominator;
    float2 f=float2(rem)/float2(denominator)-.5-float2(JitterX,JitterY);
    // Exact half-pixel ties matter for nearest depth/motion and sparse samples.
    float2 halfTie=round(f*2)*.5;
    f=float2(abs(f.x-halfTie.x)<1e-6?halfTie.x:f.x,abs(f.y-halfTie.y)<1e-6?halfTie.y:f.y);
    int2 shift=int2(floor(f));
    int2 lo=int2(whole)+shift;
    f-=float2(shift);
    int2 nearest=lo+int2(floor(f+.5));
    float2 distance=abs(f-float2(nearest-lo));
    float sampleWeight=max(0,1-2*distance.x)*max(0,1-2*distance.y);
    if (any(nearest<0) || any(nearest>=int2(Width,Height))) sampleWeight=0;
    int2 source=clamp(nearest,int2(0,0),int2(Width-1,Height-1));
    float4 g=Geometry.Load(int3(source,0));
    float4 sampleColor=ReadColor(source);
    float4 current=lerp(lerp(ReadColor(lo),ReadColor(lo+int2(1,0)),f.x),
                        lerp(ReadColor(lo+int2(0,1)),ReadColor(lo+int2(1,1)),f.x),f.y);
    precise float2 scale=float2(OutWidth,OutHeight)/float2(Width,Height);
    precise float2 offset=g.yz*scale;
    // Keep small fractional motion separate from large output pixel indices.
    // Adding them in float first loses subpixel precision at wide resolutions.
    bool valid=UseHistory && g.w>0 && all(abs(offset)<=float2(OutWidth,OutHeight));
    int2 base=0;
    float2 frac=0;
    if (valid) {
        int2 wholeOffset=int2(floor(offset));
        base=int2(id.xy)+wholeOffset;
        frac=offset-float2(wholeOffset);
        valid=all(base>=0) && all(base<int2(OutWidth,OutHeight));
        if (base.x==int(OutWidth)-1 && frac.x>0) valid=false;
        if (base.y==int(OutHeight)-1 && frac.y>0) valid=false;
    }
    float oldWeight=0;
    float3 sum=0;
    if (valid) {
        [unroll] for (int y=0;y<2;++y) {
            [unroll] for (int x=0;x<2;++x) {
                float w=(x?frac.x:1-frac.x)*(y?frac.y:1-frac.y);
                if (w>0) {
                    int2 tap=min(base+int2(x,y),int2(OutWidth-1,OutHeight-1));
                    float4 hg=HistoryGeometry.Load(int3(tap,0));
                    if (hg.x<=0 || hg.y<=0 || abs(hg.x-g.w)>.01+.01*g.w) valid=false;
                    float mass=w*hg.y;
                    oldWeight+=mass;
                    sum+=mass*HistoryColor.Load(int3(tap,0)).rgb;
                }
            }
        }
    }
    if (!valid) { oldWeight=0; sum=0; }
    oldWeight*=Decay;
    float total=oldWeight+sampleWeight;
    if (total>0) current.rgb=(Decay*sum*ExposureRatio+sampleWeight*sampleColor.rgb)/total;
    NextColor[id.xy]=current;
    NextGeometry[id.xy]=float4(g.x,min(total,4),0,valid?1:0);
}
