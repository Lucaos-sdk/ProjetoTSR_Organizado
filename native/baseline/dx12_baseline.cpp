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
struct Context {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12InfoQueue> info;
    Context(bool warp, bool debug) {
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
                DXGI_ADAPTER_DESC1 desc{};
                Check(candidate->GetDesc1(&desc), "GetDesc1");
                if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                               __uuidof(ID3D12Device), nullptr))) {
                    selected = candidate;
                    break;
                }
            }
            if (!selected) throw std::runtime_error("No DX12 hardware adapter available");
        }
        DXGI_ADAPTER_DESC1 adapterDesc{};
        Check(selected->GetDesc1(&adapterDesc), "GetDesc1");
        std::wcout << L"Adapter: " << adapterDesc.Description << (warp ? L" [WARP]\n" : L" [hardware]\n");
        Check(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
        if (debug) Check(device.As(&info), "ID3D12InfoQueue");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        D3D12_ROOT_PARAMETER params[3]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[0].Descriptor.ShaderRegister = 0;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[1].Descriptor.ShaderRegister = 0;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[2].Constants = {0, 0, 4};
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = 3;
        signature.pParameters = params;
        ComPtr<ID3DBlob> serialized, errors;
        Check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "SerializeRootSignature");
        Check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                           IID_PPV_ARGS(&root)), "CreateRootSignature");
        ComPtr<ID3DBlob> shader;
        const auto hr = D3DCompile(kBilinearShader, std::strlen(kBilinearShader), "Bilinear.hlsl", nullptr, nullptr,
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
        const auto source = tsr::Fixture(srcSize);
        const size_t inputBytes = source.size()*sizeof(tsr::Pixel);
        const size_t outputBytes = tsr::Count(dstSize)*sizeof(tsr::Pixel);
        auto upload = Buffer(device.Get(), inputBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto input = Buffer(device.Get(), inputBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        auto output = Buffer(device.Get(), outputBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
        auto readback = Buffer(device.Get(), outputBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto timings = Buffer(device.Get(), 2*sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0,0};
        Check(upload->Map(0, &noRead, &mapped), "Map upload");
        std::memcpy(mapped, source.data(), inputBytes);
        const D3D12_RANGE written{0,inputBytes};
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
        list->CopyBufferRegion(input.Get(), 0, upload.Get(), 0, inputBytes);
        Transition(list.Get(), input.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->SetComputeRootSignature(root.Get());
        list->SetComputeRootShaderResourceView(0, input->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
        const UINT dims[] = {srcSize.width, srcSize.height, dstSize.width, dstSize.height};
        list->SetComputeRoot32BitConstants(2, 4, dims, 0);
        list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        list->Dispatch((dstSize.width+7)/8, (dstSize.height+7)/8, 1);
        list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        Transition(list.Get(), output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, outputBytes);
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
        const D3D12_RANGE outputRange{0,outputBytes};
        Check(readback->Map(0, &outputRange, &mapped), "Map readback");
        std::vector<tsr::Pixel> result(tsr::Count(dstSize));
        std::memcpy(result.data(), mapped, outputBytes);
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
int main(int argc, char** argv) {
    try {
        bool warp = false, debug = false, full = false;
        for (int i=1; i<argc; ++i) {
            const std::string arg(argv[i]);
            if (arg == "--warp") warp = true;
            else if (arg == "--debug") debug = true;
            else if (arg == "--full") full = true;
            else throw std::invalid_argument("Usage: tsr_dx12_baseline [--warp] [--debug] [--full]");
        }
        Context context(warp, debug);
        context.Run({1,1}, {13,7});
        context.Run({2,2}, {3,3});
        context.Run({7,5}, {7,5});
        context.Run({17,9}, {37,23});
        context.Run({1,7}, {9,17});
        if (full) context.Run({1920,1080}, {3840,2160});
        std::cout << "Spatial baseline only; single-dispatch timings are not TSR/game performance.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
