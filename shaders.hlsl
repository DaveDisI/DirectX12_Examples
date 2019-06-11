struct PSInput{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float2 position : POSITION, float4 color : COLOR){
    PSInput result;

    result.position = float4(position, 0.0, 1.0);
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return input.color;
}
