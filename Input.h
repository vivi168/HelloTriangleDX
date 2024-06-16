#pragma once

namespace Input
{
enum class KB { Q, W, E, A, S, D, I, J, K, L, Up, Left, Down, Right, Space, Escape };

void Update();
void OnKeyUp(WPARAM key);
void OnKeyDown(WPARAM key);
bool IsHeld(KB);
bool IsPressed(KB);
bool IsReleased(KB);
}  // namespace Input
