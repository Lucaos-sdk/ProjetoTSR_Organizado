// Experimental fixed-light model. Requires linear RGB and positive view-space Z,
// known pinhole intrinsics, and matching color/depth grids. No game defaults.
Texture2D<float4> Color : register(t0);
Texture2D<float> Depth : register(t1);
RWTexture2D<float4> Output : register(u0);
cbuffer Parameters : register(b0) {
    uint Width, Height;
    float Fx, Fy, Cx, Cy, Strength, RelativeDepthLimit;
#ifdef TSR_RAW_DEPTH
    float DepthA, DepthB, OutputLimit, Padding1;
#endif
};
cbuffer Weights : register(b1) {
    float4 Hidden[24]; // normal weights xyz, bias w
    float4 Gain[24];   // output weights xyz, padding w
    float4 Bias;
};
float3 Position(int2 p, float z) {
    return float3((p.x-Cx)/Fx, (p.y-Cy)/Fy, 1)*z;
}
float LoadZ(int2 p) {
    float d=Depth.Load(int3(p,0));
#ifdef TSR_RAW_DEPTH
    // Endpoints include the cleared sky plane; do not relight it.
    float z=0;
    if(isfinite(d) && d>0 && d<1 && d!=DepthA) z=DepthB/(d-DepthA);
    z=(isfinite(z) && z>0) ? z : 0;
    return z;
#else
    return d;
#endif
}
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=Width || id.y>=Height) return;
    int2 p=int2(id.xy);
    float4 color=Color.Load(int3(p,0));
    if(Strength==0 || p.x==0 || p.y==0 || p.x==int(Width)-1 || p.y==int(Height)-1) {
        Output[p]=color; return;
    }
    float z=LoadZ(p);
    float l=LoadZ(p+int2(-1,0));
    float r=LoadZ(p+int2(1,0));
    float u=LoadZ(p+int2(0,-1));
    float d=LoadZ(p+int2(0,1));
    float4 adjacent=float4(l,r,u,d);
    if(!isfinite(z) || z<=0) { Output[p]=color; return; }
    bool4 valid=isfinite(adjacent) & (adjacent>0) & (abs(adjacent-z)<=RelativeDepthLimit*z);
    if(!(valid.x||valid.y) || !(valid.z||valid.w)) { Output[p]=color; return; }
    // At a discontinuity use the same-surface side, never bridge the silhouette.
    float3 center=Position(p,z);
    float3 dx=(valid.y?Position(p+int2(1,0),r):center)-(valid.x?Position(p+int2(-1,0),l):center);
    float3 dy=(valid.w?Position(p+int2(0,1),d):center)-(valid.z?Position(p+int2(0,-1),u):center);
    float3 normal=cross(dx,dy);
    float norm=dot(normal,normal);
    if(!isfinite(norm) || norm<=1e-20) { Output[p]=color; return; }
    normal*=rsqrt(norm);
    if(normal.z<0) normal=-normal;
    float3 logGain=Bias.xyz;
    [unroll] for(uint i=0;i<24;i++) {
        float v=dot(normal,Hidden[i].xyz)+Hidden[i].w;
        float activation=2/(1+exp(-2*v))-1;
        logGain+=activation*Gain[i].xyz;
    }
    float3 lit=color.rgb*exp(clamp(logGain,-2,2)*Strength);
#ifdef TSR_RAW_DEPTH
    lit=clamp(lit,-OutputLimit,OutputLimit);
#endif
    Output[p]=float4(lit,color.a);
}
