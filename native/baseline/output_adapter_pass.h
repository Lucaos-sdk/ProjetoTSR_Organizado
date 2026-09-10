#pragma once
#include "dx12_context.h"
#include "texture_region.h"
#include "output_adapter_shader.h"
class OutputAdapterPass {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
public:
    explicit OutputAdapterPass(ID3D12Device* device) {
        if (!device) throw std::invalid_argument("Output adapter requires a device");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        D3D12_ROOT_PARAMETER params[3]{};
        for (UINT i=0; i<2; ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i].DescriptorTable = {1, &ranges[i]};
        }
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants = {0,0,6};
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 3;
        desc.pParameters = params;
        ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "Output adapter root serialization");
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "Output adapter root");
        const char* shader=kOutputAdapterShader;
        const HRESULT hr = D3DCompile(shader, std::strlen(shader), "OutputAdapter.hlsl", nullptr, nullptr,
            "main", "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &blob, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr, "Output adapter shader");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get();
        pso.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline)), "Output adapter pipeline");
    }
    // Copy linear RGBA to a caller-provided typed UAV (RGBA32F or RGBA16F).
    // No allocation, CPU readback, submission or state transitions. Caller must
    // supply actual source/destination dimensions, finite values and GPU lifetime.
    void Record(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,
                D3D12_GPU_DESCRIPTOR_HANDLE srvs,D3D12_GPU_DESCRIPTOR_HANDLE uavs,
                const tsr::OutputRegion& region,tsr::Size sourceSize,tsr::Size destinationSize) const {
        tsr::ValidateRegion(sourceSize,region.size,region.sourceX,region.sourceY);
        tsr::ValidateRegion(destinationSize,region.size,region.destinationX,region.destinationY);
        if (!list || !heap) throw std::invalid_argument("Missing output adapter bindings");
        ID3D12DescriptorHeap* heaps[]={heap};
        list->SetDescriptorHeaps(1,heaps);
        list->SetPipelineState(pipeline.Get()); list->SetComputeRootSignature(root.Get());
        list->SetComputeRootDescriptorTable(0,srvs); list->SetComputeRootDescriptorTable(1,uavs);
        list->SetComputeRoot32BitConstants(2,6,&region,0);
        list->Dispatch((region.size.width+7)/8,(region.size.height+7)/8,1);
    }
};
