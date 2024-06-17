#include "stdafx.h"
#include "Win32Application.h"
#include "Renderer.h"
#include "Input.h"
#include "Terrain.h"
#include "Collider.h"

#define CATCH_PRINT_ERROR(extraCatchCode)         \
  catch (const std::exception& ex)                \
  {                                               \
    fwprintf(stderr, L"ERROR: %hs\n", ex.what()); \
    extraCatchCode                                \
  }                                               \
  catch (...)                                     \
  {                                               \
    fwprintf(stderr, L"UNKNOWN ERROR.\n");        \
    extraCatchCode                                \
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
  RECT windowRect = {0, 0, static_cast<LONG>(pSample->GetWidth()),
                     static_cast<LONG>(pSample->GetHeight())};
  AdjustWindowRect(&windowRect, style, FALSE);

  // Create the window and store a handle to it.
  m_hwnd = CreateWindow(windowClass.lpszClassName, pSample->GetTitle(), style,
                        CW_USEDEFAULT, CW_USEDEFAULT,
                        windowRect.right - windowRect.left,
                        windowRect.bottom - windowRect.top,
                        nullptr,  // We have no parent window.
                        nullptr,  // We aren't using menus.
                        hInstance, pSample);
  assert(m_hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui_ImplWin32_Init(Win32Application::GetHwnd());

  pSample->Init();
  m_TimeOffset = GetTickCount64();

  ShowWindow(m_hwnd, nCmdShow);

  Chunk t;

  Camera camera;
  camera.Translate(-100.f, 75.f, 130.f);
  camera.Target(0.f, 0.f, 0.f);

  pSample->SetSceneCamera(&camera);

  Mesh3D treeMesh, cubeMesh, cylinderMesh, yukaMesh, houseMesh, terrainMesh;
  treeMesh.Read("assets/tree.objb");
  yukaMesh.Read("assets/yuka.objb");
  houseMesh.Read("assets/house.objb");
  terrainMesh.Read("assets/terrain.objb");
  //terrainMesh = t.Mesh();
  cubeMesh.Read("assets/cube.objb");
  cylinderMesh.Read("assets/cylinder.objb");

  Model3D bigTree, smallTree, cube, cylinder, yuka, house, terrain;

  bigTree.mesh = &treeMesh;
  smallTree.mesh = &treeMesh;
  yuka.mesh = &yukaMesh;
  house.mesh = &houseMesh;
  terrain.mesh = &terrainMesh;
  cube.mesh = &cubeMesh;
  cylinder.mesh = &cylinderMesh;

  smallTree.Scale(0.5f);
  smallTree.Translate(-7.f, 0.f, 0.f);
  bigTree.Translate(-7.f, 0.0f, 14.f);
  yuka.Scale(5.f);
  yuka.Translate(15.f, 0.f, 15.f);
  house.Translate(50.f, 0.f, 20.f);
  cube.Translate(0.f, 50.f, 0.f);
  cube.Scale(5.f);

  pSample->AppendToScene(&bigTree);
  pSample->AppendToScene(&smallTree);
  pSample->AppendToScene(&yuka);
  pSample->AppendToScene(&house);
  pSample->AppendToScene(&terrain);
  pSample->AppendToScene(&cube);
  pSample->AppendToScene(&cylinder);

  pSample->LoadAssets();

  Collider collider;
  collider.AppendStaticModel(&terrain);
  collider.AppendStaticModel(&yuka);

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
      const UINT64 newTimeValue = GetTickCount64() - m_TimeOffset;
      m_TimeDelta = (float)(newTimeValue - m_TimeValue) * 0.001f;
      m_TimeValue = newTimeValue;
      m_Time = (float)newTimeValue * 0.001f;

      {
        Input::Update();
        camera.ProcessKeyboard();

        if (Input::IsPressed(Input::KB::Escape)) {
          PostMessage(m_hwnd, WM_CLOSE, 0, 0);
        }

        {
          float forwardX = cosf(playerDirection);
          float forwardZ = sinf(playerDirection);

          if (Input::IsHeld(Input::KB::I)) {
            playerX += playerSpeed * forwardX * m_TimeDelta;
            playerZ += playerSpeed * forwardZ * m_TimeDelta;
          } else if (Input::IsHeld(Input::KB::K)) {
            playerX -= playerSpeed * forwardX * m_TimeDelta;
            playerZ -= playerSpeed * forwardZ * m_TimeDelta;
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

      house.Rotate(m_Time*.5f, 0.f, 0.f);
      cube.Rotate(m_Time * .5f, 0.f, 0.f);

      pSample->Update(m_Time);

      {
        ImGui::Begin("Player");
        ImGui::Text("x: %f y: %f z: %f\ndir: %f", playerX, playerY,
                    playerZ, playerDirection);

        float height = 0.0f;
        collider.FindFloor({playerX, playerY, playerZ}, &height);

        ImGui::Text("floor height: %f", height);

        playerY = height;

        ImGui::End();
      }

      pSample->Render();
    }
  }
  return (int)msg.wParam;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message,
                                              WPARAM wParam, LPARAM lParam)
{
  Renderer* pSample =
      reinterpret_cast<Renderer*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

  if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    return true;

  switch (message) {
    case WM_CREATE: {
      // Save the Renderer* passed in to CreateWindow.
      LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
      SetWindowLongPtr(
          hWnd, GWLP_USERDATA,
          reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
    }

      return 0;
    case WM_DESTROY:
      try {
        pSample->Cleanup();
      }
      CATCH_PRINT_ERROR(;)

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
