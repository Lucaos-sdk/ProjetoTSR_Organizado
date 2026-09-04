// PrePack.hlsl - Prepara os buffers do jogo (1080p) para a IA (540p)

// Entradas do Jogo (1080p)
Texture2D<float4> g_GameColor       : register(t0); // RGB Linear HDR
Texture2D<float>  g_GameDepth       : register(t1); // Depth Buffer
Texture2D<float2> g_GameMotionVec   : register(t2); // Motion Vectors (UV space)

SamplerState g_LinearSampler        : register(s0);

// Saída para a IA (540p - 16 canais divididos em texturas de 4 canais)
RWTexture2D<float4> g_PackedOutput0 : register(u0); // YCoCg_Log (Y, Co, Cg, Depth)
RWTexture2D<float4> g_PackedOutput1 : register(u1); // MotionVectors (MV_x, MV_y, Jitter_x, Jitter_y)

// Função auxiliar: Converte RGB Linear para YCoCg-R Log
float3 RGB_To_YCoCg_Log(float3 rgb)
{
    float y  = 0.25f * rgb.r + 0.5f * rgb.g + 0.25f * rgb.b;
    float co = 0.5f  * rgb.r - 0.5f * rgb.b;
    float cg = -0.25f* rgb.r + 0.5f * rgb.g - 0.25f * rgb.b;
    
    // Compressão de luminância para evitar perdas em HDR
    float y_log = log1p(max(0.0f, y));
    return float3(y_log, co, cg);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos540p = dispatchThreadID.xy;
    uint width540p = 960;
    uint height540p = 540;

    if (pos540p.x >= width540p || pos540p.y >= height540p)
        return;

    // Coordenada UV normalizada para amostragem com interpolação bilinear
    float2 uv = (float2(pos540p) + 0.5f) / float2(width540p, height540p);

    // 1. Amostra e converte a cor para YCoCg-R Log
    float3 rawColor = g_GameColor.SampleLevel(g_LinearSampler, uv, 0).rgb;
    float3 ycocgLog = RGB_To_YCoCg_Log(rawColor);

    // 2. Amostra a profundidade
    float depth = g_GameDepth.SampleLevel(g_LinearSampler, uv, 0);

    // 3. Amostra os Vetores de Movimento
    float2 mv = g_GameMotionVec.SampleLevel(g_LinearSampler, uv, 0);

    // Escreve as saídas organizadas para o DirectML consumir
    g_PackedOutput0[pos540p] = float4(ycocgLog, depth);
    g_PackedOutput1[pos540p] = float4(mv, 0.0f, 0.0f);
}