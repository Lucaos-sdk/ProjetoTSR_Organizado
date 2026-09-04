// Reconstruct4K.hlsl - Compute Shader de Reconstrução Espacial 4K
// Executa na GPU DirectX 12 dentro do OptiScaler

// Texturas de Entrada (SRV - Shader Resource Views)
Texture2D<float4> g_LowResColor        : register(t0); // Color Buffer do jogo (1080p)
Texture2D<float4> g_WarpedHistory4K    : register(t1); // Quadro anterior reprojetado (4K)
Texture2D<float>  g_ConfidenceMask     : register(t2); // Máscara da IA (540p)
Texture2D<float4> g_Coeffs0            : register(t3); // Coeficientes 0-3 da IA (540p)
Texture2D<float4> g_Coeffs1            : register(t4); // Coeficientes 4-7 da IA (540p)
Texture2D<float4> g_Coeffs2            : register(t5); // Coeficientes 8-11 da IA (540p)

// Samplers de Textura
SamplerState g_LinearSampler           : register(s0);

// Textura de Saída (UAV - Unordered Access View)
RWTexture2D<float4> g_OutputColor4K    : register(u0); // Buffer final exibido na tela (4K)

[numthreads(8, 8, 1)]
void main(uint33 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos4K = dispatchThreadID.xy;
    
    // Dimensões do alvo (3840x2160)
    uint width4K, height4K;
    g_OutputColor4K.GetDimensions(width4K, height4K);

    if (pos4K.x >= width4K || pos4K.y >= height4K)
        return;

    // Coordenada UV normalizada [0.0, 1.0] para amostragem
    float2 uv = (float2(pos4K) + 0.5f) / float2(width4K, height4K);

    // 1. Amostra a imagem em 1080p usando interpolação bilinear da GPU
    float4 currentFrameColor = g_LowResColor.SampleLevel(g_LinearSampler, uv, 0);

    // 2. Amostra a imagem histórica reprojetada em 4K
    float4 historyFrameColor = g_WarpedHistory4K.SampleLevel(g_LinearSampler, uv, 0);

    // 3. Amostra a máscara de confiança (0.0 = descartar histórico, 1.0 = manter histórico)
    float confidence = g_ConfidenceMask.SampleLevel(g_LinearSampler, uv, 0);

    // 4. Amostra os coeficientes de correção espacial gerados pelo modelo neural
    float4 c0 = g_Coeffs0.SampleLevel(g_LinearSampler, uv, 0);
    float4 c1 = g_Coeffs1.SampleLevel(g_LinearSampler, uv, 0);

    // 5. Aplica a reconstrução residual com a correção da IA
    float3 spatialRefinement = currentFrameColor.rgb + (c0.rgb * 0.1f);
    
    // 6. Mescla o frame atual corrigido com o histórico baseado na confiança da IA
    float3 finalRGB = lerp(spatialRefinement, historyFrameColor.rgb, confidence);

    // Garante que não existam valores negativos de cor
    g_OutputColor4K[pos4K] = float4(max(0.0f, finalRGB), 1.0f);
}