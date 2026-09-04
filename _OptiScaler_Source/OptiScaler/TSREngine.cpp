#include "pch.h"
#include "TSREngine.h"
#include <iostream>

TSREngine::TSREngine() {}
TSREngine::~TSREngine() {}

bool TSREngine::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue, const wchar_t* modelPath) {
    try {
        // 1. Configurações de otimização da sessão
        m_sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        // 2. Anexa o DirectML Execution Provider à GPU DirectX 12
        OrtStatus* status = OrtSessionOptionsAppendExecutionProvider_DML(m_sessionOptions, 0);
        if (status != nullptr) {
            std::cerr << "[TSR Engine] Falha ao anexar o DirectML Execution Provider!" << std::endl;
            return false;
        }

        // 3. Carrega o grafo ONNX
        m_session = std::make_unique<Ort::Session>(m_ortEnv, modelPath, m_sessionOptions);
        
        // 4. Cria o objeto de I/O Binding
        m_ioBinding = std::make_unique<Ort::IoBinding>(*m_session);

        std::cout << "[TSR Engine] DirectML inicializado com sucesso no DX12." << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[TSR Engine] Erro de inicialização: " << e.what() << std::endl;
        return false;
    }
}

bool TSREngine::Dispatch(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* packedInputBuffer,
    ID3D12Resource* warpedHistoryBuffer,
    ID3D12Resource* confidenceOutputBuffer,
    ID3D12Resource* newHistoryOutputBuffer,
    ID3D12Resource* coeffsOutputBuffer
) {
    try {
        Ort::MemoryInfo dmlMemoryInfo("DML", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeDefault);

        // Converte os endereços virtuais de GPU (UINT64) em ponteiros (void*) exigidos pela API C++ do ONNX Runtime
        void* packedInputGpuPtr   = reinterpret_cast<void*>(packedInputBuffer->GetGPUVirtualAddress());
        void* warpedHistoryGpuPtr = reinterpret_cast<void*>(warpedHistoryBuffer->GetGPUVirtualAddress());
        void* confidenceGpuPtr    = reinterpret_cast<void*>(confidenceOutputBuffer->GetGPUVirtualAddress());
        void* newHistoryGpuPtr    = reinterpret_cast<void*>(newHistoryOutputBuffer->GetGPUVirtualAddress());
        void* coeffsGpuPtr        = reinterpret_cast<void*>(coeffsOutputBuffer->GetGPUVirtualAddress());

        // Quantidade total de elementos por tensor (N * C * H * W)
        size_t packedInputElemCount = 1 * 16 * 540 * 960;
        size_t historyElemCount     = 1 * 8  * 540 * 960;
        size_t confidenceElemCount  = 1 * 1  * 540 * 960;
        size_t coeffsElemCount      = 1 * 12 * 540 * 960;

        // --- ENTRADAS ---
        Ort::Value inputTensor = Ort::Value::CreateTensor(
            dmlMemoryInfo,
            packedInputGpuPtr,
            packedInputElemCount,
            m_packedInputShape.data(),
            m_packedInputShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
        );

        Ort::Value historyTensor = Ort::Value::CreateTensor(
            dmlMemoryInfo,
            warpedHistoryGpuPtr,
            historyElemCount,
            m_historyShape.data(),
            m_historyShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
        );

        m_ioBinding->BindInput("packed_input", inputTensor);
        m_ioBinding->BindInput("warped_history", historyTensor);

        // --- SAÍDAS ---
        Ort::Value confidenceTensor = Ort::Value::CreateTensor(
            dmlMemoryInfo,
            confidenceGpuPtr,
            confidenceElemCount,
            m_confidenceShape.data(),
            m_confidenceShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
        );

        Ort::Value newHistoryTensor = Ort::Value::CreateTensor(
            dmlMemoryInfo,
            newHistoryGpuPtr,
            historyElemCount,
            m_historyShape.data(),
            m_historyShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
        );

        Ort::Value coeffsTensor = Ort::Value::CreateTensor(
            dmlMemoryInfo,
            coeffsGpuPtr,
            coeffsElemCount,
            m_coeffsShape.data(),
            m_coeffsShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
        );

        m_ioBinding->BindOutput("confidence_mask", confidenceTensor);
        m_ioBinding->BindOutput("new_history", newHistoryTensor);
        m_ioBinding->BindOutput("reconstruction_coeffs", coeffsTensor);

        // Executa a inferência síncrona DirectML no DirectX 12
        m_session->Run(Ort::RunOptions{ nullptr }, *m_ioBinding);

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[TSR Engine] Erro ao disparar inferência DirectML: " << e.what() << std::endl;
        return false;
    }
}