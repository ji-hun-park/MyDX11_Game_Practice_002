#include <windows.h>
#include <wrl/client.h> // ComPtr용

// DirectX 11 관련 헤더
#include <d3d11.h>
#include <d3dcompiler.h> // 셰이더 컴파일용
#include <DirectXMath.h> // 수학 라이브러리 (행렬, 벡터)

// 라이브러리 링크 (속성 페이지에서 추가하는 대신 코드로 처리)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;      // DirectXMath 네임스페이스로 라이브러리 사용
using Microsoft::WRL::ComPtr; // ComPtr 스마트 포인터

// 전역 변수 (나중에는 클래스로 관리해야 함)
HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;

// DirectX 핵심 객체들
ComPtr<ID3D11Device> g_pd3dDevice;                  // 자원 생성 공장
ComPtr<ID3D11DeviceContext> g_pImmediateContext;    // 그리기 명령 작업자
ComPtr<IDXGISwapChain> g_pSwapChain;                // 더블 버퍼링 관리자
ComPtr<ID3D11RenderTargetView> g_pRenderTargetView; // 렌더링 타겟 (도화지)

// 쉐이더 관련 전역 변수
ComPtr<ID3D11VertexShader> g_pVertexShader; // 정점 셰이더 객체
ComPtr<ID3D11PixelShader> g_pPixelShader;   // 픽셀 셰이더 객체
ComPtr<ID3D11InputLayout> g_pVertexLayout;  // 데이터 서식 (중요!)
ComPtr<ID3D11Buffer> g_pVertexBuffer;       // 버텍스 버퍼 추가
ComPtr<ID3D11Buffer> g_pConstantBuffer;     // 상수 버퍼 객체
XMMATRIX g_World;
XMMATRIX g_View;
XMMATRIX g_Projection;

// C++용 정점 구조체
struct SimpleVertex
{
    XMFLOAT3 Pos;    // x, y, z (12 bytes) - HLSL의 float4 Pos에 매핑됨 (w는 자동 1.0)
    XMFLOAT4 Color;  // r, g, b, a (16 bytes)
};

// HLSL의 cbuffer와 바이트 크기가 맞아야 함
struct ConstantBuffer
{
    XMMATRIX mWorld;
    XMMATRIX mView;
    XMMATRIX mProjection;
};

// 윈도우 프로시저 (이벤트 처리기)
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Direct3D 초기화 함수
HRESULT InitD3D(HWND hWnd) {
    HRESULT hr = S_OK;

    // 1. 스왑 체인 설정 구조체 작성
    DXGI_SWAP_CHAIN_DESC sd = { 0 };
    sd.BufferCount = 1;                                     // 백버퍼 개수 (1개면 충분)
    sd.BufferDesc.Width = 800;                              // 해상도 너비
    sd.BufferDesc.Height = 600;                             // 해상도 높이
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;      // 색상 포맷 (RGBA 32비트)
    sd.BufferDesc.RefreshRate.Numerator = 60;               // 주사율 60Hz
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;       // "이 버퍼에 그림 그릴거임"
    sd.OutputWindow = hWnd;                                 // 그림 그릴 윈도우 핸들
    sd.SampleDesc.Count = 1;                                // 멀티샘플링(MSAA) 끄기
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;                                     // 창 모드

    // 2. 장치(Device), 컨텍스트(Context), 스왑체인(SwapChain) 동시 생성
    // D3D_FEATURE_LEVEL_11_0 을 지원하는 그래픽 카드를 요청합니다.
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);

    hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // 기본 어댑터(그래픽카드) 사용
        D3D_DRIVER_TYPE_HARDWARE,   // 하드웨어 가속 사용 (필수!)
        nullptr,
        0,                          // 플래그 (디버그 시 D3D11_CREATE_DEVICE_DEBUG 사용 가능)
        featureLevels, numFeatureLevels,
        D3D11_SDK_VERSION,
        &sd,
        g_pSwapChain.GetAddressOf(),
        g_pd3dDevice.GetAddressOf(),
        nullptr,
        g_pImmediateContext.GetAddressOf()
    );

    if (FAILED(hr)) return hr;

    // 3. 렌더 타겟 뷰(RTV) 생성
    // 스왑 체인에서 백 버퍼(그림 그릴 종이)를 가져옵니다.
    ComPtr<ID3D11Texture2D> pBackBuffer;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)pBackBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;

    // 가져온 백 버퍼를 타겟으로 뷰를 만듭니다.
    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, g_pRenderTargetView.GetAddressOf());
    if (FAILED(hr)) return hr;

    // 4. 렌더 타겟 설정 (Output Merger 단계)
    // "이제부터 이 도화지(RTV)에 그릴 거야"라고 작업자(Context)에게 지시
    g_pImmediateContext->OMSetRenderTargets(1, g_pRenderTargetView.GetAddressOf(), nullptr);

    // 5. 뷰포트(Viewport) 설정 (Rasterizer 단계)
    // 도화지의 어느 영역에 그릴지 설정 (전체 화면)
    D3D11_VIEWPORT vp;
    vp.Width = 800.0f;
    vp.Height = 600.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports(1, &vp);

    return S_OK;
}

