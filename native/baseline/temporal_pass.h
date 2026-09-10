#pragma once
#include "dx12_context.h"
#include "temporal_shader.h"
#include "output_history_shader.h"

// GPU command recording only. No queue, submission, fence wait, upload,
// readback, CPU reference, resource allocation or history mutation in Record.
// The caller owns resource states and lifetime, descriptors and frame sequencing.
class TemporalPass {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    const bool outputHistory;
public:
    struct Bindings {
        ID3D12DescriptorHeap* heap;
        D3D12_GPU_DESCRIPTOR_HANDLE srvs; // four contiguous RGBA32F Texture2D SRVs
        D3D12_GPU_DESCRIPTOR_HANDLE uavs; // two contiguous RGBA32F Texture2D UAVs
    };
    explicit TemporalPass(ID3D12Device* device,bool useOutputHistory) : outputHistory(useOutputHistory) {
        if (!device) throw std::invalid_argument("TemporalPass requires a device");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 4;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 2;
        D3D12_ROOT_PARAMETER params[3]{};
        for (UINT i=0; i<2; ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i].DescriptorTable = {1, &ranges[i]};
        }
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants = {0,0,12};
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 3;
        desc.pParameters = params;
        ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "Temporal root serialization");
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "Temporal root");
        const char* shader=outputHistory ? kOutputHistoryShader : kTemporalShader;
        const HRESULT hr = D3DCompile(shader, std::strlen(shader), outputHistory ? "OutputHistory.hlsl" : "Temporal.hlsl", nullptr, nullptr,
            "main", "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &blob, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr, "Temporal shader");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get();
        pso.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline)), "Temporal pipeline");
    }
    float ValidateParameters(const tsr::FrameParameters& p,const tsr::FrameParameters& previous,bool reuse) const {
        tsr::Count(p.renderSize); tsr::Count(p.outputSize);
        if (outputHistory && (p.outputSize.width<p.renderSize.width || p.outputSize.height<p.renderSize.height))
            throw std::invalid_argument("Output history does not support downsampling");
        if (reuse && !tsr::CanReuse(true,previous,p))
            throw std::invalid_argument("TemporalPass cannot reuse incompatible history");
        if (!std::isfinite(p.preExposure) || p.preExposure<=0 ||
            (reuse && (!std::isfinite(previous.preExposure) || previous.preExposure<=0)))
            throw std::invalid_argument("Invalid temporal exposure");
        for (unsigned i=0;i<2;++i) {
            if (!std::isfinite(p.jitterPixels[i]) || std::abs(p.jitterPixels[i])>.5f ||
                (reuse && (!std::isfinite(previous.jitterPixels[i]) || std::abs(previous.jitterPixels[i])>.5f)))
                throw std::invalid_argument("Invalid temporal jitter");
        }
        const float exposure=reuse ? p.preExposure/previous.preExposure : 1.f;
        if (!std::isfinite(exposure)) throw std::invalid_argument("Temporal exposure overflow");
        if(p.frameIndex==0 && !p.reset)throw std::invalid_argument("First frame requires reset");
        return exposure;
    }
    // SRVs: current color; packed (current Z, motion X/Y, previous-surface Z);
    // previous color; previous geometry. UAVs: next color; next geometry.
    // Inputs must be NON_PIXEL_SHADER_RESOURCE and outputs UNORDERED_ACCESS.
    // Resources, heap, PSO and root signature must survive GPU completion.
    // Record replaces compute pipeline/root bindings and the descriptor heap.
    // Optional queries reserve [firstQuery, firstQuery+1] for dispatch timing.
    void Record(ID3D12GraphicsCommandList* list,const Bindings& bindings,
                const tsr::FrameParameters& p,const tsr::FrameParameters& previous,
                bool reuse,ID3D12QueryHeap* queries=nullptr,UINT firstQuery=0) const {
        if (!list || !bindings.heap) throw std::invalid_argument("TemporalPass requires a command list and heap");
        const float exposure=ValidateParameters(p,previous,reuse);
        const auto historySize=outputHistory ? p.outputSize : p.renderSize;
        ID3D12DescriptorHeap* heaps[]={bindings.heap};
        list->SetDescriptorHeaps(1,heaps);
        list->SetPipelineState(pipeline.Get());
        list->SetComputeRootSignature(root.Get());
        list->SetComputeRootDescriptorTable(0,bindings.srvs);
        list->SetComputeRootDescriptorTable(1,bindings.uavs);
        struct Constants { UINT w,h,reuse,pad; float jx,jy,exposure,weight; UINT ow,oh,pad2,pad3; } constants{
            p.renderSize.width,p.renderSize.height,UINT(reuse),0,
            outputHistory ? p.jitterPixels[0] : (reuse ? p.jitterPixels[0]-previous.jitterPixels[0] : 0),
            outputHistory ? p.jitterPixels[1] : (reuse ? p.jitterPixels[1]-previous.jitterPixels[1] : 0),
            exposure,outputHistory ? .9f : .75f,p.outputSize.width,p.outputSize.height,0,0};
        static_assert(sizeof(Constants)==48);
        list->SetComputeRoot32BitConstants(2,12,&constants,0);
        if (queries) list->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,firstQuery);
        list->Dispatch((historySize.width+7)/8,(historySize.height+7)/8,1);
        if (queries) list->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,firstQuery+1);
    }
};
