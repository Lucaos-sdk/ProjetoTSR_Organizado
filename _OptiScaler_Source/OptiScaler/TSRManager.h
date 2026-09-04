#pragma once
#include <d3d12.h>
#include "TSREngine.h"

class TSRManager {
private:
    TSREngine m_tsrEngine;
    ID3D12Resource* m_packedInput = nullptr;
    ID3D12Resource* m_warpedHistory = nullptr;
    ID3D12Resource* m_confidenceOutput = nullptr;
    ID3D12Resource* m_newHistoryOutput = nullptr;
    ID3D12Resource* m_coeffsOutput = nullptr;
    bool m_initialized = false;

    // Função interna que aloca os buffers de VRAM na GPU
    ID3D12Resource* CriarBufferGPU(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format, bool permiteEscrita) {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = permiteEscrita ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
        ID3D12Resource* resource = nullptr;
        
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&resource)
        );

        return resource;
    }

public:
    // Inicializa o ONNX Runtime / DirectML e aloca os buffers
    bool Inicializar(ID3D12Device* device, ID3D12CommandQueue* commandQueue, const wchar_t* pathModelo, UINT largura, UINT altura) {
        if (!m_tsrEngine.Initialize(device, commandQueue, pathModelo)) {
            return false;
        }

        m_packedInput = CriarBufferGPU(device, largura, altura, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        m_warpedHistory = CriarBufferGPU(device, largura, altura, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        m_confidenceOutput = CriarBufferGPU(device, largura, altura, DXGI_FORMAT_R16_FLOAT, true);
        m_newHistoryOutput = CriarBufferGPU(device, largura, altura, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        m_coeffsOutput = CriarBufferGPU(device, largura, altura, DXGI_FORMAT_R16G16B16A16_FLOAT, true);

        m_initialized = true;
        return true;
    }

    // Gerencia barreiras de recursos e dispara a inferência
    void ExecutarFrame(ID3D12GraphicsCommandList* cmdList) {
        if (!m_initialized || cmdList == nullptr) return;

        D3D12_RESOURCE_BARRIER barriers[2] = {};
        
        // Transição de Entrada (Leitura)
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_packedInput;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        // Transição de Saída (Escrita - UAV)
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_newHistoryOutput;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        cmdList->ResourceBarrier(2, barriers);

        m_tsrEngine.Dispatch(
            cmdList,
            m_packedInput,
            m_warpedHistory,
            m_confidenceOutput,
            m_newHistoryOutput,
            m_coeffsOutput
        );
    }
};