// 셰이더 컴파일 및 생성 함수
HRESULT InitPipeline() {
    HRESULT hr = S_OK;
    ComPtr<ID3DBlob> pVSBlob = nullptr; // 컴파일된 셰이더 바이너리(기계어)
    ComPtr<ID3DBlob> pErrorBlob = nullptr;
    ComPtr<ID3DBlob> pPSBlob = nullptr;

    // 1. Vertex Shader 컴파일
    // D3DCompileFromFile(파일경로, 매크로, include, 함수이름, 셰이더버전, 플래그1, 플래그2, 결과, 에러)
    hr = D3DCompileFromFile(L"Shaders.hlsl", nullptr, nullptr, "VS", "vs_4_0", 0, 0, pVSBlob.GetAddressOf(), pErrorBlob.GetAddressOf());

    if (FAILED(hr)) {
        if (pErrorBlob) {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer()); // 에러 메시지 출력
        }
        return hr;
    }

    // 2. Vertex Shader 객체 생성
    hr = g_pd3dDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, g_pVertexShader.GetAddressOf());
    if (FAILED(hr)) return hr;

    // 3. Input Layout 생성 (매우 중요!)
    // CPU의 데이터 구조체와 GPU의 HLSL 구조체를 매칭시켜주는 '설명서'입니다.
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        // "POSITION": HLSL의 시맨틱 이름
        // 0: 시맨틱 인덱스 (POSITION0)
        // DXGI_FORMAT_R32G32B32_FLOAT: float3 (x, y, z)
        // 0: 입력 슬롯 (나중에 Vertex Buffer 넣을 때 0번 슬롯에 넣겠다는 뜻)
        // 0: 오프셋 (구조체 시작부터 0바이트 지점)
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },

        // "COLOR": HLSL의 시맨틱 이름
        // 12: 오프셋 (float3가 4byte * 3 = 12byte니까 그 다음부터 시작)
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);

    hr = g_pd3dDevice->CreateInputLayout(layout, numElements, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), g_pVertexLayout.GetAddressOf());
    if (FAILED(hr)) return hr;

    // 4. Input Layout을 컨텍스트에 적용
    g_pImmediateContext->IASetInputLayout(g_pVertexLayout.Get());

    // 5. Pixel Shader 컴파일
    hr = D3DCompileFromFile(L"Shaders.hlsl", nullptr, nullptr, "PS", "ps_4_0", 0, 0, pPSBlob.GetAddressOf(), pErrorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (pErrorBlob) OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        return hr;
    }

    // 6. Pixel Shader 객체 생성
    hr = g_pd3dDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, g_pPixelShader.GetAddressOf());
    if (FAILED(hr)) return hr;

    // 1. 버텍스 버퍼, 삼각형을 이룰 점 3개 정의 (NDC 좌표계: 화면 중앙 0,0 / 우측상단 1,1)
    // 중요: 시계 방향(Clockwise) 순서로 정의해야 앞면으로 인식되어 그려집니다!
    SimpleVertex vertices[] =
    {
        // 위치(x, y, z)                // 색상(r, g, b, a)
        { XMFLOAT3(0.0f,  0.5f, 0.5f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) }, // 위쪽 (빨강)
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) }, // 우측 하단 (초록)
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f) }, // 좌측 하단 (파랑)
    };

    // 2. 버퍼 설정 구조체 (설계도)
    D3D11_BUFFER_DESC bd = { 0 };
    bd.Usage = D3D11_USAGE_DEFAULT;             // GPU가 읽고 쓰기 좋게 배치 (CPU 접근 불가)
    bd.ByteWidth = sizeof(SimpleVertex) * 3;    // 버퍼의 총 바이트 크기
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;    // "이것은 버텍스 버퍼다"
    bd.CPUAccessFlags = 0;                      // CPU가 내용 수정 안 함

    // 3. 초기 데이터 설정 (내용물)
    D3D11_SUBRESOURCE_DATA InitData = { 0 };
    InitData.pSysMem = vertices; // 위에서 만든 배열의 포인터

    // 4. 버퍼 생성 (VRAM 할당 및 데이터 복사)
    hr = g_pd3dDevice->CreateBuffer(&bd, &InitData, g_pVertexBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;

    // 1. 상수 버퍼 생성
    D3D11_BUFFER_DESC cbd = { 0 };
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(ConstantBuffer);      // 크기 (64 * 3 = 192 bytes)
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;  // "이것은 상수 버퍼다"
    cbd.CPUAccessFlags = 0;

    hr = g_pd3dDevice->CreateBuffer(&cbd, nullptr, g_pConstantBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;

    // 2. 행렬 초기화 (카메라 세팅)
    // 월드 행렬: 단위 행렬(Identity)로 시작
    g_World = XMMatrixIdentity();

    // 뷰 행렬: 카메라 위치(0, 0, -5)에서 원점(0, 0, 0)을 바라봄
    XMVECTOR Eye = XMVectorSet(0.0f, 0.0f, -5.0f, 0.0f);
    XMVECTOR At = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    g_View = XMMatrixLookAtLH(Eye, At, Up); // LH: Left-Handed (왼손 좌표계)

    // 프로젝션 행렬: 
    // FOV(시야각) 90도(PI/2), 화면비 800/600, 근접 0.01, 원거리 100
    g_Projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 800.0f / 600.0f, 0.01f, 100.0f);

    return S_OK;
}

