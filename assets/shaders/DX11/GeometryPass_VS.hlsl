struct VS_INPUT
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord: TEXCOORD;
    float3 tangent: TANGENT;
};

cbuffer ConstantBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 normalMatrix;
};

struct VS_OUTPUT
{
    float4 pos: SV_POSITION;
    float3 fragPos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord: TEXCOORD;
    float3 tangent: TANGENT;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 newPosition = float4(input.pos, 1.0f);
    newPosition = mul(newPosition, model);
    output.fragPos = newPosition;
    newPosition = mul(newPosition, view);
    newPosition = mul(newPosition, projection);

    output.pos = newPosition;
    output.texCoord = input.texCoord;

    output.normal = normalize(mul(input.normal, (float3x3)normalMatrix));
    output.tangent = normalize(mul(input.tangent, (float3x3)normalMatrix));
    return output;
}
