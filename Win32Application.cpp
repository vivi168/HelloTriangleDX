#include "stdafx.h"
#include "Win32Application.h"
#include "Renderer.h"
#include "Input.h"
#include "Terrain.h"
#include "Collider.h"

using namespace DirectX;

static const wchar_t* const CLASS_NAME = L"D3D12MemAllocSample";
static const wchar_t* const WINDOW_TITLE = L"D3D12 Memory Allocator Sample";

static HWND g_Hwnd = nullptr;
static UINT64 g_TimeOffset;  // In ms.
static UINT64 g_TimeValue;   // Time since g_TimeOffset, in ms.
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
  g_TimeOffset = GetTickCount64();

  ShowWindow(g_Hwnd, nCmdShow);

  Chunk t;

  Camera camera;
  camera.Translate(-100.f, 75.f, 130.f);
  camera.Target(0.f, 0.f, 0.f);

  Renderer::SetSceneCamera(&camera);

  Mesh3D treeMesh, cubeMesh, cylinderMesh, yukaMesh, houseMesh, terrainMesh,
      stairsMesh;
  treeMesh.Read("assets/tree.objb");
  yukaMesh.Read("assets/yuka.objb");
  houseMesh.Read("assets/house.objb");
  terrainMesh.Read("assets/terrain.objb");
  // terrainMesh = t.Mesh();
  cubeMesh.Read("assets/cube.objb");
  cylinderMesh.Read("assets/cylinder.objb");
  stairsMesh.Read("assets/stairs.objb");

  Model3D bigTree, smallTree, cube, cylinder, yuka, house, terrain, stairs;

  bigTree.mesh = &treeMesh;
  smallTree.mesh = &treeMesh;
  yuka.mesh = &yukaMesh;
  house.mesh = &houseMesh;
  terrain.mesh = &terrainMesh;
  cube.mesh = &cubeMesh;
  cylinder.mesh = &cylinderMesh;
  stairs.mesh = &stairsMesh;

  smallTree.Scale(0.5f);
  smallTree.Translate(-7.f, 0.f, 0.f);
  bigTree.Translate(-7.f, 0.0f, 14.f);
  yuka.Scale(5.f);
  yuka.Translate(15.f, 0.f, 15.f);
  house.Translate(50.f, 0.f, 20.f);
  stairs.Translate(-50.f, 0.f, 20.f);
  cube.Translate(0.f, 50.f, 0.f);
  cube.Scale(5.f);

  Renderer::AppendToScene(&bigTree);
  Renderer::AppendToScene(&smallTree);
  Renderer::AppendToScene(&yuka);
  Renderer::AppendToScene(&house);
  Renderer::AppendToScene(&terrain);
  Renderer::AppendToScene(&cube);
  Renderer::AppendToScene(&cylinder);
  Renderer::AppendToScene(&stairs);

  Renderer::LoadAssets();

  Collider collider;
  collider.AppendStaticModel(&terrain);
  collider.AppendStaticModel(&yuka);
  collider.AppendStaticModel(&stairs);

  float playerX, playerY, playerZ;
  float playerDirection = XM_PIDIV2;
  float playerRotSpeed = 0.02f;
  float playerSpeed = 20.0f;

  playerX = 0.f;
  playerY = 0.f;
  playerZ = 0.f;

  cylinder.Translate(playerX, playerY, playerZ);

  MSG msg;
  for (;;) {
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) break;

      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      const UINT64 newTimeValue = GetTickCount64() - g_TimeOffset;
      g_TimeDelta = (float)(newTimeValue - g_TimeValue) * 0.001f;
      g_TimeValue = newTimeValue;
      g_Time = (float)newTimeValue * 0.001f;

      {
        Input::Update();
        camera.ProcessKeyboard();

        if (Input::IsPressed(Input::KB::Escape)) {
          PostMessage(g_Hwnd, WM_CLOSE, 0, 0);
        }

        {
          float forwardX = cosf(playerDirection);
          float forwardZ = sinf(playerDirection);

          if (Input::IsHeld(Input::KB::I)) {
            playerX += playerSpeed * forwardX * g_TimeDelta;
            playerZ += playerSpeed * forwardZ * g_TimeDelta;
          } else if (Input::IsHeld(Input::KB::K)) {
            playerX -= playerSpeed * forwardX * g_TimeDelta;
            playerZ -= playerSpeed * forwardZ * g_TimeDelta;
          }
          if (Input::IsHeld(Input::KB::J)) {
            playerDirection += playerRotSpeed;
          }
          if (Input::IsHeld(Input::KB::L)) {
            playerDirection -= playerRotSpeed;
          }

          if (Input::IsPressed(Input::KB::Space))
            camera.Target(playerX, playerY, playerZ);

          cylinder.Translate(playerX, playerY, playerZ);
          cylinder.Rotate(0.0f, -playerDirection, 0.f);
        }
      }

      cube.Rotate(g_Time * .5f, 0.f, 0.f);
      float height = -100.0f;
      collider.FindFloor({playerX, playerY, playerZ}, &height);
      playerY = height;

      Renderer::Update(g_Time);

      {
        ImGui::Begin("Player");
        ImGui::Text("x: %f y: %f z: %f\ndir: %f", playerX, playerY, playerZ,
                    playerDirection);

        ImGui::Text("floor height: %f", height);

        ImGui::End();
      }

      Renderer::Render();
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
