#include "dx12_context.h"
#include "temporal_pass.h"
#include "temporal_cases.h"
#include "visual_sequence.h"
#include "reconstruction.h"
#include "output_history_cases.h"
#include <memory>
#include <chrono>
#include <iomanip>
#include "runtime_options.h"

struct TemporalGpu {
    struct Timing { double temporalMs=0, spatialMs=0, gpuBatchMs=0, hostMs=0; } timing;
    bool measure=false;
    Context& context;
    tsr::Reconstruction filter;
    bool outputHistory;
    TemporalPass pass;
    ComPtr<ID3D12Resource> color[2], geometry[2];
    // One synchronous slot per instance. Run waits for its fence before any reuse.
    ComPtr<ID3D12Resource> current, currentGeometry, output, uploadColor, uploadGeometry;
    ComPtr<ID3D12Resource> historyReadBuffer, geometryReadBuffer, outputReadBuffer;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> timestamps;
    ComPtr<ID3D12Fence> fence;
    Event event;
    UINT64 fenceValue=0, frequency=0;
    uint64_t resourceCreations=0;
    tsr::Size inputCapacity{}, outputCapacity{}, historyCapacity{};
    bool initialized = false;
    unsigned front = 0;
    tsr::FrameParameters previous{};
    tsr::TemporalReference reference;
    tsr::OutputHistoryReference outputReference;

