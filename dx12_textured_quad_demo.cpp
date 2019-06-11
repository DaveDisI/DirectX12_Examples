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
ID3D12DescriptorHeap* m_srvHeap;
ID3D12PipelineState* m_pipelineState;
ID3D12GraphicsCommandList* m_commandList;
UINT m_rtvDescriptorSize;

UINT m_frameIndex;
HANDLE m_fenceEvent;
ID3D12Fence* m_fence;
UINT64 m_fenceValue;

ID3D12Resource* m_vertexBuffer;
ID3D12Resource* m_texture;
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

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    checkError(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT n = 0; n < FrameCount; n++){
        checkError(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
        m_device->CreateRenderTargetView(m_renderTargets[n], 0, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }
    
    checkError(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));


    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))){
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    D3D12_DESCRIPTOR_RANGE1 ranges;
    ranges.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges.NumDescriptors = 1;
    ranges.BaseShaderRegister = 0;
    ranges.RegisterSpace = 0;
    ranges.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
    ranges.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_DESCRIPTOR_TABLE1 rtDescTbl;
    rtDescTbl.NumDescriptorRanges = 1;
    rtDescTbl.pDescriptorRanges = &ranges;


    D3D12_ROOT_PARAMETER1 rootParameters;
    rootParameters.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters.DescriptorTable = rtDescTbl;
    rootParameters.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC vRtSigDesc;
    vRtSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    vRtSigDesc.Desc_1_1.NumParameters = 1;
    vRtSigDesc.Desc_1_1.pParameters = &rootParameters;
    vRtSigDesc.Desc_1_1.NumStaticSamplers = 1;
    vRtSigDesc.Desc_1_1.pStaticSamplers = &sampler;
    vRtSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;


    ID3DBlob* signature;
    ID3DBlob* error;
    checkError(D3D12SerializeVersionedRootSignature(&vRtSigDesc, &signature, &error));
    checkError(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    ID3DBlob* vertexShader;
    ID3DBlob* pixelShader;

    checkError(D3DCompileFromFile(L"sprite_shaders.hlsl", 0, 0, "VSMain", "vs_5_0", 0, 0, &vertexShader, 0));
    checkError(D3DCompileFromFile(L"sprite_shaders.hlsl", 0, 0, "PSMain", "ps_5_0", 0, 0, &pixelShader, 0));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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
    psoDesc.InputLayout = { inputElementDescs, 2 };
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

    checkError(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, m_pipelineState, IID_PPV_ARGS(&m_commandList)));
    
    float triangleVertices[] = {
        -0.5f, -0.5f,  0.0f, 1.0f,
        -0.5f,  0.5f,  0.0f, 0.0f,
         0.5f,  0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  1.0f, 0.0f,
         0.5f, -0.5f,  1.0f, 1.0f, 
        -0.5f, -0.5f,  0.0f, 1.0f,
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
    m_vertexBufferView.StrideInBytes = 16;
    m_vertexBufferView.SizeInBytes = vertexBufferSize;

    ////////////TEXTURE STUFF//////////////////////////////
    unsigned int TextureWidth = 2;
    unsigned int TextureHeight = 2; 
    unsigned int TexturePixelSize = 4;
    
    UINT8 texturePixels[] = {
        255, 0, 0, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 255, 0, 0, 255
    };


    ID3D12Resource* textureUploadHeap;

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.Width = TextureWidth;
    textureDesc.Height = TextureHeight;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    checkError(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, 0, IID_PPV_ARGS(&m_texture)));

    UINT64 uploadBufferSize = 0;
    m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

    // Create the GPU upload buffer.
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC heapDesc = {};
    heapDesc.MipLevels = 1;
    heapDesc.Format = DXGI_FORMAT_UNKNOWN;
    heapDesc.Width = uploadBufferSize;
    heapDesc.Height = 1;
    heapDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    heapDesc.DepthOrArraySize = 1;
    heapDesc.SampleDesc.Count = 1;
    heapDesc.SampleDesc.Quality = 0;
    heapDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    heapDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    checkError(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &heapDesc, D3D12_RESOURCE_STATE_GENERIC_READ, 0, IID_PPV_ARGS(&textureUploadHeap)));

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = &texturePixels[0];
    textureData.RowPitch = TextureWidth * TexturePixelSize;
    textureData.SlicePitch = textureData.RowPitch * TextureHeight;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    layout.Offset = 0;
    layout.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    layout.Footprint.Width = 2;
    layout.Footprint.Height = 2;
    layout.Footprint.Depth = 1;
    layout.Footprint.RowPitch = 256;
    BYTE* pData;
    checkError(textureUploadHeap->Map(0, 0, (void**)(&pData)));
    D3D12_MEMCPY_DEST destData = { pData, layout.Footprint.RowPitch,  layout.Footprint.RowPitch * layout.Footprint.Height };
    BYTE* pDestSlice = (BYTE*)(destData.pData);
    const BYTE* pSrcSlice = (BYTE*)(textureData.pData);
    for (UINT i = 0; i < 2; i++){
        memcpy(pDestSlice + destData.RowPitch * i, pSrcSlice + textureData.RowPitch * i, 8);
    }
    textureUploadHeap->Unmap(0, 0);

    D3D12_TEXTURE_COPY_LOCATION Dst = {};
    Dst.pResource = m_texture;
    Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    Dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION Src = {};
    Src.pResource = textureUploadHeap;
    Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Src.PlacedFootprint = layout;
    m_commandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, 0);


    D3D12_RESOURCE_BARRIER resBar = {};
    resBar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resBar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    resBar.Transition.pResource = m_texture;
    resBar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    resBar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    resBar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &resBar);

    // Describe and create a SRV for the texture.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_texture, &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    checkError(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

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

            ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap };

            m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
            m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());


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
            m_commandList->DrawInstanced(6, 1, 0, 0);

            // Indicate that the back buffer will now be used to present.
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