#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include "pch.h"

using Microsoft::WRL::ComPtr;

class TSREngine {
public:
    TSREngine();
    ~TSREngine();

    // Inicializa a sessão ONNX Runtime vinculada à fila DirectX 12 do jogo
    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue, const wchar_t* modelPath);

    // Executa a inferência na GPU reutilizando buffers de VRAM (Zero-Copy)
    bool Dispatch(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* packedInputBuffer,
        ID3D12Resource* warpedHistoryBuffer,
        ID3D12Resource* confidenceOutputBuffer,
        ID3D12Resource* newHistoryOutputBuffer,
        ID3D12Resource* coeffsOutputBuffer
    );

private:
    Ort::Env m_ortEnv{ ORT_LOGGING_LEVEL_WARNING, "TSR_DirectML" };
    Ort::SessionOptions m_sessionOptions;
    std::unique_ptr<Ort::Session> m_session;
    std::unique_ptr<Ort::IoBinding> m_ioBinding;

    // Dimensões fixas do modelo híbrido (540p)
    const std::vector<int64_t> m_packedInputShape = { 1, 16, 540, 960 };
    const std::vector<int64_t> m_historyShape     = { 1, 8, 540, 960 };
    const std::vector<int64_t> m_confidenceShape  = { 1, 1, 540, 960 };
    const std::vector<int64_t> m_coeffsShape      = { 1, 12, 540, 960 };
};