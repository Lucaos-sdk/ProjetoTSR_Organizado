// Current-frame, world-anchored diffuse appearance transform. No history texture.
Texture2D<float4> Color : register(t0);
Texture2D<float> Depth : register(t1);
RWTexture2D<float4> Output : register(u0);
cbuffer Parameters : register(b0) {
    uint Width, Height;
    float Fx, Fy, Cx, Cy, Strength, RelativeDepthLimit;
    float DepthA, DepthB, OutputLimit, Smoothing;
    float4 NormalRow0, NormalRow1, NormalRow2;
    float ColorTransfer;
    float SceneryProtection, FadeStart, FadeEnd;
};
cbuffer Weights : register(b1) {
    float4 Hidden[24];
    float4 Gain[24];
    float4 Bias;
};
// 8x8 output tile plus the two-pixel smoothing halo. Every thread participates
// in loading/synchronization, including threads outside odd image dimensions.
#ifndef TSR_DIRECT_DEPTH_READS
groupshared float DepthTile[144];
#endif
float ReadZ(int2 p) {
    float raw=Depth.Load(int3(p,0));
    float z=0;
    if(isfinite(raw) && raw>0 && raw<1 && raw!=DepthA) z=DepthB/(raw-DepthA);
    return isfinite(z) && z>0 ? z : 0;
}
float LoadZ(int2 p,int2 origin) {
#ifdef TSR_DIRECT_DEPTH_READS
    return ReadZ(p);
#else
    int2 local=p-origin;
    return DepthTile[local.y*12+local.x];
#endif
}
bool SameSurface(float z,float center) {
    return isfinite(z) && z>0 && abs(z-center)<=RelativeDepthLimit*center;
}
float3 Position(int2 p,float z) {return float3((p.x-Cx)/Fx,(p.y-Cy)/Fy,1)*z;}
float3 SmoothNormal(int2 p,float centerZ,float3 fallback,int2 origin,out float confidence) {
    // Fit inverse depth, which is affine on a perspective-projected plane.
    // Center-relative samples avoid cancellation on distant/flat surfaces.
    float centerQ=1/centerZ;
    float total=0,sx=0,sy=0,sq=0,sxx=0,sxy=0,syy=0,sxq=0,syq=0;
    float support=0;
    [unroll] for(int y=-2;y<=2;++y) [unroll] for(int x=-2;x<=2;++x) {
        int2 at=p+int2(x,y);
        if(at.x>=0 && at.y>=0 && at.x<int(Width) && at.y<int(Height)) {
            float z=LoadZ(at,origin);
            float wx=abs(x)==0?6:(abs(x)==1?4:1);
            float wy=abs(y)==0?6:(abs(y)==1?4:1);
            float w=wx*wy;
            if(SceneryProtection>0 && z>0)
                support+=w*(1-smoothstep(.5*RelativeDepthLimit,RelativeDepthLimit,abs(z-centerZ)/centerZ));
            if(SameSurface(z,centerZ)) {
                float q=1/z-centerQ;
                total+=w;sx+=w*x;sy+=w*y;sq+=w*q;
                sxx+=w*x*x;sxy+=w*x*y;syy+=w*y*y;sxq+=w*x*q;syq+=w*y*q;
            }
        }
    }
    confidence=smoothstep(.7,.98,support/256);
    float inv=1/max(total,1);
    float mx=sx*inv,my=sy*inv,mq=sq*inv;
    float xx=sxx*inv-mx*mx,xy=sxy*inv-mx*my,yy=syy*inv-my*my;
    float xq=sxq*inv-mx*mq,yq=syq*inv-my*mq;
    float determinant=xx*yy-xy*xy;
    float3 result=fallback;
    if(determinant>1e-4) {
        float a=(xq*yy-yq*xy)/determinant,b=(yq*xx-xq*xy)/determinant;
        float q=centerQ+mq-a*mx-b*my;
        float3 fit=float3(a*Fx,b*Fy,q-a*(p.x-Cx)-b*(p.y-Cy));
        float length2=dot(fit,fit);
        if(isfinite(length2) && length2>1e-20 && q>0 && abs(q-centerQ)<=.02*centerQ) {
            fit*=rsqrt(length2);
            if(dot(fit,Position(p,centerZ))<0)fit=-fit;
            if(dot(fit,fallback)>.2)result=normalize(lerp(fallback,fit,Smoothing));
        }
    }
    return result;
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID,uint3 group:SV_GroupID,uint3 thread:SV_GroupThreadID) {
    int2 origin=int2(group.xy*8)-2;
#ifndef TSR_DIRECT_DEPTH_READS
    uint index=thread.y*8+thread.x;
    for(uint tileIndex=index;tileIndex<144;tileIndex+=64) {
        int2 at=origin+int2(tileIndex%12,tileIndex/12);
        float z=0;
        if(at.x>=0 && at.y>=0 && at.x<int(Width) && at.y<int(Height))z=ReadZ(at);
        DepthTile[tileIndex]=z;
    }
    GroupMemoryBarrierWithGroupSync();
#endif
    if(id.x>=Width || id.y>=Height)return;
    int2 p=int2(id.xy);float4 color=Color.Load(int3(p,0));
    if(Strength==0 || p.x==0 || p.y==0 || p.x==int(Width)-1 || p.y==int(Height)-1){Output[p]=color;return;}
    float z=LoadZ(p,origin);
    if(z<=0){Output[p]=color;return;}
    float3 center=Position(p,z);
    // Radial distance avoids changing the fade merely by turning the camera.
    float distanceConfidence=1;
    if(SceneryProtection>0)distanceConfidence=1-smoothstep(FadeStart,FadeEnd,length(center));
    if(SceneryProtection==1 && distanceConfidence==0){Output[p]=color;return;}
    float l=LoadZ(p+int2(-1,0),origin),r=LoadZ(p+int2(1,0),origin),u=LoadZ(p+int2(0,-1),origin),d=LoadZ(p+int2(0,1),origin);
    bool vl=SameSurface(l,z),vr=SameSurface(r,z),vu=SameSurface(u,z),vd=SameSurface(d,z);
    if(!(vl||vr)||!(vu||vd)){Output[p]=color;return;}
    float3 dx=(vr?Position(p+int2(1,0),r):center)-(vl?Position(p+int2(-1,0),l):center);
    float3 dy=(vd?Position(p+int2(0,1),d):center)-(vu?Position(p+int2(0,-1),u):center);
    float3 normal=cross(dx,dy);float norm=dot(normal,normal);
    if(!isfinite(norm)||norm<=1e-20){Output[p]=color;return;}
    normal*=rsqrt(norm);
    // Orient relative to the ray, not the Z axis: off-axis faces remain coherent.
    if(dot(normal,center)<0)normal=-normal;
    float surfaceConfidence=1;
    if(Smoothing>0 || SceneryProtection>0)normal=SmoothNormal(p,z,normal,origin,surfaceConfidence);
    float effectiveStrength=Strength*lerp(1,distanceConfidence*surfaceConfidence,SceneryProtection);
    if(effectiveStrength==0){Output[p]=color;return;}
    normal=float3(dot(NormalRow0.xyz,normal),dot(NormalRow1.xyz,normal),dot(NormalRow2.xyz,normal));
    float3 logGain=Bias.xyz;
    [unroll] for(uint i=0;i<24;++i){float v=dot(normal,Hidden[i].xyz)+Hidden[i].w;
        logGain+=(2/(1+exp(-2*v))-1)*Gain[i].xyz;}
    float3 gain=exp(clamp(logGain,-2,2)*effectiveStrength);
    if(ColorTransfer<1) {
        // Compose the learned illumination in linear light. A scalar gain keeps
        // the original RGB ratios; blending gains retains the same luminance
        // as the legacy result until the output format's range is reached.
        // Normalize first so very bright FP32 inputs cannot overflow the ratio.
        float scale=max(max(abs(color.r),abs(color.g)),max(abs(color.b),1e-20));
        // D3D shader division may use a reciprocal; at extreme FP32 values
        // that reciprocal is subnormal and flushes to zero. Rescale both sides.
        float factor=scale>1e20?1e-20:1;
        float3 normalized=(color.rgb*factor)/(scale*factor);
        float3 positive=max(normalized,0);
        float3 lumaWeights=float3(.2126,.7152,.0722);
        float luminance=dot(positive,lumaWeights);
        float lightGain=luminance>1e-8?dot(positive*gain,lumaWeights)/luminance:1;
        gain=lerp(lightGain.xxx,gain,ColorTransfer);
        // Limit the entire RGB triplet together at FP16/FP32 boundaries.
        float3 scaled=abs(normalized*gain);
        float peak=max(scaled.r,max(scaled.g,scaled.b));
        float available=(OutputLimit*factor)/(max(scale,1)*factor);
        if(peak>available)gain*=available/peak;
    }
    Output[p]=float4(clamp(color.rgb*gain,-OutputLimit,OutputLimit),color.a);
}
