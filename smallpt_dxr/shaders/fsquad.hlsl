struct VSInput
{
    float2 position : POSITION;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_Target;
};

cbuffer FSQuadCB : register(b0)
{
    int frame_count;
}

Texture2D tex : register(t0);
SamplerState the_sampler;

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position/* + xyoffset_alpha.xy*/, 0.0f, 1.0f);
    output.texcoord = input.texcoord;
    return output;
}

float3 toInt(float3 rgb)
{
    return pow(clamp(rgb, 0, 1), 1 / 2.2);
}

PSOutput PSMain(VSOutput input)
{
    PSOutput output;
    float w = (frame_count <= 0) ? 1 : 1.0f / frame_count;
    output.color = tex.Sample(the_sampler, input.texcoord.xy) * w;
    output.color.xyz = toInt(output.color.xyz);
    output.color.a = 1.0;
    return output;
}