    explicit TemporalGpu(Context& c, tsr::Reconstruction reconstruction = tsr::Reconstruction::Bilinear,
                         bool useOutputHistory=false) : context(c), filter(reconstruction), outputHistory(useOutputHistory),
                         pass(c.device.Get(),useOutputHistory) {}
    void Upload(ID3D12GraphicsCommandList* commands, ID3D12Resource* texture,
                                const std::vector<tsr::Pixel>& pixels, tsr::Size size, ComPtr<ID3D12Resource>& upload,
                                D3D12_RESOURCE_STATES before) {
        const TextureTransfer layout(context.device.Get(), texture);
        if (!upload) { upload = Buffer(context.device.Get(), size_t(layout.bytes), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); ++resourceCreations; }
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0,0};
        Check(upload->Map(0, &noRead, &mapped), "Temporal upload map");
        std::memset(mapped, 0xcd, size_t(layout.bytes));
        for (UINT y=0; y<size.height; ++y)
            std::memcpy(static_cast<unsigned char*>(mapped) + layout.footprint.Offset + size_t(y)*layout.footprint.Footprint.RowPitch,
                        pixels.data() + size_t(y)*size.width, size_t(size.width)*sizeof(tsr::Pixel));
        const D3D12_RANGE written{0, size_t(layout.bytes)};
        upload->Unmap(0, &written);
        Transition(commands, texture, before, D3D12_RESOURCE_STATE_COPY_DEST);
        const auto src = layout.BufferLocation(upload.Get()), dst = TextureTransfer::TextureLocation(texture);
        commands->CopyTextureRegion(&dst, 0,0,0, &src, nullptr);
        Transition(commands, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    }
    struct Readback {
        TextureTransfer layout;
        ComPtr<ID3D12Resource> buffer;
        Readback(Context& c, ID3D12GraphicsCommandList* list, ID3D12Resource* texture,
                 D3D12_RESOURCE_STATES before, ComPtr<ID3D12Resource>& cached, uint64_t& creations) : layout(c.device.Get(), texture) {
            if (!cached) { cached = Buffer(c.device.Get(), size_t(layout.bytes), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST); ++creations; }
            buffer=cached;
            Transition(list, texture, before, D3D12_RESOURCE_STATE_COPY_SOURCE);
            const auto src = TextureTransfer::TextureLocation(texture), dst = layout.BufferLocation(buffer.Get());
            list->CopyTextureRegion(&dst, 0,0,0, &src, nullptr);
            Transition(list, texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        std::vector<tsr::Pixel> Get(tsr::Size size) {
            std::vector<tsr::Pixel> result(tsr::Count(size));
            void* mapped = nullptr;
            const D3D12_RANGE range{0,size_t(layout.bytes)}, noWrite{0,0};
            Check(buffer->Map(0,&range,&mapped), "Temporal readback map");
            for (UINT y=0; y<size.height; ++y)
                std::memcpy(result.data()+size_t(y)*size.width,
                    static_cast<const unsigned char*>(mapped)+layout.footprint.Offset+size_t(y)*layout.footprint.Footprint.RowPitch,
                    size_t(size.width)*sizeof(tsr::Pixel));
            buffer->Unmap(0,&noWrite);
            return result;
        }
    };
    tsr::TemporalResult Run(const tsr::FrameInput& frame, const char* label) {
        const auto hostStart=std::chrono::steady_clock::now();
        tsr::ValidateFrame(frame); // Invalid input must not advance either history.
        const auto& p = frame.parameters;
        if (outputHistory && (p.outputSize.width<p.renderSize.width || p.outputSize.height<p.renderSize.height))
            throw std::invalid_argument("Output history supports native resolution or upscaling, not downsampling");
        const auto historySize=outputHistory ? p.outputSize : p.renderSize;
        const bool reuse = tsr::CanReuse(initialized, previous, p);
        const float exposure = reuse ? p.preExposure / previous.preExposure : 1;
        if (!std::isfinite(exposure)) throw std::invalid_argument("Exposure ratio overflows float");
        const bool allocate = !initialized || !tsr::SameSize(outputHistory ? previous.outputSize : previous.renderSize, historySize);
        if (allocate) {
            for (unsigned i=0; i<2; ++i) {
                color[i] = Texture(context.device.Get(), historySize, true);
                geometry[i] = Texture(context.device.Get(), historySize, true);
                resourceCreations+=2;
            }
            const auto desc=color[0]->GetDesc();
            const auto allocation=context.device->GetResourceAllocationInfo(0,1,&desc);
            std::cout << "History allocation: " << historySize.width << 'x' << historySize.height
                      << "; four textures MiB=" << double(allocation.SizeInBytes)*4/(1024*1024) << '\n';
        }
        if (!tsr::SameSize(historyCapacity,historySize)) {
            historyReadBuffer.Reset(); geometryReadBuffer.Reset(); historyCapacity=historySize;
        }
        const unsigned back = 1-front;
        const bool newInput=!current || !tsr::SameSize(inputCapacity,p.renderSize);
        if (newInput) {
            current=Texture(context.device.Get(),p.renderSize,false);
            currentGeometry=Texture(context.device.Get(),p.renderSize,false);
            resourceCreations+=2;
            uploadColor.Reset(); uploadGeometry.Reset(); inputCapacity=p.renderSize;
        }
        const bool newOutput=!output || !tsr::SameSize(outputCapacity,p.outputSize);
        if (!outputHistory && newOutput) {
            output=Texture(context.device.Get(),p.outputSize,true); ++resourceCreations;
            outputReadBuffer.Reset(); outputCapacity=p.outputSize;
        }
        if (!allocator) {
            Check(context.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)), "Temporal allocator");
            Check(context.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)), "Temporal list");
            Check(context.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)), "Temporal fence");
        } else {
            Check(allocator->Reset(), "Temporal allocator reset");
            Check(list->Reset(allocator.Get(),nullptr), "Temporal list reset");
        }
        if (measure && !queries) {
            D3D12_QUERY_HEAP_DESC queryDesc{};
            queryDesc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; queryDesc.Count=6;
            Check(context.device->CreateQueryHeap(&queryDesc,IID_PPV_ARGS(&queries)), "Timestamp heap");
            timestamps=Buffer(context.device.Get(),6*sizeof(UINT64),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            ++resourceCreations;
            Check(context.queue->GetTimestampFrequency(&frequency), "Timestamp frequency");
            if (!frequency) throw std::runtime_error("Zero timestamp frequency");
        }
        auto stamp=[&](UINT index) { if (measure) list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,index); };
        stamp(0); // Whole GPU batch: uploads, dispatches, transitions and image readbacks.
        std::vector<tsr::Pixel> packed(frame.depth.size());
        for (size_t i=0; i<packed.size(); ++i) {
            const float predicted = p.depthReprojection == tsr::DepthReprojection::PredictedPreviousZ ?
                                    frame.predictedPreviousDepth[i] : frame.depth[i];
            packed[i] = {frame.depth[i],frame.motion[i][0],frame.motion[i][1],predicted};
        }
        const auto inputState=newInput ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Upload(list.Get(), current.Get(), frame.color, p.renderSize, uploadColor, inputState);
        Upload(list.Get(), currentGeometry.Get(), packed, p.renderSize, uploadGeometry, inputState);
        // Every completed call leaves both history slots in SRV state. New slots
        // start COMMON; the unused old slot is bound but never read on reset.
        if (allocate) {
            Transition(list.Get(),color[front].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(list.Get(),geometry[front].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        const auto oldState = allocate ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Transition(list.Get(),color[back].Get(),oldState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(list.Get(),geometry[back].Get(),oldState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 8;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!heap) Check(context.device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&heap)), "Temporal descriptors");
        const UINT stride = context.device->GetDescriptorHandleIncrementSize(heapDesc.Type);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        ID3D12Resource* resources[] = {current.Get(),currentGeometry.Get(),color[front].Get(),geometry[front].Get(),
                                      color[back].Get(),geometry[back].Get(),color[back].Get(),output.Get()};
        for (unsigned i=0; i<8; ++i) {
            if (i==4 || i==5 || i==7) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
                view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                context.device->CreateUnorderedAccessView(resources[i],nullptr,&view,cpu);
            } else {
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Texture2D.MipLevels = 1;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                context.device->CreateShaderResourceView(resources[i],&view,cpu);
            }
            cpu.ptr += stride;
        }
        ID3D12DescriptorHeap* heaps[] = {heap.Get()};
        list->SetDescriptorHeaps(1,heaps);
        const auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        auto at = [&](UINT i) { return D3D12_GPU_DESCRIPTOR_HANDLE{gpu.ptr+UINT64(i)*stride}; };
        pass.Record(list.Get(),{heap.Get(),at(0),at(4)},p,previous,reuse,measure ? queries.Get() : nullptr,1);
        // Consume temporal color on GPU before performing diagnostic readbacks.
        Transition(list.Get(),color[back].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        std::unique_ptr<Readback> outputRead;
        if (!outputHistory) {
        // Feed the GPU-produced temporal color straight into the spatial shader.
        Transition(list.Get(),output.Get(),newOutput ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->SetPipelineState(context.pipeline.Get());
        list->SetComputeRootSignature(context.root.Get());
        list->SetComputeRootDescriptorTable(0,at(6));
        list->SetComputeRootDescriptorTable(1,at(7));
        struct SpatialConstants { UINT sw,sh,dw,dh; float jx,jy; UINT mode; } spatial{
            p.renderSize.width,p.renderSize.height,p.outputSize.width,p.outputSize.height,
            p.jitterPixels[0],p.jitterPixels[1],UINT(filter)};
        static_assert(sizeof(SpatialConstants)==28);
        list->SetComputeRoot32BitConstants(2,7,&spatial,0);
        stamp(3);
        list->Dispatch((p.outputSize.width+7)/8,(p.outputSize.height+7)/8,1);
        stamp(4);
        outputRead=std::make_unique<Readback>(context,list.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,outputReadBuffer,resourceCreations);
        }
        Readback historyRead(context,list.Get(),color[back].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,historyReadBuffer,resourceCreations);
        Readback geometryRead(context,list.Get(),geometry[back].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,geometryReadBuffer,resourceCreations);
        if (outputHistory) { stamp(3); stamp(4); } // Initialize every resolved query.
        stamp(5);
        if (measure) list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,6,timestamps.Get(),0);
        Check(list->Close(), "Temporal close");
        ID3D12CommandList* lists[] = {list.Get()};
        context.queue->ExecuteCommandLists(1,lists);
        Check(context.queue->Signal(fence.Get(),++fenceValue), "Temporal signal");
        Check(fence->SetEventOnCompletion(fenceValue,event.handle), "Temporal completion");
        if (WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0) throw std::runtime_error("Temporal GPU wait failed");
        Check(context.device->GetDeviceRemovedReason(), "Temporal device removed");
        context.CheckDebugMessages();
        if (measure) {
            UINT64* ticks=nullptr;
            const D3D12_RANGE read{0,6*sizeof(UINT64)}, noWrite{0,0};
            Check(timestamps->Map(0,&read,reinterpret_cast<void**>(&ticks)), "Timestamp map");
            UINT64 values[6]; std::memcpy(values,ticks,sizeof(values));
            timestamps->Unmap(0,&noWrite);
            for (unsigned i=1;i<6;++i)
                if (values[i]<values[i-1]) throw std::runtime_error("Non-monotonic GPU timestamps");
            const double ms=1000.0/double(frequency);
            timing.temporalMs=double(values[2]-values[1])*ms;
            timing.spatialMs=outputHistory ? 0 : double(values[4]-values[3])*ms;
            timing.gpuBatchMs=double(values[5]-values[0])*ms;
        }
        tsr::TemporalResult result{historyRead.Get(historySize), geometryRead.Get(historySize)};
        auto expected = outputHistory ? outputReference.Run(frame) : reference.Run(frame);
        if (filter != tsr::Reconstruction::Bilinear)
            expected.output = tsr::Reconstruct(expected.color,p.renderSize,p.outputSize,p.jitterPixels,filter);
        const double colorError = tsr::Verify(result.color,expected.color);
        tsr::Verify(result.geometry,expected.geometry);
        for (size_t i=0; i<result.geometry.size(); ++i)
            if (result.geometry[i][3]!=expected.geometry[i][3]) throw std::runtime_error("History validity mask mismatch");
        result.output = outputHistory ? result.color : outputRead->Get(p.outputSize);
        const double outputError = tsr::Verify(result.output,expected.output);
        front = back;
        initialized = true;
        previous = p;
        timing.hostMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-hostStart).count();
        std::cout << "PASS " << label << "; frame=" << p.frameIndex << "; reuse=" << reuse
                  << "; history_error=" << colorError << "; output_error=" << outputError << '\n';
        return result;
    }
};
int main(int argc, char** argv) {
    try {
        bool warp=false, debug=false, full=false;
        bool outputHistory=false;
        bool listAdapters=false, renderSet=false, outputSet=false;
        int adapter=-1;
        uint32_t warmup=10, samples=50;
        bool countsSet=false;
        std::filesystem::path benchmarkFile;
        tsr::Size render{1280,720}, output{1920,1080};
        tsr::Reconstruction filter=tsr::Reconstruction::Bilinear;
        std::filesystem::path visualDirectory;
        for (int i=1;i<argc;++i) {
            const std::string arg(argv[i]);
            if (arg=="--warp") warp=true;
            else if (arg=="--benchmark" && i+1<argc) benchmarkFile=argv[++i];
            else if (arg=="--warmup" && i+1<argc) { warmup=tsr::ParseUnsigned(argv[++i]); countsSet=true; }
            else if (arg=="--samples" && i+1<argc) { samples=tsr::ParseUnsigned(argv[++i]); countsSet=true; }
            else if (arg=="--debug") debug=true;
            else if (arg=="--full") full=true;
            else if (arg=="--cubic") filter=tsr::Reconstruction::CubicClamped;
            else if (arg=="--output-history") outputHistory=true;
            else if (arg=="--list-adapters") listAdapters=true;
            else if (arg=="--adapter" && i+1<argc) adapter=tsr::ParseAdapter(argv[++i]);
            else if (arg=="--render" && i+1<argc) { render=tsr::ParseResolution(argv[++i]); renderSet=true; }
            else if (arg=="--output" && i+1<argc) { output=tsr::ParseResolution(argv[++i]); outputSet=true; }
            else if (arg=="--1080p") { render={1280,720}; output={1920,1080}; renderSet=outputSet=true; }
            else if (arg=="--visual-dir" && i+1<argc) visualDirectory=argv[++i];
            else throw std::invalid_argument("Usage: tsr_temporal_dx12 [--warp | --adapter N] [--list-adapters] [--debug] [--full] [--1080p | --render WxH --output WxH] [--cubic | --output-history] [--visual-dir path] [--benchmark file.csv --warmup N --samples N]");
        }
        if (outputHistory && filter!=tsr::Reconstruction::Bilinear) throw std::invalid_argument("Cubic and output-history are separate experiments");
        if (renderSet!=outputSet) throw std::invalid_argument("Specify both --render and --output");
        if (warp && adapter>=0) throw std::invalid_argument("Use --warp or --adapter, not both");
        if (countsSet && benchmarkFile.empty()) throw std::invalid_argument("Sample counts require --benchmark");
        if (!benchmarkFile.empty() && (full || !visualDirectory.empty() || listAdapters))
            throw std::invalid_argument("Benchmark cannot be combined with full, visual-dir or list-adapters");
        if (!warmup || !samples || warmup>10000 || samples>10000)
            throw std::invalid_argument("Warmup and samples must be in 1..10000");
        if (listAdapters) { Context::ListAdapters(); return 0; }
        Context context(warp,debug,true,adapter);
        std::cout << "Reconstruction: " << (outputHistory ? "output-grid sample history" : (filter==tsr::Reconstruction::Bilinear ? "bilinear" : "Catmull-Rom clamped")) << '\n';
        if (!benchmarkFile.empty()) {
            if (!benchmarkFile.parent_path().empty()) std::filesystem::create_directories(benchmarkFile.parent_path());
            std::ofstream csv(benchmarkFile);
            if (!csv) throw std::runtime_error("Cannot open benchmark CSV");
            csv << "phase,frame,render_width,render_height,output_width,output_height,mode,debug,warp,temporal_ms,spatial_ms,shader_sum_ms,gpu_batch_ms,host_validation_ms,resource_creations\n" << std::setprecision(10);
            TemporalGpu bench(context,filter,outputHistory); bench.measure=true;
            auto frame=tsr::FrameFixture(render,output);
            for (auto& motion:frame.motion) motion={-.25f,.125f};
            const tsr::Motion jitter[]={{-.25f,-.25f},{.25f,.25f},{.25f,-.25f},{-.25f,.25f}};
            std::vector<double> shaders,batches,hosts;
            for (uint32_t i=0;i<warmup+samples;++i) {
                frame.parameters.frameIndex=i; frame.parameters.reset=i==0; frame.parameters.jitterPixels=jitter[i%4];
                const auto before=bench.resourceCreations;
                bench.Run(frame,i<warmup ? "benchmark warmup" : "benchmark sample");
                const auto created=bench.resourceCreations-before;
                if (i>0 && created) throw std::runtime_error("Unexpected steady-state GPU resource allocation");
                const auto t=bench.timing;
                const double sum=t.temporalMs+t.spatialMs;
                csv << (i<warmup ? "warmup" : "sample") << ',' << i << ',' << render.width << ',' << render.height << ','
                    << output.width << ',' << output.height << ',' << (outputHistory ? "output_history" : (filter==tsr::Reconstruction::Bilinear ? "bilinear" : "cubic"))
                    << ',' << debug << ',' << warp << ',' << t.temporalMs << ',' << t.spatialMs << ',' << sum << ',' << t.gpuBatchMs << ',' << t.hostMs << ',' << created << '\n';
                if (i>=warmup) { shaders.push_back(sum); batches.push_back(t.gpuBatchMs); hosts.push_back(t.hostMs); }
            }
            csv.flush(); if (!csv) throw std::runtime_error("Cannot write benchmark CSV");
            auto report=[](const char* name,std::vector<double> values) {
                std::sort(values.begin(),values.end());
                auto percentile=[&](double p) { return values[size_t(std::ceil(p*values.size()))-1]; };
                std::cout << name << ": p50_ms=" << percentile(.5) << "; p95_ms=" << percentile(.95) << '\n';
            };
            report("Shader sum",shaders); report("GPU batch including copies",batches); report("Host including CPU validation",hosts);
            std::cout << "Synthetic serialized validation harness; excludes neural inference and game integration. Not full-pipeline timing.\n";
            return 0;
        }
        TemporalGpu first(context,filter,outputHistory), second(context,filter,outputHistory);
        auto run=[&](int instance,const tsr::FrameInput& frame,const char* label) {
            auto& target=instance==0 ? first : second;
            const bool stable=target.initialized && tsr::SameSize(target.previous.renderSize,frame.parameters.renderSize)
                && tsr::SameSize(target.previous.outputSize,frame.parameters.outputSize);
            const auto before=target.resourceCreations;
            auto result=target.Run(frame,label);
            if (stable && target.resourceCreations!=before)
                throw std::runtime_error("Same-size frame allocated new GPU resources");
            return result;
        };
        if (renderSet) {
            std::cout << "Configured render: " << render.width << 'x' << render.height << "; output: " << output.width << 'x' << output.height << '\n';
            tsr::ConfiguredResolutionCases(run,render,output);
        }
        else if (outputHistory) tsr::OutputHistoryCases(run,full);
        else tsr::TemporalCases(run,full);
        if (!visualDirectory.empty()) {
            TemporalGpu visual(context,filter,outputHistory);
            std::filesystem::create_directories(visualDirectory);
            std::ofstream description(visualDirectory/"reconstruction.txt");
            description << (outputHistory ? "output-grid sample history" : (filter==tsr::Reconstruction::Bilinear ? "bilinear" : "Catmull-Rom clamped")) << '\n';
            if (!description) throw std::runtime_error("Cannot write reconstruction metadata");
            tsr::VisualSequence([&](const tsr::FrameInput& frame) { return visual.Run(frame,"visual sequence"); },visualDirectory);
        }
        std::cout << "Temporal baseline with explicit previous-surface depth; no neural model, game integration or performance claim.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
