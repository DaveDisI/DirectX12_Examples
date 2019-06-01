#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

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

void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter){
    *ppAdapter = nullptr;
    for (UINT adapterIndex = 0; ; ++adapterIndex){
        IDXGIAdapter1* pAdapter = nullptr;
        if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &pAdapter)){
            // No more adapters to enumerate.
            break;
        } 

        // Check to see if the adapter supports Direct3D 12, but don't create the
        // actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))){
            *ppAdapter = pAdapter;
            return;
        }
        pAdapter->Release();
    }
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

    IDXGIAdapter1* hardwareAdapter;
    GetHardwareAdapter(factory, &hardwareAdapter);

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

    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        checkError(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        for (UINT n = 0; n < FrameCount; n++)
        {
            checkError(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n], 0, rtvHandle);
            rtvHandle.ptr += m_rtvDescriptorSize;
        }
    }

    checkError(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    checkError(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, 0, IID_PPV_ARGS(&m_commandList)));
    checkError(m_commandList->Close());

    {
        checkError(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        m_fenceEvent = CreateEvent(0, FALSE, FALSE, 0);
        if (m_fenceEvent == 0)
        {
            checkError(HRESULT_FROM_WIN32(GetLastError()));
        }
    }

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

            // Indicate that the back buffer will be used as a render target.
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = m_renderTargets[m_frameIndex];
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            //m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
            m_commandList->ResourceBarrier(1, &barrier);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
            rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;

            // Record commands.
            const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
            m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, 0);

            // Indicate that the back buffer will now be used to present.
            D3D12_RESOURCE_BARRIER barrier2 = {};
            barrier2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier2.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier2.Transition.pResource = m_renderTargets[m_frameIndex];
            barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            //m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
            m_commandList->ResourceBarrier(1, &barrier2);

            checkError(m_commandList->Close());

            ID3D12CommandList* ppCommandLists[] = { m_commandList };
            m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Present the frame.
            checkError(m_swapChain->Present(1, 0));

            const UINT64 fence = m_fenceValue;
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