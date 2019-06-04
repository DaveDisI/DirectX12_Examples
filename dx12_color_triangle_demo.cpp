#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

#include <stdio.h>

#include <comdef.h>

static const UINT FrameCount = 2;

IDXGISwapChain3* m_swapChain;
ID3D12Device* m_device;
ID3D12Resource* m_renderTargets[FrameCount];
ID3D12CommandAllocator* m_commandAllocator;
ID3D12CommandQueue* m_commandQueue;
ID3D12DescriptorHeap* m_rtvHeap;
ID3D12PipelineState* m_pipelineState;
ID3D12GraphicsCommandList* m_commandList;
UINT m_rtvDescriptorSize;

UINT m_frameIndex;
HANDLE m_fenceEvent;
ID3D12Fence* m_fence;
UINT64 m_fenceValue;

ID3D12Resource* m_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
ID3D12RootSignature* m_rootSignature;

void checkError(HRESULT res){
    if(res != S_OK){
        _com_error err(res);
        LPCTSTR errMsg = err.ErrorMessage();
        MessageBox(0, errMsg, "Error!", 0);
        exit(1);
    }
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam){
    return DefWindowProc(hWnd, message, wParam, lParam);
}

int main(int argc, char** argv){
    HMODULE hwnd = GetModuleHandle(0);
    WNDCLASSEX windowClass = { 0 };
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hwnd;
    windowClass.hCursor = LoadCursor(0, IDC_ARROW);
    windowClass.lpszClassName = "DXSampleClass";
    RegisterClassEx(&windowClass);
    
    HWND window = CreateWindow(windowClass.lpszClassName, "dx12", WS_OVERLAPPEDWINDOW, 100, 100, 900, 500, 0, 0, hwnd, 0);

    UINT dxgiFactoryFlags = 0;
    IDXGIFactory4* factory;
    checkError(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    IDXGIAdapter1* hardwareAdapter = 0;

    for (UINT adapterIndex = 0; ; ++adapterIndex){
        IDXGIAdapter1* pAdapter = 0;
        if (DXGI_ERROR_NOT_FOUND == factory->EnumAdapters1(adapterIndex, &hardwareAdapter)){
            break;
        } 

        if (SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), 0))){
            hardwareAdapter = pAdapter;
            break;
        }
        pAdapter->Release();
    }

    checkError(D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    checkError(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = 900;
    swapChainDesc.Height = 500;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    IDXGISwapChain1* swapChain;
    checkError(factory->CreateSwapChainForHwnd(m_commandQueue, window, &swapChainDesc, 0, 0, &swapChain));

    checkError(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));

    m_swapChain = (IDXGISwapChain3*)swapChain;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    checkError(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT n = 0; n < FrameCount; n++)
    {
        checkError(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
        m_device->CreateRenderTargetView(m_renderTargets[n], 0, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
    
    checkError(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    ///////////////////////////////
    //Make Shader Pipeline
    ///////////////////////////////
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    //rootSignatureDesc.Init(0, 0, 0, 0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    rootSignatureDesc.NumParameters = 0;
    rootSignatureDesc.pParameters = 0;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = 0;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* signature;
    ID3DBlob* error;
    checkError(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    checkError(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    ID3DBlob* vertexShader;
    ID3DBlob* pixelShader;

    checkError(D3DCompileFromFile(L"shaders.hlsl", 0, 0, "VSMain", "vs_5_0", 0, 0, &vertexShader, 0));
    checkError(D3DCompileFromFile(L"shaders.hlsl", 0, 0, "PSMain", "ps_5_0", 0, 0, &pixelShader, 0));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_SHADER_BYTECODE vsbc;
    vsbc.pShaderBytecode = vertexShader->GetBufferPointer();
    vsbc.BytecodeLength = vertexShader->GetBufferSize();

    D3D12_SHADER_BYTECODE psbc;
    psbc.pShaderBytecode = pixelShader->GetBufferPointer();
    psbc.BytecodeLength = pixelShader->GetBufferSize();

    D3D12_RASTERIZER_DESC rades;
    rades.FillMode = D3D12_FILL_MODE_SOLID;
    rades.CullMode = D3D12_CULL_MODE_BACK;
    rades.FrontCounterClockwise = false;
    rades.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rades.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rades.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rades.DepthClipEnable = true;
    rades.MultisampleEnable = false;
    rades.AntialiasedLineEnable = false;
    rades.ForcedSampleCount = 0;
    rades.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC bledes;
    bledes.AlphaToCoverageEnable = false;
    bledes.IndependentBlendEnable = false;
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
        false,false,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; i++){
        bledes.RenderTarget[i] = defaultRenderTargetBlendDesc;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature;
    psoDesc.VS = vsbc;
    psoDesc.PS = psbc;
    psoDesc.RasterizerState = rades;
    psoDesc.BlendState = bledes;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    checkError(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    checkError(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, 0, IID_PPV_ARGS(&m_commandList)));
    checkError(m_commandList->Close());
    ///////////////////////////////
    //Make and Load buffers
    ///////////////////////////////
    float triangleVertices[] = {
        -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         0.0f,  0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
         0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
    };

    const UINT vertexBufferSize = sizeof(triangleVertices);

    D3D12_HEAP_PROPERTIES heapProp = {};
    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = vertexBufferSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    checkError(m_device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, 0, IID_PPV_ARGS(&m_vertexBuffer)));

    UINT8* pVertexDataBegin;
    D3D12_RANGE readRange = {}; 
    readRange.Begin = 0;
    readRange.End = 0;
    checkError(m_vertexBuffer->Map(0, &readRange, (void**)(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
    m_vertexBuffer->Unmap(0, 0);

    // Initialize the vertex buffer view.
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = 28;
    m_vertexBufferView.SizeInBytes = vertexBufferSize;


    checkError(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;

    m_fenceEvent = CreateEvent(0, FALSE, FALSE, 0);
    if (m_fenceEvent == 0){
        checkError(HRESULT_FROM_WIN32(GetLastError()));
    }
    
    const UINT64 fence = m_fenceValue;
    checkError(m_commandQueue->Signal(m_fence, fence));
    m_fenceValue++;

    if (m_fence->GetCompletedValue() < fence){
        checkError(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    ShowWindow(window, SW_SHOW);

    MSG msg = {};
    while (msg.message != WM_QUIT){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if(msg.message == WM_PAINT){
            checkError(m_commandAllocator->Reset());

            checkError(m_commandList->Reset(m_commandAllocator, m_pipelineState));

            D3D12_VIEWPORT viewport;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;
            viewport.Width = 900;
            viewport.Height = 500;
            viewport.MinDepth = D3D12_MIN_DEPTH;
            viewport.MaxDepth = D3D12_MAX_DEPTH;
            D3D12_RECT scissorRect;
            scissorRect.left = 0;
            scissorRect.top = 0;
            scissorRect.right = 900;
            scissorRect.bottom = 500;
            m_commandList->SetGraphicsRootSignature(m_rootSignature);
            m_commandList->RSSetViewports(1, &viewport);
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // Indicate that the back buffer will be used as a render target.
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = m_renderTargets[m_frameIndex];
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &barrier);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
            rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
            m_commandList->OMSetRenderTargets(1, &rtvHandle, false, 0);

            // Record commands.
            const float clearColor[] = { 1.0f, 0.2f, 0.4f, 1.0f };
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, 0);
            m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
            m_commandList->DrawInstanced(3, 1, 0, 0);

            // Indicate that the back buffer will now be used to present.
            //m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            m_commandList->ResourceBarrier(1, &barrier);

            checkError(m_commandList->Close());

            ID3D12CommandList* ppCommandLists[] = { m_commandList };
            m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Present the frame.
            checkError(m_swapChain->Present(1, 0));

            checkError(m_commandQueue->Signal(m_fence, fence));
            m_fenceValue++;

            // Wait until the previous frame is finished.
            if (m_fence->GetCompletedValue() < fence){
                checkError(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }

            m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
            
        }else if(msg.message == WM_KEYDOWN){
            exit(0);
        }
    }

    return 0;
}