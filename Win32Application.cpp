#include "stdafx.h"
#include "Win32Application.h"
#include "Renderer.h"

#define CATCH_PRINT_ERROR(extraCatchCode) \
    catch(const std::exception& ex) \
    { \
        fwprintf(stderr, L"ERROR: %hs\n", ex.what()); \
        extraCatchCode \
    } \
    catch(...) \
    { \
        fwprintf(stderr, L"UNKNOWN ERROR.\n"); \
        extraCatchCode \
    }

static const wchar_t* const CLASS_NAME = L"D3D12MemAllocSample";

HWND Win32Application::m_hwnd = nullptr;
UINT64 Win32Application::m_TimeOffset;
UINT64 Win32Application::m_TimeValue;
float Win32Application::m_Time;
float Win32Application::m_TimeDelta;

int Win32Application::Run(Renderer* pSample, HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
    windowClass.hbrBackground = NULL;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    windowClass.hInstance = hInstance;
    windowClass.lpfnWndProc = &WindowProc;
    windowClass.lpszClassName = CLASS_NAME;

    ATOM classR = RegisterClassEx(&windowClass);
    assert(classR);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT windowRect = { 0, 0, static_cast<LONG>(pSample->GetWidth()), static_cast<LONG>(pSample->GetHeight()) };
    AdjustWindowRect(&windowRect, style, FALSE);

    // Create the window and store a handle to it.
    m_hwnd = CreateWindow(
        windowClass.lpszClassName,
        pSample->GetTitle(),
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,        // We have no parent window.
        nullptr,        // We aren't using menus.
        hInstance,
        pSample);
    assert(m_hwnd);

    pSample->Init();
    m_TimeOffset = GetTickCount64();

    ShowWindow(m_hwnd, nCmdShow);

    MSG msg;
    for (;;)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            const UINT64 newTimeValue = GetTickCount64() - m_TimeOffset;
            m_TimeDelta = (float)(newTimeValue - m_TimeValue) * 0.001f;
            m_TimeValue = newTimeValue;
            m_Time = (float)newTimeValue * 0.001f;

            pSample->Update(m_Time);
            pSample->Render();
        }
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Renderer* pSample = reinterpret_cast<Renderer*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
        case WM_CREATE:
        {
            // Save the Renderer* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }

        return 0;
    case WM_DESTROY:
        try
        {
            pSample->Cleanup();
        }
        CATCH_PRINT_ERROR(;)
        
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        try
        {
            pSample->OnKeyDown(wParam);
        }
        CATCH_PRINT_ERROR(DestroyWindow(hWnd);)
        
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}