// 렌더링 함수
void Render() {
    // 0. 화면 지우기 (Clear)
    // RGBA (파란색 계열: Cornflower Blue)
    float ClearColor[4] = { 0.0f, 0.125f, 0.3f, 1.0f };
    g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView.Get(), ClearColor);

    // ---------------------------------------------------------
    // 1. 애니메이션 로직 (Update)
    // ---------------------------------------------------------

    // 시간을 이용해 회전 각도 계산 (GetTickCount64 사용)
    static ULONGLONG timeStart = 0;
    ULONGLONG timeCur = GetTickCount64();
    if (timeStart == 0) timeStart = timeCur;
    float t = (timeCur - timeStart) / 1000.0f; // 초 단위 시간

    // Y축 기준 회전 행렬 생성 (빙글빙글)
    g_World = XMMatrixRotationY(t);


    // ---------------------------------------------------------
    // 2. GPU로 데이터 전송 (Update Subresource)
    // ---------------------------------------------------------
    ConstantBuffer cb;
    // Transpose(전치)를 꼭 해줘야 HLSL이 올바르게 읽습니다!
    // C++(DirectXMath)은 행 우선(Row-Major) 행렬을 쓰지만, HLSL은 기본적으로 열 우선(Column-Major) 행렬을 기대합니다.
    // 그래서 데이터를 보내기 전에 전치(Transpose) 해줘야 합니다.
    cb.mWorld = XMMatrixTranspose(g_World);
    cb.mView = XMMatrixTranspose(g_View);
    cb.mProjection = XMMatrixTranspose(g_Projection);

    // 컨텍스트를 이용해 GPU 메모리 갱신
    g_pImmediateContext->UpdateSubresource(g_pConstantBuffer.Get(), 0, nullptr, &cb, 0, 0);


    // ---------------------------------------------------------
    // 3. 파이프라인 설정 및 그리기
    // ---------------------------------------------------------

    // VS 단계에 상수 버퍼 연결 (0번 슬롯)
    g_pImmediateContext->VSSetConstantBuffers(0, 1, g_pConstantBuffer.GetAddressOf());

    // 셰이더 장착
    g_pImmediateContext->VSSetShader(g_pVertexShader.Get(), nullptr, 0);
    g_pImmediateContext->PSSetShader(g_pPixelShader.Get(), nullptr, 0);

    // IA(Input Assembler) 설정 및 그리기
    // A. 정점 버퍼를 파이프라인에 묶기
    UINT stride = sizeof(SimpleVertex); // 정점 하나가 몇 바이트인지
    UINT offset = 0;                    // 버퍼 시작부터 몇 바이트 띄우고 읽을지
    g_pImmediateContext->IASetVertexBuffers(0, 1, g_pVertexBuffer.GetAddressOf(), &stride, &offset);

    // B. 도형의 위상(Topology) 설정
    // "점 3개마다 끊어서 삼각형 하나로 인식해라" (Triangle List)
    g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // C. 그리기 명령 (GPU야 일해라!)
    // Draw(정점 개수, 시작 인덱스)
    g_pImmediateContext->Draw(3, 0);

    // D. 보여주기 (Swap Buffer)
    // 백 버퍼와 프론트 버퍼 교체
    g_pSwapChain->Present(0, 0); // 첫 번째 인자: 1이면 VSync 켜기, 0이면 끄기
}

// 메인 진입점
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    g_hInst = hInstance;

    // 1. 윈도우 클래스 등록
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"DX11GameClass";

    RegisterClassExW(&wcex);

    // 2. 윈도우 생성
    g_hWnd = CreateWindowW(L"DX11GameClass", L"My First DX11 Game", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, nullptr);

    if (!g_hWnd) return FALSE;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // 3. DirectX 초기화 호출, 파이프라인 초기화 호출
    if (FAILED(InitD3D(g_hWnd))) return 0; // 초기화 실패 시 종료
    if (FAILED(InitPipeline())) return 0;

    // 4. 메인 루프 (유니티의 엔진 루프와 같음)
    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        // 윈도우 메시지가 있으면 처리 (입력 등)
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            // 메시지가 없을 때 게임 로직 실행 (Update & Render)
            // 렌더링 함수 호출
            Render();
        }
    }

    return (int)msg.wParam;
}