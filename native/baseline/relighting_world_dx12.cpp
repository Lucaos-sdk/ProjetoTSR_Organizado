#include "dx12_context.h"
#include "test_texture_transfer.h"
#include "../integration/game_relighting_dx12.h"
#include <fstream>
using namespace tsr::integration;
static void Require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static CameraVector Unit(CameraVector v){float n=std::sqrt(CameraDot(v,v));for(float& x:v)x/=n;return v;}
static CameraSample Camera(float yaw) {
    CameraSample c;c.right={std::cos(yaw),0,-std::sin(yaw)};c.up={0,1,0};c.forward={std::sin(yaw),0,std::cos(yaw)};return c;
}
static tsr::Pixel Model(CameraVector n,float strength=.35f) {
    tsr::Pixel p{.6f,.6f,.6f,.75f};
    for(int ch=0;ch<3;++ch){double gain=GameLightWeights[192+ch];for(int i=0;i<24;++i)
        gain+=std::tanh(double(n[0])*GameLightWeights[4*i]+double(n[1])*GameLightWeights[4*i+1]+double(n[2])*GameLightWeights[4*i+2]+GameLightWeights[4*i+3])*GameLightWeights[96+4*i+ch];
        p[ch]*=float(std::exp(std::clamp(gain,-2.,2.)*strength));}return p;
}
class Harness {
public:
    Context context;
    std::shared_ptr<SubmissionObserver> observer=std::make_shared<SubmissionObserver>();
    GameRelighting pass;
    GameRelighting directPass;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    Event event;UINT64 serial=0;
    explicit Harness(bool warp):context(warp,true,true),pass(context.device.Get(),observer),directPass(context.device.Get(),observer,false) {
        Check(context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
        Check(context.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
        Check(context.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
    }
    ~Harness(){list->Close();}
    void Submit() {
        Check(list->Close(),"Close");ID3D12CommandList* commands[]={list.Get()};context.queue->ExecuteCommandLists(1,commands);
        observer->NotifySubmitted(context.queue.Get(),1,commands);
        Check(context.queue->Signal(fence.Get(),++serial),"Signal");Check(fence->SetEventOnCompletion(serial,event.handle),"Event");
        Require(WaitForSingleObject(event.handle,30000)==WAIT_OBJECT_0,"GPU timeout");
        Check(allocator->Reset(),"Reset allocator");auto hr=list->Reset(allocator.Get(),nullptr);
        observer->NotifyReset(list.Get(),SUCCEEDED(hr));Check(hr,"Reset list");
    }
    std::vector<tsr::Pixel> Render(tsr::Size size,const std::vector<float>& z,const CameraProjection& p,const LightingFrame& lighting,bool fp16=false,bool direct=false,const std::vector<tsr::Pixel>* customColor=nullptr) {
        const size_t count=tsr::Count(size);Require(z.size()==count,"Invalid depth fixture");
        std::vector<float> raw(count);std::vector<tsr::Pixel> color(count,tsr::Pixel{.6f,.6f,.6f,.75f});
        if(customColor){Require(customColor->size()==count,"Invalid color fixture");color=*customColor;}
        for(size_t i=0;i<count;++i)raw[i]=z[i]>0?p.depthA+p.depthB/z[i]:0;
        std::vector<uint16_t> halves;
        if(fp16){halves.resize(count*4);for(size_t i=0;i<count;++i)for(int ch=0;ch<4;++ch)
            halves[i*4+ch]=DirectX::PackedVector::XMConvertFloatToHalf(color[i][ch]);}
        Transfer input(context,list.Get(),size,fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT,
            fp16?static_cast<const void*>(halves.data()):static_cast<const void*>(color.data()),fp16?8:16);
        Transfer depth(context,list.Get(),size,DXGI_FORMAT_R32_FLOAT,raw.data(),4);
        Submit();std::string reason;
        auto& renderer=direct?directPass:pass;
        auto* output=renderer.Record(list.Get(),input.texture.Get(),depth.texture.Get(),size.width,size.height,p,.35f,reason,lighting);
        Require(output!=nullptr,reason.c_str());
        TextureTransfer layout(context.device.Get(),output);
        auto readback=Buffer(context.device.Get(),size_t(layout.bytes),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(list.Get(),output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        auto src=TextureTransfer::TextureLocation(output),dst=layout.BufferLocation(readback.Get());
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Transition(list.Get(),output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Submit();
        void* mapped=nullptr;D3D12_RANGE range{0,size_t(layout.bytes)},noWrite{};Check(readback->Map(0,&range,&mapped),"Readback");
        std::vector<tsr::Pixel> result(count);
        for(UINT y=0;y<size.height;++y){auto* row=static_cast<char*>(mapped)+layout.footprint.Offset+size_t(y)*layout.footprint.Footprint.RowPitch;
            if(!fp16)std::memcpy(result.data()+size_t(y)*size.width,row,size_t(size.width)*16);
            else for(UINT x=0;x<size.width;++x)for(int ch=0;ch<4;++ch){uint16_t bits;std::memcpy(&bits,row+(size_t(x)*4+ch)*2,2);
                result[size_t(y)*size.width+x][ch]=DirectX::PackedVector::XMConvertHalfToFloat(bits);}}
        readback->Unmap(0,&noWrite);
        for(auto& pixel:result){for(float v:pixel)Require(std::isfinite(v),"Nonfinite pixel");Require(pixel[3]==.75f,"Alpha changed");}
        context.CheckDebugMessages();return result;
    }
};
static void Save(const char* name,tsr::Size size,const std::vector<tsr::Pixel>& pixels) {
    std::ofstream file(name,std::ios::binary);file<<"P6\n"<<size.width<<' '<<size.height<<"\n255\n";
    for(const auto& p:pixels)for(int ch=0;ch<3;++ch){float v=std::clamp(p[ch],0.f,1.f);
        v=v<=.0031308f?v*12.92f:1.055f*std::pow(v,1/2.4f)-.055f;file.put(char(std::lround(v*255)));}
}
static void TestComposition(Harness& h) {
    const tsr::Size size{33,19};const size_t count=tsr::Count(size);
    CameraProjection projection{40,40,16,9,-.000030755997f,.20000616f,1};
    const double weights[3]={.2126,.7152,.0722};double worstReference=0,worstChroma=0;
    bool observedChange=false;
    for(float angle:{0.f,-.8f})for(bool half:{false,true}) {
        const auto baseline=Model({std::sin(angle),0,std::cos(angle)});double gains[3];for(int c=0;c<3;++c)gains[c]=baseline[c]/.6;
        const float high=half?60000.f:2.8e38f;
        const std::vector<tsr::Pixel> palette={{.7f,.32f,.18f,.75f},{1,0,0,.75f},{0,1,0,.75f},{0,0,1,.75f},
            {.2f,.5f,.9f,.75f},{8,4,2,.75f},{0,0,0,.75f},{.00001f,.00002f,.00004f,.75f},
            {high,high*.5f,high*.25f,.75f},{-.2f,.3f,.5f,.75f},{-.5f,-.3f,-.1f,.75f}};
        std::vector<tsr::Pixel> input(count);std::vector<float> depth(count,3);
        for(size_t i=0;i<count;++i){input[i]=palette[i%palette.size()];
            if(half)for(int c=0;c<4;++c)input[i][c]=DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(input[i][c]));}
        for(UINT y=0;y<size.height;++y)depth[size_t(y)*size.width+10]=0;
        std::vector<tsr::Pixel> natural,legacy;
        for(float transfer:{0.f,.5f,1.f}) {
            auto lighting=*MakeLightingFrame(Camera(angle),Camera(0),1);lighting.colorTransfer=transfer;
            auto result=h.Render(size,depth,projection,lighting,half,false,&input);
            const double limit=half?65504.:3.4e38;
            for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x){const size_t i=size_t(y)*size.width+x;
                const bool bypass=x==0||y==0||x==size.width-1||y==size.height-1||depth[i]==0;
                double y0=0,y1=0;for(int c=0;c<3;++c){double v=std::max(double(input[i][c]),0.);y0+=weights[c]*v;y1+=weights[c]*v*gains[c];}
                double scalar=y0>0?y1/y0:1;double expected[3],peak=0;
                for(int c=0;c<3;++c){expected[c]=input[i][c]*(scalar*(1-transfer)+gains[c]*transfer);peak=std::max(peak,std::abs(expected[c]));}
                if(transfer<1 && peak>limit)for(double& v:expected)v*=limit/peak;
                for(int c=0;c<3;++c){if(bypass)expected[c]=input[i][c];else expected[c]=std::clamp(expected[c],-limit,limit);
                    double error=std::abs(double(result[i][c])-expected[c])/std::max(std::abs(expected[c]),1e-4);
                    worstReference=std::max(worstReference,error);
                    if(error>=(half?.002:.00008)) {
                        std::cerr<<"Composition mismatch half="<<half<<" transfer="<<transfer<<" x="<<x<<" y="<<y<<" channel="<<c
                            <<" input="<<input[i][c]<<" expected="<<expected[c]<<" actual="<<result[i][c]<<" relative="<<error<<'\n';
                        throw std::runtime_error("Composition differs from independent CPU reference");
                    }
                    if(bypass)Require(result[i][c]==input[i][c],"Composition changed sky or image boundary");}
                if(transfer==0){double before=0,after=0;for(int c=0;c<3;++c){before=std::max(before,std::abs(double(input[i][c])));after=std::max(after,std::abs(double(result[i][c])));}
                    if(before>0 && after>0)for(int c=0;c<3;++c){double error=std::abs(result[i][c]/after-input[i][c]/before);
                        worstChroma=std::max(worstChroma,error);Require(error<(half?.002:.000002),"Natural lighting changed RGB ratios");}}
            }
            if(transfer==0)natural=result;if(transfer==1)legacy=result;
        }
        for(size_t i=0;i<count;++i){double a=0,b=0,peak=0;bool positive=true;
            for(int c=0;c<3;++c){a+=natural[i][c]*weights[c];b+=legacy[i][c]*weights[c];peak=std::max(peak,double(input[i][c]));positive&=input[i][c]>=0;
                if(std::abs(natural[i][c]-legacy[i][c])>.01f)observedChange=true;}
            if(positive && peak<=8)Require(std::abs(a-b)/std::max(std::abs(b),1e-4)<(half?.002:.00008),"Color control changed unclipped luminance");}
    }
    Require(observedChange,"Color composition comparison produced no visible-sized difference");
    LightingFrame bad;bad.colorTransfer=1.1f;Require(!ValidLightingFrame(bad),"Invalid transfer accepted");
    bad.colorTransfer=std::numeric_limits<float>::quiet_NaN();Require(!ValidLightingFrame(bad),"NaN transfer accepted");
    std::cout<<"PASS color composition: max_relative_reference_error="<<worstReference<<" max_normalized_rgb_error="<<worstChroma
        <<" fp16_fp32=true hdr=true black=true negative=true sky=true alpha=true transfer_endpoints=true\n";
}
static void TestSceneryProtection(Harness& h) {
    const tsr::Size size{65,49};const auto count=tsr::Count(size);const size_t center=24*65+32;
    CameraProjection p{90,90,32,24,-.000030755997f,.20000616f,1};
    LightingFrame lighting;lighting.smoothing=1;lighting.sceneryProtection=1;
    const auto model=Model({0,0,1});double maxError=0,rotationError=0;
    for(bool half:{false,true}) {
        const float input=half?DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(.6f)):.6f;
        double previousEffect=1e9;
        for(float distance:{5.f,20.f,25.f,35.f,50.f,65.f,79.f,80.f,100.f,160.f}) {
            std::vector<float> depth(count,distance);auto out=h.Render(size,depth,p,lighting,half);
            double t=std::clamp((double(distance)-20)/60,0.,1.);double weight=1-t*t*(3-2*t),effect=0;
            for(int ch=0;ch<3;++ch){const double expected=input*std::pow(double(model[ch])/.6,weight);
                const double error=std::abs(out[center][ch]-expected);maxError=std::max(maxError,error);
                Require(error<(half?.001:.00002),"Distance protection differs from reference fade");effect+=std::abs(out[center][ch]-input);}
            Require(effect<=previousEffect+(half?.002:.00002),"Distance protection is not monotonic");previousEffect=effect;
            if(distance>=80)for(auto pixel:out)for(int ch=0;ch<3;++ch)Require(pixel[ch]==input,"Distant scenery was modified");
        }
        std::vector<float> nearDepth(count,5);auto safe=h.Render(size,nearDepth,p,lighting,half);
        LightingFrame old=lighting;old.sceneryProtection=0;auto legacy=h.Render(size,nearDepth,p,old,half);
        for(UINT y=3;y<size.height-3;++y)for(UINT x=3;x<size.width-3;++x)for(int ch=0;ch<4;++ch)
            Require(std::abs(safe[size_t(y)*65+x][ch]-legacy[size_t(y)*65+x][ch])<1e-6,"Near smooth surfaces changed");
        std::vector<float> depth(count);for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x)
            depth[size_t(y)*65+x]=x<30?4:(x>33?9:0);
        auto protectedEdges=h.Render(size,depth,p,lighting,half);auto oldEdges=h.Render(size,depth,p,old,half);
        for(int ch=0;ch<3;++ch){
            Require(protectedEdges[24*65+29][ch]==input,"Depth edge should receive no relighting");
            const double feather=std::abs(protectedEdges[24*65+28][ch]-input),full=std::abs(oldEdges[24*65+28][ch]-input);
            Require(feather<=full+.001,"Edge protection amplified the effect");
            Require(std::abs(protectedEdges[24*65+27][ch]-oldEdges[24*65+27][ch])<1e-6,"Protection spread beyond the depth neighborhood");
        }
        // A one-pixel foreground strip: ordinary one-sided derivatives accept it,
        // but the surrounding depth cannot support a surface-light estimate.
        std::fill(depth.begin(),depth.end(),9.f);for(UINT y=0;y<size.height;++y){depth[size_t(y)*65+32]=4;depth[size_t(y)*65+33]=4;}
        auto thin=h.Render(size,depth,p,lighting,half);
        for(int ch=0;ch<3;++ch)Require(thin[center][ch]==input,"Thin depth cutout was relit");
        auto withoutSmooth=lighting;withoutSmooth.smoothing=0;auto unsmoothed=h.Render(size,depth,p,withoutSmooth,half);
        for(int ch=0;ch<3;++ch)Require(unsmoothed[center][ch]==input,"Edge protection depends on smoothing slider");
        auto direct=h.Render(size,depth,p,lighting,half,true);
        Require(thin==direct,"Shared depth cache changes protection");
    }
    // The center ray sees the same radial range at different view-Z/angles.
    std::vector<tsr::Pixel> reference;
    for(float angle:{0.f,-.6f,.6f}){auto projection=p;projection.cx=32-p.fx*std::tan(angle);
        std::vector<float> depth(count,50*std::cos(angle));auto out=h.Render(size,depth,projection,lighting);
        if(reference.empty())reference=out;
        for(int ch=0;ch<3;++ch)rotationError=std::max(rotationError,double(std::abs(out[center][ch]-reference[center][ch])));}
    Require(rotationError<.00002,"Radial fade changes with view angle");
    auto bad=lighting;bad.fadeEnd=bad.fadeStart;Require(!ValidLightingFrame(bad),"Empty fade interval accepted");
    bad=lighting;bad.sceneryProtection=-1;Require(!ValidLightingFrame(bad),"Invalid protection accepted");
    std::cout<<"PASS scenery protection: max_fade_error="<<maxError<<" radial_angle_error="<<rotationError
        <<" far_identity=true near_unchanged=true depth_edges=true thin_cutouts=true fp16_fp32=true\n";
}
int main(int argc,char** argv) {
    try {
        bool warp=false,benchmark=false;for(int i=1;i<argc;++i)if(std::string(argv[i])=="--warp")warp=true;
            else if(std::string(argv[i])=="--benchmark")benchmark=true;else throw std::invalid_argument("Expected --warp / --benchmark");
        Harness h(warp);TestComposition(h);TestSceneryProtection(h);const tsr::Size size{129,97};const size_t count=tsr::Count(size);
        CameraProjection p{150,150,64,48,-.000030755997f,.20000616f,1};
        auto anchor=Camera(0);double worstAnchored=0,worstUnanchored=0;
        // Same plane/normal in the scene viewed under seven rotations, including
        // normals in the negative-Z half of the model's reference coordinates.
        for(auto n:{Unit({.45f,.25f,.85f}),Unit({-.3f,.2f,-.94f})}) {
            const auto expected=Model(n);
            float base=std::atan2(n[0],n[2]);
            for(int angle=-30;angle<=30;angle+=10) {
                auto camera=Camera(base+angle*.01745329252f);auto lighting=MakeLightingFrame(camera,anchor,1);Require(bool(lighting),"Invalid basis");
                CameraVector nc{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)nc[i]+=lighting->normalTransform[j*4+i]*n[j];
                std::vector<float> depth(count);for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x)
                    depth[size_t(y)*size.width+x]=3/(nc[0]*(float(x)-p.cx)/p.fx+nc[1]*(float(y)-p.cy)/p.fy+nc[2]);
                auto out=h.Render(size,depth,p,*lighting);
                LightingFrame screen;screen.smoothing=1;auto old=h.Render(size,depth,p,screen);
                for(UINT y=4;y<size.height-4;++y)for(UINT x=4;x<size.width-4;++x)for(int ch=0;ch<3;++ch){const size_t i=size_t(y)*size.width+x;
                    worstAnchored=std::max(worstAnchored,double(std::abs(out[i][ch]-expected[ch])));
                    worstUnanchored=std::max(worstUnanchored,double(std::abs(old[i][ch]-expected[ch])));}
            }
        }
        Require(worstAnchored<.0005,"World light changed with viewing angle");
        Require(worstUnanchored>.02,"Rotation test did not distinguish camera-relative light");
        // A smooth projected surface approximated by an 8-pixel triangular mesh.
        // Its ideal inverse-depth derivatives give independent reference normals.
        const auto q=[](float x,float y){return .3f+.000004f*(x*x+y*y);};
        std::vector<float> mesh(count);std::vector<tsr::Pixel> ideal(count);
        for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x){float xx=float(x)-64,yy=float(y)-48;
            float bx=std::floor(xx/8)*8,by=std::floor(yy/8)*8,u=(xx-bx)/8,v=(yy-by)/8;
            float interpolated;
            if(u+v<=1)interpolated=q(bx,by)*(1-u-v)+q(bx+8,by)*u+q(bx,by+8)*v;
            else interpolated=q(bx+8,by+8)*(u+v-1)+q(bx+8,by)*(1-v)+q(bx,by+8)*(1-u);
            const size_t i=size_t(y)*size.width+x;mesh[i]=1/interpolated;
            float a=.000008f*xx,b=.000008f*yy;
            ideal[i]=Model(Unit({a*p.fx,b*p.fy,q(xx,yy)-a*xx-b*yy}));
        }
        LightingFrame rough,smooth;smooth.smoothing=1;
        auto original=h.Render(size,mesh,p,rough),filtered=h.Render(size,mesh,p,smooth);
        double roughMse=0,smoothMse=0;size_t samples=0;
        for(UINT y=4;y<size.height-4;++y)for(UINT x=4;x<size.width-4;++x)for(int ch=0;ch<3;++ch){size_t i=size_t(y)*size.width+x;
            roughMse+=std::pow(original[i][ch]-ideal[i][ch],2);smoothMse+=std::pow(filtered[i][ch]-ideal[i][ch],2);++samples;}
        roughMse/=samples;smoothMse/=samples;
        Require(smoothMse<roughMse*.95,"Smoothing did not improve faceted-surface lighting");
        if(!warp){Save("artifacts/relighting-world-v2/faceted.ppm",size,original);Save("artifacts/relighting-world-v2/smooth.ppm",size,filtered);Save("artifacts/relighting-world-v2/ideal.ppm",size,ideal);}
        // Hard depth break and a strip of sky must not receive blended geometry.
        std::vector<float> step(count);for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x)step[size_t(y)*size.width+x]=x<60?3:(x>65?9:0);
        auto edges=h.Render(size,step,p,smooth);auto flat=Model({0,0,1});double edgeError=0;
        for(UINT y=2;y<size.height-2;++y)for(UINT x=2;x<size.width-2;++x)for(int ch=0;ch<3;++ch){const size_t i=size_t(y)*size.width+x;
            const float expected=step[i]>0?flat[ch]:.6f;edgeError=std::max(edgeError,double(std::abs(edges[i][ch]-expected)));}
        Require(edgeError<.0002,"Smoothing crossed silhouette or changed sky");
        auto repeated=h.Render(size,mesh,p,smooth);
        Require(std::memcmp(repeated.data(),filtered.data(),count*sizeof(tsr::Pixel))==0,"Prior frame contaminated current-frame result");
        double cacheError=0;
        for(bool half:{false,true})for(auto& depth:{mesh,step}) {
            auto cached=h.Render(size,depth,p,smooth,half),direct=h.Render(size,depth,p,smooth,half,true);
            for(size_t i=0;i<count;++i)for(int ch=0;ch<4;++ch)
                cacheError=std::max(cacheError,double(std::abs(cached[i][ch]-direct[i][ch])));
        }
        Require(cacheError<1e-6,"Depth cache changed shading");
        for(tsr::Size tiny:{tsr::Size{1,1},tsr::Size{2,17},tsr::Size{17,2}}) {
            std::vector<float> depth(tsr::Count(tiny),3);
            auto cached=h.Render(tiny,depth,p,smooth),direct=h.Render(tiny,depth,p,smooth,false,true);
            Require(cached==direct,"Partial workgroup changed boundary pixels");
        }
        std::cout<<"PASS cached/direct depth agreement: max_error="<<cacheError<<" fp16_fp32=true odd_dimensions=true partial_workgroups=true\n";
        LightingFrame bad;bad.normalTransform[0]=2;Require(!ValidLightingFrame(bad),"Non-rotation accepted");
        std::cout<<"PASS world anchor: max_error="<<worstAnchored<<" unanchored_error="<<worstUnanchored<<" rotations=14 full_sphere=true\n";
        std::cout<<"PASS faceted surface: original_mse="<<roughMse<<" smooth_mse="<<smoothMse<<" reduction="<<1-smoothMse/roughMse<<'\n';
        std::cout<<"PASS silhouettes/sky: max_error="<<edgeError<<" previous_frame_independence=true alpha_preserved=true\n";
        std::cout<<"Last completed own GPU pass ms="<<h.pass.lastGpuMs<<" size=129x97 debug=true\n";
        if(benchmark)for(tsr::Size render:{tsr::Size{1476,830},tsr::Size{1920,1080}}){
            CameraProjection bp{render.width*.6f,render.width*.6f,render.width*.5f-.5f,render.height*.5f-.5f,p.depthA,p.depthB,1};
            std::vector<float> depth(tsr::Count(render));
            for(UINT y=0;y<render.height;++y)for(UINT x=0;x<render.width;++x){float dx=(float(x)-bp.cx)/bp.fx,dy=(float(y)-bp.cy)/bp.fy;
                depth[size_t(y)*render.width+x]=1/(.3f+.04f*(dx*dx+dy*dy));}
            auto lighting=MakeLightingFrame(Camera(.4f),anchor,1);std::vector<double> ms[2];
            // Alternate order to avoid always giving one variant a warmer GPU.
            for(int i=0;i<25;++i)for(int k=0;k<2;++k){const int variant=(i+k)%2;
                lighting->colorTransfer=0;lighting->sceneryProtection=float(variant);
                h.Render(render,depth,bp,*lighting,true);
                // ReadTiming is collected on the next Record; same-variant repeat
                // makes the reported completed query belong to this mode.
                h.Render(render,depth,bp,*lighting,true);
                if(i>=5)ms[variant].push_back(h.pass.lastGpuMs);}
            for(int variant=0;variant<2;++variant){auto& values=ms[variant];std::sort(values.begin(),values.end());
                std::cout<<"BENCHMARK own_pass size="<<render.width<<'x'<<render.height<<" scenery_protection="<<variant<<" color_transfer=0 cache=true fp16=true depth=R32F smoothing=1 debug=true samples="<<values.size()
                    <<" median_ms="<<values[values.size()/2]<<" p95_ms="<<values[size_t(std::ceil(.95*values.size()))-1]<<" max_ms="<<values.back()<<'\n';}
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
