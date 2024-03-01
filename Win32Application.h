#pragma once

class Renderer;

class Win32Application
{
public:
  static int Run(Renderer* pSample, HINSTANCE hInstance, int nCmdShow);
  static HWND GetHwnd() { return m_hwnd; }

protected:
  static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);

private:
  static HWND m_hwnd;
  static UINT64 m_TimeOffset;  // In ms.
  static UINT64 m_TimeValue;   // Time since m_TimeOffset, in ms.
  static float m_Time;         // m_TimeValue converted to float, in seconds.
  static float m_TimeDelta;
};
