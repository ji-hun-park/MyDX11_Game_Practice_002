// --------------------------------------------------------
// Constant Buffer (cbuffer)
// CPU에서 매 프레임 갱신해서 보내줄 데이터(행렬)입니다.
// register(b0): 0번 슬롯(Slot)을 사용하겠다는 뜻
// --------------------------------------------------------
cbuffer ConstantBuffer : register(b0)
{
    matrix World;       // 물체의 위치/회전/크기
    matrix View;        // 카메라의 위치/각도
    matrix Projection;  // 카메라의 렌즈(원근감)
    float4 vLightDir;   // 빛이 오는 방향 (x, y, z, unused)
    float4 vLightColor; // 빛의 색상 (r, g, b, a)
}

// 1. 입력 데이터 구조체 (C++에서 보낼 데이터와 모양이 같아야 함)
struct VS_INPUT
{
    float4 Pos : POSITION;  // 정점 위치 (x, y, z, 1.0)
    // float4 Color : COLOR; 기존 정점 색상 (r, g, b, a)
    float3 Normal : NORMAL; // 색상 대신 법선 벡터를 받음 (x, y, z)
};

// 2. 버텍스 셰이더와 픽셀 셰이더 사이의 통신 구조체
struct PS_INPUT
{
    float4 Pos : SV_POSITION;  // 중요: 화면상 좌표 (System Value)
    // float4 Color : COLOR; 색상
    float3 Normal : TEXCOORD0; // 픽셀 셰이더로 법선을 넘겨줌
};

// --------------------------------------------------------
// Vertex Shader (VS)
// 역할: 3D 공간의 점을 화면 좌표로 변환
// --------------------------------------------------------
PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output;
    
    // 위치 변환
    // 입력받은 정점(Pos)에 W-V-P 행렬을 차례대로 곱합니다.
    // 순서 중요: 정점 * 월드 * 뷰 * 프로젝션
    
    float4 pos = input.Pos;
    
    // 1. 월드 변환 (로컬 -> 월드 공간)
    pos = mul(pos, World);
    
    // 2. 뷰 변환 (월드 -> 카메라 공간)
    pos = mul(pos, View);
    
    // 3. 프로젝션 변환 (카메라 -> 클립 공간/화면)
    pos = mul(pos, Projection);
    
    output.Pos = pos;
    
    // 기존: 색상 그대로 넘김 output.Color = input.Color;
    // 2. 법선 벡터 변환 (중요!)
    // 물체가 회전하면 법선도 같이 회전해야 합니다.
    // (World 행렬의 회전 성분만 적용)
    output.Normal = mul(input.Normal, (float3x3) World);
    
    return output;
}

// --------------------------------------------------------
// Pixel Shader (PS)
// 역할: 픽셀 하나의 최종 색상을 결정 (Rasterizer가 보간해준 데이터 사용)
// --------------------------------------------------------
float4 PS(PS_INPUT input) : SV_Target
{
    // 기존: 정점 셰이더에서 넘어온 색상을 그대로 화면에 찍습니다. return input.Color;
    
    // 1. 법선 정규화 (보간 과정에서 길이가 변할 수 있으므로 다시 1로 만듦)
    float3 normal = normalize(input.Normal);
    
    // 2. 빛의 방향 (역방향 필요 없음, 우리가 빛이 나가는 방향을 정의할 것임)
    // 보통 빛 계산에서는 '표면에서 빛으로 향하는 벡터'를 씁니다.
    // 여기서는 편의상 vLightDir을 '빛이 표면을 때리는 방향'으로 가정하고 반대로 계산하겠습니다.
    float3 lightDir = -normalize(vLightDir.xyz);

    // 3. 램버트(Lambert) 조명 계산 (내적)
    // saturate: 0.0 ~ 1.0 사이로 자름 (음수가 나오면 검은색 처리)
    float diffuseFactor = saturate(dot(normal, lightDir));
    
    // 4. 최종 색상 = 조명 밝기 * 빛의 색상
    // (물체 고유색이 있다면 여기서 곱해줍니다. 지금은 흰색 물체라 가정)
    return diffuseFactor * vLightColor;
}