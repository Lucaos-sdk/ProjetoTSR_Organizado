#pragma once
#include "dx12_context.h"
#include "input_adapter.h"
#include "input_adapter_shader.h"
class InputAdapterPass {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
public:
    explicit InputAdapterPass(ID3D12Device* device) {
        if (!device) throw std::invalid_argument("Input adapter requires a device");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 3;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 2;
        D3D12_ROOT_PARAMETER params[3]{};
        for (UINT i=0; i<2; ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i].DescriptorTable = {1, &ranges[i]};
        }
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants = {0,0,16};
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 3;
        desc.pParameters = params;
        ComPtr<ID3DBlob> blob, errors;
        Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "Input adapter root serialization");
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "Input adapter root");
        const char* shader=kInputAdapterShader;
        const HRESULT hr = D3DCompile(shader, std::strlen(shader), "InputAdapter.hlsl", nullptr, nullptr,
            "main", "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &blob, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr, "Input adapter shader");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get();
        pso.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline)), "Input adapter pipeline");
    }
    // Caller owns states, dimensions, formats, descriptors and GPU lifetime.
    // Input SRVs share sourceSize and origin; packed outputs are render-sized.
    // Output UAVs: RGBA32F color and packed geometry. No submission/copies/waits.
    void Record(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,
                D3D12_GPU_DESCRIPTOR_HANDLE srvs,D3D12_GPU_DESCRIPTOR_HANDLE uavs,
                const tsr::InputConversion& parameters,tsr::Size sourceSize) const {
        tsr::ValidateConversion(parameters);
        tsr::ValidateRegion(sourceSize,parameters.size,parameters.originX,parameters.originY);
        if (!list || !heap) throw std::invalid_argument("Missing input adapter bindings");
        ID3D12DescriptorHeap* heaps[]={heap};
        list->SetDescriptorHeaps(1,heaps);
        list->SetPipelineState(pipeline.Get()); list->SetComputeRootSignature(root.Get());
        list->SetComputeRootDescriptorTable(0,srvs); list->SetComputeRootDescriptorTable(1,uavs);
        list->SetComputeRoot32BitConstants(2,16,&parameters,0);
        list->Dispatch((parameters.size.width+7)/8,(parameters.size.height+7)/8,1);
    }
};
