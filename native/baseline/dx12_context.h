#pragma once
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include "reference.h"
#include "frame_contract.h"
#include "shader_source.h"
using Microsoft::WRL::ComPtr;

static void Check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message << operation << " failed: HRESULT 0x" << std::hex << unsigned(hr);
        throw std::runtime_error(message.str());
    }
}
struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Event() { if (!handle) throw std::runtime_error("CreateEvent failed"); }
    ~Event() { CloseHandle(handle); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, size_t bytes, D3D12_HEAP_TYPE heap,
                                    D3D12_RESOURCE_STATES state, bool uav = false) {
    D3D12_HEAP_PROPERTIES props{};
    props.Type = heap;
    props.CreationNodeMask = props.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                         IID_PPV_ARGS(&resource)), "CreateCommittedResource");
    return resource;
}
static void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
}
static ComPtr<ID3D12Resource> Texture(ID3D12Device* device, tsr::Size size, bool uav) {
    tsr::Count(size);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size.width;
    desc.Height = size.height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)), "Create texture");
    return resource;
}
struct TextureTransfer {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    TextureTransfer(ID3D12Device* device, ID3D12Resource* texture) {
        const auto desc = texture->GetDesc();
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    }
    D3D12_TEXTURE_COPY_LOCATION BufferLocation(ID3D12Resource* buffer) const {
        D3D12_TEXTURE_COPY_LOCATION location{};
        location.pResource = buffer;
        location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        location.PlacedFootprint = footprint;
        return location;
    }
    static D3D12_TEXTURE_COPY_LOCATION TextureLocation(ID3D12Resource* texture) {
        D3D12_TEXTURE_COPY_LOCATION location{};
        location.pResource = texture;
        location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        return location;
    }
};
struct Context {
    bool textures;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12InfoQueue> info;
    static void ListAdapters() {
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"List adapters factory");
        for (UINT index=0;;++index) {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result=factory->EnumAdapters1(index,&adapter);
            if (result==DXGI_ERROR_NOT_FOUND) break;
            Check(result,"List adapter");
            DXGI_ADAPTER_DESC1 desc{};
            Check(adapter->GetDesc1(&desc),"Adapter description");
            const bool compatible=SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),nullptr));
            std::wcout << index << L": " << desc.Description << L"; DX12=" << (compatible?L"yes":L"no")
                       << L"; " << ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)?L"software":L"hardware") << '\n';
        }
    }
    Context(bool warp, bool debug, bool useTextures, int adapterIndex=-1) : textures(useTextures) {
        if (warp && adapterIndex>=0) throw std::invalid_argument("Use --warp or --adapter, not both");
        if (debug) {
            ComPtr<ID3D12Debug> layer;
            Check(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)), "Debug layer unavailable: install Graphics Tools");
            layer->EnableDebugLayer();
        }
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
        ComPtr<IDXGIAdapter1> selected;
        if (warp) {
            Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&selected)), "EnumWarpAdapter");
        } else {
            // Explicit hardware mode: never silently falls back to software.
            for (UINT index = 0;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT result = factory->EnumAdapters1(index, &candidate);
                if (result == DXGI_ERROR_NOT_FOUND) break;
                Check(result, "EnumAdapters1");
                if (adapterIndex>=0 && index!=UINT(adapterIndex)) continue;
                DXGI_ADAPTER_DESC1 desc{};
                Check(candidate->GetDesc1(&desc), "GetDesc1");
                if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                               __uuidof(ID3D12Device), nullptr))) {
                    selected = candidate;
                    break;
                }
            }
            if (!selected) throw std::runtime_error("Requested/default DX12 hardware adapter unavailable or incompatible; no fallback was selected");
        }
        DXGI_ADAPTER_DESC1 adapterDesc{};
        Check(selected->GetDesc1(&adapterDesc), "GetDesc1");
        std::wcout << L"Adapter: " << adapterDesc.Description << (warp ? L" [WARP]\n" : L" [hardware]\n");
        Check(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
        if (debug) Check(device.As(&info), "ID3D12InfoQueue");
        if (textures) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_R32G32B32A32_FLOAT};
            Check(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)), "Texture format support");
            const UINT required = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
            if ((support.Support1 & required) != required ||
                !(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
                throw std::runtime_error("RGBA32F texture load/UAV store unavailable");
        }
        std::cout << "Resource path: " << (textures ? "Texture2D RGBA32F" : "structured Float4 buffers") << '\n';
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[0].Descriptor.ShaderRegister = 0;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[1].Descriptor.ShaderRegister = 0;
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        if (textures) {
            for (UINT i = 0; i < 2; ++i) {
                ranges[i].RangeType = i == 0 ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                ranges[i].NumDescriptors = 1;
                params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                params[i].DescriptorTable = {1, &ranges[i]};
            }
        }
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants = {0, 0, 7};
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = 3;
        signature.pParameters = params;
        ComPtr<ID3DBlob> serialized, errors;
        Check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "SerializeRootSignature");
        Check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                           IID_PPV_ARGS(&root)), "CreateRootSignature");
        ComPtr<ID3DBlob> shader;
        const D3D_SHADER_MACRO macros[] = {{"TSR_TEXTURES", "1"}, {nullptr, nullptr}};
        const auto hr = D3DCompile(kBilinearShader, std::strlen(kBilinearShader), "Bilinear.hlsl", textures ? macros : nullptr, nullptr,
                                  "main", "cs_5_1", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0,
                                  &shader, &errors);
        if (FAILED(hr) && errors) std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        Check(hr, "D3DCompile");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.pRootSignature = root.Get();
        pipelineDesc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)), "CreateComputePipelineState");
    }
    void CheckDebugMessages() {
        if (!info) return;
        bool failed = false;
        for (UINT64 i=0; i<info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
            SIZE_T bytes = 0;
            Check(info->GetMessage(i, nullptr, &bytes), "GetMessage size");
            std::vector<unsigned char> storage(bytes);
            auto message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(info->GetMessage(i, message, &bytes), "GetMessage");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
                std::cerr << message->pDescription << '\n';
                failed = true;
            }
        }
        info->ClearStoredMessages();
        if (failed) throw std::runtime_error("D3D12 debug layer reported warning/error");
    }
    void Run(tsr::Size srcSize, tsr::Size dstSize) {
        const auto frame = tsr::FrameFixture(srcSize, dstSize);
        tsr::ValidateFrame(frame);
        // Spatial stage consumes only color; temporal planes are validated on CPU.
        const auto& source = frame.color;
        const size_t inputBytes = source.size()*sizeof(tsr::Pixel);
        const size_t outputBytes = tsr::Count(dstSize)*sizeof(tsr::Pixel);
        auto input = textures ? Texture(device.Get(), srcSize, false) :
            Buffer(device.Get(), inputBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        auto output = textures ? Texture(device.Get(), dstSize, true) :
            Buffer(device.Get(), outputBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, true);
        // Footprints also work for buffers; only the texture path uses their row pitch.
        const TextureTransfer inputTransfer(device.Get(), input.Get()), outputTransfer(device.Get(), output.Get());
        const size_t uploadBytes = textures ? size_t(inputTransfer.bytes) : inputBytes;
        const size_t readbackBytes = textures ? size_t(outputTransfer.bytes) : outputBytes;
        auto upload = Buffer(device.Get(), uploadBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto readback = Buffer(device.Get(), readbackBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        ComPtr<ID3D12DescriptorHeap> descriptors;
        UINT descriptorStride = 0;
        if (textures) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heap.NumDescriptors = 2;
            heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors)), "Create texture descriptors");
            descriptorStride = device->GetDescriptorHandleIncrementSize(heap.Type);
            auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(input.Get(), &srv, handle);
            handle.ptr += descriptorStride;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = srv.Format;
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, handle);
        }
        auto timings = Buffer(device.Get(), 2*sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0,0};
        Check(upload->Map(0, &noRead, &mapped), "Map upload");
        if (textures) {
            // Poison padding so an incorrect tightly-packed upload cannot pass silently.
            std::memset(mapped, 0xcd, uploadBytes);
            for (UINT y = 0; y < srcSize.height; ++y)
                std::memcpy(static_cast<unsigned char*>(mapped) + inputTransfer.footprint.Offset +
                            size_t(y) * inputTransfer.footprint.Footprint.RowPitch,
                            source.data() + size_t(y) * srcSize.width, size_t(srcSize.width) * sizeof(tsr::Pixel));
        } else std::memcpy(mapped, source.data(), inputBytes);
        const D3D12_RANGE written{0,uploadBytes};
        upload->Unmap(0, &written);
        ComPtr<ID3D12CommandAllocator> allocator;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
        ComPtr<ID3D12GraphicsCommandList> list;
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&list)), "CreateCommandList");
        D3D12_QUERY_HEAP_DESC queryDesc{};
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = 2;
        ComPtr<ID3D12QueryHeap> queries;
        Check(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queries)), "CreateQueryHeap");
        Transition(list.Get(), input.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (textures) {
            const auto dst = TextureTransfer::TextureLocation(input.Get());
            const auto src = inputTransfer.BufferLocation(upload.Get());
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        } else list->CopyBufferRegion(input.Get(), 0, upload.Get(), 0, inputBytes);
        Transition(list.Get(), input.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->SetComputeRootSignature(root.Get());
        if (textures) {
            ID3D12DescriptorHeap* heaps[] = {descriptors.Get()};
            list->SetDescriptorHeaps(1, heaps);
            auto handle = descriptors->GetGPUDescriptorHandleForHeapStart();
            list->SetComputeRootDescriptorTable(0, handle);
            handle.ptr += descriptorStride;
            list->SetComputeRootDescriptorTable(1, handle);
        } else {
            list->SetComputeRootShaderResourceView(0, input->GetGPUVirtualAddress());
            list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
        }
        const UINT dims[] = {srcSize.width, srcSize.height, dstSize.width, dstSize.height, 0, 0, 0};
        list->SetComputeRoot32BitConstants(2, 7, dims, 0);
        list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        list->Dispatch((dstSize.width+7)/8, (dstSize.height+7)/8, 1);
        list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        Transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (textures) {
            const auto dst = outputTransfer.BufferLocation(readback.Get());
            const auto src = TextureTransfer::TextureLocation(output.Get());
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        } else list->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, outputBytes);
        list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timings.Get(), 0);
        Check(list->Close(), "Close command list");
        ComPtr<ID3D12Fence> fence;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        Event event;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        Check(queue->Signal(fence.Get(), 1), "Signal fence");
        Check(fence->SetEventOnCompletion(1, event.handle), "SetEventOnCompletion");
        if (WaitForSingleObject(event.handle, 30000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU completion timed out or wait failed");
        Check(device->GetDeviceRemovedReason(), "Device removed");
        const D3D12_RANGE outputRange{0,readbackBytes};
        Check(readback->Map(0, &outputRange, &mapped), "Map readback");
        std::vector<tsr::Pixel> result(tsr::Count(dstSize));
        if (textures) {
            for (UINT y = 0; y < dstSize.height; ++y)
                std::memcpy(result.data() + size_t(y) * dstSize.width,
                            static_cast<const unsigned char*>(mapped) + outputTransfer.footprint.Offset +
                            size_t(y) * outputTransfer.footprint.Footprint.RowPitch,
                            size_t(dstSize.width) * sizeof(tsr::Pixel));
        } else std::memcpy(result.data(), mapped, outputBytes);
        readback->Unmap(0, &noRead);
        const double error = tsr::Verify(result, tsr::Bilinear(source, srcSize, dstSize));
        UINT64 ticks[2]{}, frequency = 0;
        const D3D12_RANGE timeRange{0,sizeof(ticks)};
        Check(timings->Map(0, &timeRange, &mapped), "Map timestamps");
        std::memcpy(ticks, mapped, sizeof(ticks));
        timings->Unmap(0, &noRead);
        Check(queue->GetTimestampFrequency(&frequency), "GetTimestampFrequency");
        if (!frequency || ticks[1] < ticks[0]) throw std::runtime_error("Invalid timestamp results");
        std::cout << "PASS " << srcSize.width << 'x' << srcSize.height << " -> " << dstSize.width << 'x' << dstSize.height
                  << "; max_abs_error=" << error << "; dispatch_ms=" << double(ticks[1]-ticks[0])*1000.0/double(frequency) << '\n';
        CheckDebugMessages();
    }
};
