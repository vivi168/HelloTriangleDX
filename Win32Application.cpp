#include "stdafx.h"
#include "Win32Application.h"
#include "Renderer.h"
#include "Input.h"
#include "Game.h"

using namespace DirectX;

static const wchar_t* const CLASS_NAME = L"D3D12MemAllocSample";
static const wchar_t* const WINDOW_TITLE = L"D3D12 Memory Allocator Sample";

static HWND g_Hwnd = nullptr;
static LARGE_INTEGER g_Frequency;
static LARGE_INTEGER g_StartTime;
static LARGE_INTEGER g_LastTime;
static float g_Time;         // g_TimeValue converted to float, in seconds.
static float g_TimeDelta;

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                                   LPARAM lParam);

int Win32Application::Run(HINSTANCE hInstance, int nCmdShow)
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

  Renderer::InitWindow(1280, 720, WINDOW_TITLE);

  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT windowRect = {0, 0, static_cast<LONG>(Renderer::GetWidth()),
                     static_cast<LONG>(Renderer::GetHeight())};
  AdjustWindowRect(&windowRect, style, FALSE);

  // Create the window and store a handle to it.
  g_Hwnd = CreateWindow(windowClass.lpszClassName, Renderer::GetTitle(), style,
                        CW_USEDEFAULT, CW_USEDEFAULT,
                        windowRect.right - windowRect.left,
                        windowRect.bottom - windowRect.top,
                        nullptr,  // We have no parent window.
                        nullptr,  // We aren't using menus.
                        hInstance, nullptr);
  assert(g_Hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui_ImplWin32_Init(Win32Application::GetHwnd());

  Renderer::Init();

  QueryPerformanceFrequency(&g_Frequency);
  QueryPerformanceCounter(&g_StartTime);
  g_LastTime = g_StartTime;

  ShowWindow(g_Hwnd, nCmdShow);

  Game::Init();

  Renderer::LoadAssets();

  MSG msg;
  for (;;) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) break;

      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      LARGE_INTEGER currentTime;
      QueryPerformanceCounter(&currentTime);
      g_TimeDelta = static_cast<float>(currentTime.QuadPart - g_LastTime.QuadPart) / g_Frequency.QuadPart;
      g_Time = static_cast<float>(currentTime.QuadPart - g_StartTime.QuadPart) / g_Frequency.QuadPart;
      g_LastTime = currentTime;

      if (g_TimeDelta > 0.f)
      {
        Input::Update();

        if (Input::IsPressed(Input::KB::Escape)) {
          PostMessage(g_Hwnd, WM_CLOSE, 0, 0);
        }

        Game::Update(g_Time, g_TimeDelta);
        Renderer::Update(g_Time, g_TimeDelta);
        Game::DebugWindow();
        static std::array<float, 100> fpsHistory = {};
        static int fpsIndex = 0;
        float fps = 1.0f / g_TimeDelta;
        fpsHistory[fpsIndex] = fps;
        fpsIndex = (fpsIndex + 1) % fpsHistory.size();
        float avgFps = std::accumulate(fpsHistory.begin(), fpsHistory.end(), 0.f) / fpsHistory.size();

        ImGui::Begin("Stats");
        ImGui::Text("FPS: %.1f", avgFps);
        ImGui::PlotLines("FPS History", fpsHistory.data(), fpsHistory.size(),
                         fpsIndex, nullptr, 0.0f, 120.0f, ImVec2(0, 80));
        ImGui::End();
        Renderer::Render();
      }
    }
  }
  return (int)msg.wParam;
}

HWND Win32Application::GetHwnd() { return g_Hwnd; }

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                                   LPARAM lParam)
{
  if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    return true;

  switch (message) {
    case WM_DESTROY:
      Renderer::Cleanup();
      PostQuitMessage(0);
      return 0;
    case WM_KEYUP:
      Input::OnKeyUp(wParam);
      return 0;
    case WM_KEYDOWN:
      Input::OnKeyDown(wParam);
      return 0;
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}
