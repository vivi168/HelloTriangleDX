#include "stdafx.h"
#include "Input.h"

#include <unordered_map>

using namespace Input;

struct KeyState
{
	bool held, pressed, released;
};

struct InputState
{
	std::unordered_map<KB, bool> newKeyState;
	std::unordered_map<KB, bool> oldKState;
	std::unordered_map<KB, KeyState> keyState;
};

static InputState inputState;

void Input::Update()
{
	for (auto& [k, ks] : inputState.keyState) {
		ks.pressed = false;
		ks.released = false;

		if (inputState.newKeyState[k] != inputState.oldKState[k]) {
			if (inputState.newKeyState[k] == true) {
				ks.pressed = !ks.held;
				ks.held = true;
			}
			else {
				ks.released = true;
				ks.held = false;
			}
		}

		inputState.oldKState[k] = inputState.newKeyState[k];
	}
}

void SetNewKeyState(WPARAM key, bool state)
{
	switch (key)
	{
	case 'W':
		inputState.newKeyState[KB::W] = state;
		break;
	case 'A':
		inputState.newKeyState[KB::A] = state;
		break;
	case 'S':
		inputState.newKeyState[KB::S] = state;
		break;
	case 'D':
		inputState.newKeyState[KB::D] = state;
		break;
	case VK_UP:
		inputState.newKeyState[KB::Up] = state;
		break;
	case VK_LEFT:
		inputState.newKeyState[KB::Left] = state;
		break;
	case VK_DOWN:
		inputState.newKeyState[KB::Down] = state;
		break;
	case VK_RIGHT:
		inputState.newKeyState[KB::Right] = state;
		break;
	case VK_ESCAPE:
		inputState.newKeyState[KB::Escape] = state;
		break;
	}
}

void Input::OnKeyUp(WPARAM key)
{
	SetNewKeyState(key, false);
}

void Input::OnKeyDown(WPARAM key)
{
	SetNewKeyState(key, true);
}

bool Input::IsHeld(KB key)
{
	return inputState.keyState[key].held;
}

bool Input::IsPressed(KB key)
{
	return inputState.keyState[key].pressed;
}

bool Input::IsReleased(KB key)
{
	return inputState.keyState[key].released;
}