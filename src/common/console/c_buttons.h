#pragma once

#include <stdint.h>
#include "tarray.h"
#include "name.h"

// Actions
struct FButtonStatus
{
	enum { MAX_KEYS = 6 };	// Maximum number of keys that can press this button

	uint16_t Keys[MAX_KEYS];
	bool bDown;				// Button is down right now
	bool bWentDown;			// Button went down this tic
	bool bWentUp;			// Button went up this tic
	bool bReleaseLock;		// Lock ReleaseKey call in ResetButtonStates
	void (*PressHandler)();		// for optional game-side customization
	void (*ReleaseHandler)();

	bool PressKey (int keynum);		// Returns true if this key caused the button to be pressed.
	bool ReleaseKey (int keynum);	// Returns true if this key is no longer pressed.
	void ResetTriggers () { bWentDown = bWentUp = false; }
	void Reset () { bDown = bWentDown = bWentUp = false; }
};

class ButtonMap
{

	TArray<FButtonStatus> Buttons;
	TArray<FString> NumToName;		// The internal name of the button
	TMap<FName, int> NameToNum;

public:
	void SetButtons(const char** names, int count);

	int NumButtons() const
	{
		return Buttons.Size();
	}

	int FindButtonIndex(const char* func, int funclen = -1) const;

	FButtonStatus* FindButton(const char* func, int funclen = -1)
	{
		int index = FindButtonIndex(func, funclen);
		return index > -1 ? &Buttons[index] : nullptr;
	}

	FButtonStatus* GetButton(int index)
	{
		return &Buttons[index];
	}

	void ResetButtonTriggers();	// Call ResetTriggers for all buttons
	void ResetButtonStates();		// Same as above, but also clear bDown
	int ListActionCommands(const char* pattern);
	void AddButtonTabCommands();


	bool ButtonDown(int x) const
	{
		return Buttons[x].bDown;
	}

	bool ButtonPressed(int x) const
	{
		return Buttons[x].bWentDown;
	}

	bool ButtonReleased(int x) const
	{
		return Buttons[x].bWentUp;
	}

	void ButtonSet(int x) const
	{
		Buttons[x].bDown = Buttons[x].bWentDown = true;
		Buttons[x].bWentUp = false;
	}

	void ClearButton(int x)
	{
		Buttons[x].Reset();
	}

	// [split-screen] Save/restore the complete button state so several local
	// players can each have their own set of pressed buttons.
	void GetStates(TArray<FButtonStatus>& out) const
	{
		if (out.Size() != Buttons.Size()) out = Buttons;
		else for (unsigned i = 0; i < Buttons.Size(); i++) out[i] = Buttons[i];
	}
	void SetStates(const TArray<FButtonStatus>& in)
	{
		if (in.Size() == Buttons.Size())
			for (unsigned i = 0; i < Buttons.Size(); i++) Buttons[i] = in[i];
	}
	static void ResetStates(TArray<FButtonStatus>& states);
	// Called by ResetButtonStates so stored states of other local players can be cleared as well.
	static void (*OnResetButtonStates)();
};

extern ButtonMap buttonMap;
