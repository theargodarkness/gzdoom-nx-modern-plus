/*
** i_joystick.cpp
**
**---------------------------------------------------------------------------
** Copyright 2005-2016 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/
#include <SDL.h>

#include "basics.h"
#include "cmdlib.h"

#include "m_joy.h"
#include "keydef.h"
#include "d_eventbase.h"

#ifdef __SWITCH__
#include <switch.h>
#include "i_time.h"
#endif

// Very small deadzone so that floating point magic doesn't happen
#define MIN_DEADZONE 0.000001f

#define DEFAULT_DEADZONE 0.25f;

class SDLInputJoystick: public IJoystickConfig
{
public:
	SDLInputJoystick(int DeviceIndex) : DeviceIndex(DeviceIndex), Multiplier(1.0f) , Enabled(true)
	{
		InstanceID = -1;
		Device = SDL_JoystickOpen(DeviceIndex);
		if(Device != NULL)
		{
			InstanceID = SDL_JoystickInstanceID(Device);
			NumAxes = SDL_JoystickNumAxes(Device);
			NumHats = SDL_JoystickNumHats(Device);

			SetDefaultConfig();
		}
	}

	int GetInstanceID() const
	{
		return InstanceID;
	}
	~SDLInputJoystick()
	{
		if(Device != NULL)
			M_SaveJoystickConfig(this);
		SDL_JoystickClose(Device);
	}

	bool IsValid() const
	{
		return Device != NULL;
	}

	FString GetName()
	{
		return SDL_JoystickName(Device);
	}
	float GetSensitivity()
	{
		return Multiplier;
	}
	void SetSensitivity(float scale)
	{
		Multiplier = scale;
	}

	int GetNumAxes()
	{
		return NumAxes + NumHats*2;
	}
	float GetAxisDeadZone(int axis)
	{
		return Axes[axis].DeadZone;
	}
	EJoyAxis GetAxisMap(int axis)
	{
		return Axes[axis].GameAxis;
	}
	const char *GetAxisName(int axis)
	{
		return Axes[axis].Name.GetChars();
	}
	float GetAxisScale(int axis)
	{
		return Axes[axis].Multiplier;
	}

	void SetAxisDeadZone(int axis, float zone)
	{
		Axes[axis].DeadZone = clamp(zone, MIN_DEADZONE, 1.f);
	}
	void SetAxisMap(int axis, EJoyAxis gameaxis)
	{
		Axes[axis].GameAxis = gameaxis;
	}
	void SetAxisScale(int axis, float scale)
	{
		Axes[axis].Multiplier = scale;
	}

	// Used by the saver to not save properties that are at their defaults.
	bool IsSensitivityDefault()
	{
		return Multiplier == 1.0f;
	}
	bool IsAxisDeadZoneDefault(int axis)
	{
		return Axes[axis].DeadZone <= MIN_DEADZONE;
	}
	bool IsAxisMapDefault(int axis)
	{
		if(axis >= 5)
			return Axes[axis].GameAxis == JOYAXIS_None;
		return Axes[axis].GameAxis == DefaultAxes[axis];
	}
	bool IsAxisScaleDefault(int axis)
	{
		return Axes[axis].Multiplier == 1.0f;
	}

	void SetDefaultConfig()
	{
		for(int i = 0;i < GetNumAxes();i++)
		{
			AxisInfo info;
			if(i < NumAxes)
				info.Name.Format("Axis %d", i+1);
			else
				info.Name.Format("Hat %d (%c)", (i-NumAxes)/2 + 1, (i-NumAxes)%2 == 0 ? 'x' : 'y');
			info.DeadZone = DEFAULT_DEADZONE;
			info.Multiplier = 1.0f;
			info.Value = 0.0;
			info.ButtonValue = 0;
			if(i >= 5)
				info.GameAxis = JOYAXIS_None;
			else
				info.GameAxis = DefaultAxes[i];
			Axes.Push(info);
		}
	}
	FString GetIdentifier()
	{
		char id[16];
		snprintf(id, countof(id), "JS:%d", DeviceIndex);
		return id;
	}

	bool GetEnabled()
	{
		return Enabled;
	}
	
	void SetEnabled(bool enabled)
	{
		Enabled = enabled;
	}

	bool AllowsEnabledInBackground() { return false; }
	bool GetEnabledInBackground() { return false; }
	void SetEnabledInBackground(bool enabled) {}


	void AddAxes(float axes[NUM_JOYAXIS])
	{
		// Add to game axes.
		for (int i = 0; i < GetNumAxes(); ++i)
		{
			if(Axes[i].GameAxis != JOYAXIS_None)
				axes[Axes[i].GameAxis] -= float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
		}
	}

	void ProcessInput()
	{
		uint8_t buttonstate;

		for (int i = 0; i < NumAxes; ++i)
		{
			buttonstate = 0;

			Axes[i].Value = SDL_JoystickGetAxis(Device, i)/32767.0;
			Axes[i].Value = Joy_RemoveDeadZone(Axes[i].Value, Axes[i].DeadZone, &buttonstate);

			// Map button to axis
			// X and Y are handled differently so if we have 2 or more axes then we'll use that code instead.
			if (NumAxes == 1 || (i >= 2 && i < NUM_JOYAXISBUTTONS))
			{
				Joy_GenerateButtonEvents(Axes[i].ButtonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
				Axes[i].ButtonValue = buttonstate;
			}
		}

		if(NumAxes > 1)
		{
			buttonstate = Joy_XYAxesToButtons(Axes[0].Value, Axes[1].Value);
			Joy_GenerateButtonEvents(Axes[0].ButtonValue, buttonstate, 4, KEY_JOYAXIS1PLUS);
			Axes[0].ButtonValue = buttonstate;
		}

		// Map POV hats to buttons and axes.  Why axes?  Well apparently I have
		// a gamepad where the left control stick is a POV hat (instead of the
		// d-pad like you would expect, no that's pressure sensitive).  Also
		// KDE's joystick dialog maps them to axes as well.
		for (int i = 0; i < NumHats; ++i)
		{
			AxisInfo &x = Axes[NumAxes + i*2];
			AxisInfo &y = Axes[NumAxes + i*2 + 1];

			buttonstate = SDL_JoystickGetHat(Device, i);

			// If we're going to assume that we can pass SDL's value into
			// Joy_GenerateButtonEvents then we might as well assume the format here.
			if(buttonstate & 0x1) // Up
				y.Value = -1.0;
			else if(buttonstate & 0x4) // Down
				y.Value = 1.0;
			else
				y.Value = 0.0;
			if(buttonstate & 0x2) // Left
				x.Value = 1.0;
			else if(buttonstate & 0x8) // Right
				x.Value = -1.0;
			else
				x.Value = 0.0;

			if(i < 4)
			{
				Joy_GenerateButtonEvents(x.ButtonValue, buttonstate, 4, KEY_JOYPOV1_UP + i*4);
				x.ButtonValue = buttonstate;
			}
		}
	}

protected:
	struct AxisInfo
	{
		FString Name;
		float DeadZone;
		float Multiplier;
		EJoyAxis GameAxis;
		double Value;
		uint8_t ButtonValue;
	};
	static const EJoyAxis DefaultAxes[5];

	int					DeviceIndex;
	int					InstanceID;
	SDL_Joystick		*Device;

	float				Multiplier;
	bool				Enabled = true;
	TArray<AxisInfo>	Axes;
	int					NumAxes;
	int					NumHats;
};
#ifdef __SWITCH__
const EJoyAxis SDLInputJoystick::DefaultAxes[5] = {JOYAXIS_Side, JOYAXIS_Forward, JOYAXIS_Yaw, JOYAXIS_Pitch, JOYAXIS_Up};
#else
// [Nash 4 Feb 2024] seems like on Linux, the third axis is actually the Left Trigger, resulting in the player uncontrollably looking upwards.
const EJoyAxis SDLInputJoystick::DefaultAxes[5] = {JOYAXIS_Side, JOYAXIS_Forward, JOYAXIS_None, JOYAXIS_Yaw, JOYAXIS_Pitch};
#endif

#ifdef __SWITCH__
//==========================================================================
//
// Nintendo Switch controllers
//
// Read straight from libnx rather than through SDL. SDL's Switch driver pops
// up the system controller applet (with 8 player slots) every time any
// controller changes, and reports a sideways Joy-Con's stick only as four
// digital buttons. Reading the pads here avoids both, and lets a sideways
// Joy-Con have its own buttons (JC_*) with their own bindings, so it can be
// set up separately from full controllers.
//
// Device N is controller N+1. Device 0 also includes the console in handheld mode.
//
//==========================================================================

enum
{
	NX_NUM_PADS = 8,
	NX_NUM_AXES = 4,

	// A sideways Joy-Con's buttons, named by where they are when it's held sideways.
	KEY_JC_RIGHT	= KEY_JOY1,
	KEY_JC_DOWN		= KEY_JOY2,
	KEY_JC_UP		= KEY_JOY3,
	KEY_JC_LEFT		= KEY_JOY4,
	KEY_JC_SL		= KEY_JOY5,
	KEY_JC_SR		= KEY_JOY6,
	KEY_JC_STICK	= KEY_JOY7,
	KEY_JC_MENU		= KEY_JOY8,
};

struct NXButtonMap
{
	u64 Button;
	int Key;
};

// Same keys SDL's driver produced, so existing bindings keep working.
static const NXButtonMap NXFullButtons[] =
{
	{ HidNpadButton_A,		KEY_PAD_A },
	{ HidNpadButton_B,		KEY_PAD_B },
	{ HidNpadButton_X,		KEY_PAD_X },
	{ HidNpadButton_Y,		KEY_PAD_Y },
	{ HidNpadButton_StickL,	KEY_PAD_LTHUMB },
	{ HidNpadButton_StickR,	KEY_PAD_RTHUMB },
	{ HidNpadButton_L,		KEY_PAD_LSHOULDER },
	{ HidNpadButton_R,		KEY_PAD_RSHOULDER },
	{ HidNpadButton_ZL,		KEY_PAD_LTRIGGER },
	{ HidNpadButton_ZR,		KEY_PAD_RTRIGGER },
	{ HidNpadButton_Plus,	KEY_PAD_BACK },
	{ HidNpadButton_Minus,	KEY_PAD_START },
	{ HidNpadButton_Left,	KEY_PAD_DPAD_LEFT },
	{ HidNpadButton_Up,		KEY_PAD_DPAD_UP },
	{ HidNpadButton_Right,	KEY_PAD_DPAD_RIGHT },
	{ HidNpadButton_Down,	KEY_PAD_DPAD_DOWN },
};

// The system reports a single Joy-Con's buttons as if it were held upright.
static const NXButtonMap NXLeftJoyConButtons[] =
{
	{ HidNpadButton_Down,	KEY_JC_RIGHT },
	{ HidNpadButton_Left,	KEY_JC_DOWN },
	{ HidNpadButton_Right,	KEY_JC_UP },
	{ HidNpadButton_Up,		KEY_JC_LEFT },
	{ HidNpadButton_LeftSL,	KEY_JC_SL },
	{ HidNpadButton_LeftSR,	KEY_JC_SR },
	{ HidNpadButton_StickL,	KEY_JC_STICK },
	{ HidNpadButton_Minus,	KEY_JC_MENU },
};

static const NXButtonMap NXRightJoyConButtons[] =
{
	{ HidNpadButton_X,		KEY_JC_RIGHT },
	{ HidNpadButton_A,		KEY_JC_DOWN },
	{ HidNpadButton_Y,		KEY_JC_UP },
	{ HidNpadButton_B,		KEY_JC_LEFT },
	{ HidNpadButton_RightSL,KEY_JC_SL },
	{ HidNpadButton_RightSR,KEY_JC_SR },
	{ HidNpadButton_StickR,	KEY_JC_STICK },
	{ HidNpadButton_Plus,	KEY_JC_MENU },
};

class NXInputJoystick : public IJoystickConfig
{
public:
	enum EKind { Full, LeftJoyCon, RightJoyCon };

	NXInputJoystick(int index) : Index(index), Multiplier(1.0f)
	{
		if (index == 0)
			padInitializeDefault(&Pad);	// controller 1 or handheld
		else
			padInitialize(&Pad, (HidNpadIdType)(HidNpadIdType_No1 + index));
		SetDefaultConfig();
	}
	~NXInputJoystick()
	{
		M_SaveJoystickConfig(this);
	}

	int GetInstanceID() const { return Index; }
	bool IsValid() const { return true; }

	FString GetName()
	{
		FString name;
		name.Format("Controller %d", Index + 1);
		return name;
	}
	float GetSensitivity() { return Multiplier; }
	void SetSensitivity(float scale) { Multiplier = scale; }

	int GetNumAxes() { return NX_NUM_AXES; }
	float GetAxisDeadZone(int axis) { return Axes[axis].DeadZone; }
	EJoyAxis GetAxisMap(int axis) { return Axes[axis].GameAxis; }
	const char *GetAxisName(int axis) { return Axes[axis].Name.GetChars(); }
	float GetAxisScale(int axis) { return Axes[axis].Multiplier; }

	void SetAxisDeadZone(int axis, float zone) { Axes[axis].DeadZone = clamp(zone, MIN_DEADZONE, 1.f); }
	void SetAxisMap(int axis, EJoyAxis gameaxis) { Axes[axis].GameAxis = gameaxis; }
	void SetAxisScale(int axis, float scale) { Axes[axis].Multiplier = scale; }

	bool IsSensitivityDefault() { return Multiplier == 1.0f; }
	bool IsAxisDeadZoneDefault(int axis) { return Axes[axis].DeadZone <= MIN_DEADZONE; }
	bool IsAxisMapDefault(int axis) { return Axes[axis].GameAxis == DefaultAxes[axis]; }
	bool IsAxisScaleDefault(int axis) { return Axes[axis].Multiplier == 1.0f; }

	void SetDefaultConfig()
	{
		static const char *const names[NX_NUM_AXES] = { "Left stick X", "Left stick Y", "Right stick X", "Right stick Y" };
		for (int i = 0; i < NX_NUM_AXES; i++)
		{
			Axes[i].Name = names[i];
			Axes[i].DeadZone = MIN_DEADZONE;
			Axes[i].Multiplier = 1.0f;
			Axes[i].GameAxis = DefaultAxes[i];
			Axes[i].Value = 0.0;
			Axes[i].ButtonValue = 0;
		}
	}
	FString GetIdentifier()
	{
		// Same as SDL's, so saved settings carry over.
		char id[16];
		snprintf(id, countof(id), "JS:%d", Index);
		return id;
	}

	bool GetEnabled()
	{
		return Enabled;
	}
	
	void SetEnabled(bool enabled)
	{
		Enabled = enabled;
	}

	bool AllowsEnabledInBackground() { return false; }
	bool GetEnabledInBackground() { return false; }
	void SetEnabledInBackground(bool enabled) {}


	void AddAxes(float axes[NUM_JOYAXIS])
	{
		if (Kind != Full)
		{
			// One stick: up/down walks, left/right turns. Holding +strafe makes it sidestep.
			axes[JOYAXIS_Forward] -= float(Axes[1].Value * Multiplier * Axes[1].Multiplier);
			axes[JOYAXIS_Yaw] -= float(Axes[0].Value * Multiplier * Axes[2].Multiplier);
			return;
		}
		for (int i = 0; i < NX_NUM_AXES; ++i)
		{
			if (Axes[i].GameAxis != JOYAXIS_None)
				axes[Axes[i].GameAxis] -= float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
		}
	}

	void ProcessInput()
	{
		padUpdate(&Pad);
		bool connected = padIsConnected(&Pad);
		Kind = connected ? GetKind() : Full;

		// Sticks, in SDL's convention (Y is positive downwards).
		double raw[NX_NUM_AXES] = {};
		if (connected)
		{
			if (Kind == Full)
			{
				HidAnalogStickState l = padGetStickPos(&Pad, 0), r = padGetStickPos(&Pad, 1);
				raw[0] = l.x / 32767.0;
				raw[1] = -l.y / 32767.0;
				raw[2] = r.x / 32767.0;
				raw[3] = -r.y / 32767.0;
			}
			else if (Kind == LeftJoyCon)
			{
				// Turned a quarter left: the stick's up is now left, its right is now up.
				HidAnalogStickState s = padGetStickPos(&Pad, 0);
				raw[0] = -s.y / 32767.0;
				raw[1] = -s.x / 32767.0;
			}
			else
			{
				// Turned a quarter right: the stick's up is now right, its right is now down.
				HidAnalogStickState s = padGetStickPos(&Pad, 1);
				raw[0] = s.y / 32767.0;
				raw[1] = s.x / 32767.0;
			}
		}

		uint8_t buttonstate;
		for (int i = 0; i < NX_NUM_AXES; ++i)
		{
			buttonstate = 0;
			// Joy-Con sticks rest a little off-centre.
			float deadzone = Kind == Full ? Axes[i].DeadZone : std::max(Axes[i].DeadZone, 0.15f);
			Axes[i].Value = Joy_RemoveDeadZone(raw[i], deadzone, &buttonstate);
			if (i >= 2)
			{
				Joy_GenerateButtonEvents(Axes[i].ButtonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
				Axes[i].ButtonValue = buttonstate;
			}
		}
		// The (left) stick also works as four buttons, which is how the menus are navigated.
		buttonstate = Joy_XYAxesToButtons(Axes[0].Value, Axes[1].Value);
		Joy_GenerateButtonEvents(Axes[0].ButtonValue, buttonstate, 4, KEY_JOYAXIS1PLUS);
		Axes[0].ButtonValue = buttonstate;

		// Buttons
		TArray<int> down;
		if (connected) GetPressedKeys(down);
		for (int key : KeysDown)
		{
			if (down.Find(key) == down.Size()) PostKey(EV_KeyUp, key);
		}
		for (int key : down)
		{
			if (KeysDown.Find(key) == KeysDown.Size()) PostKey(EV_KeyDown, key);
		}
		KeysDown = std::move(down);
	}

	// The controller applet takes over the screen and the controllers. Let go of
	// everything that was held when it opened (so the menu doesn't think the button
	// that opened it is still held and open it again), and ignore buttons still held
	// when it closes until they're released.
	void ResetAfterApplet()
	{
		for (int key : KeysDown) PostKey(EV_KeyUp, key);
		for (auto &axis : Axes)
		{
			axis.Value = 0;
		}
		Joy_GenerateButtonEvents(Axes[0].ButtonValue, 0, 4, KEY_JOYAXIS1PLUS);
		Axes[0].ButtonValue = 0;
		for (int i = 2; i < NX_NUM_AXES; i++)
		{
			Joy_GenerateButtonEvents(Axes[i].ButtonValue, 0, 2, KEY_JOYAXIS1PLUS + i*2);
			Axes[i].ButtonValue = 0;
		}

		padUpdate(&Pad);
		Kind = padIsConnected(&Pad) ? GetKind() : Full;
		KeysDown.Clear();
		if (padIsConnected(&Pad)) GetPressedKeys(KeysDown);
	}

	bool IsSingleJoyCon() const { return Kind != Full; }

protected:
	struct AxisInfo
	{
		FString Name;
		float DeadZone;
		float Multiplier;
		EJoyAxis GameAxis;
		double Value;
		uint8_t ButtonValue;
	};
	static const EJoyAxis DefaultAxes[NX_NUM_AXES];

	EKind GetKind()
	{
		u32 style = padGetStyleSet(&Pad);
		if (style & (HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadHandheld)) return Full;
		if (style & HidNpadStyleTag_NpadJoyLeft) return LeftJoyCon;
		if (style & HidNpadStyleTag_NpadJoyRight) return RightJoyCon;
		return Full;
	}

	void GetPressedKeys(TArray<int> &keys)
	{
		const NXButtonMap *map = NXFullButtons;
		size_t count = countof(NXFullButtons);
		if (Kind == LeftJoyCon) { map = NXLeftJoyConButtons; count = countof(NXLeftJoyConButtons); }
		else if (Kind == RightJoyCon) { map = NXRightJoyConButtons; count = countof(NXRightJoyConButtons); }

		u64 buttons = padGetButtons(&Pad);
		for (size_t i = 0; i < count; i++)
		{
			if (buttons & map[i].Button) keys.Push(map[i].Key);
		}
	}

	void PostKey(EGenericEvent type, int key)
	{
		event_t ev = {};
		ev.type = type;
		ev.data1 = key;
		D_PostEvent(&ev);
	}

	int					Index;
	PadState			Pad;
	EKind				Kind = Full;
	float				Multiplier;
	bool				Enabled = true;
	AxisInfo			Axes[NX_NUM_AXES];
	TArray<int>			KeysDown;
};
const EJoyAxis NXInputJoystick::DefaultAxes[NX_NUM_AXES] = { JOYAXIS_Side, JOYAXIS_Forward, JOYAXIS_Yaw, JOYAXIS_Pitch };

typedef NXInputJoystick InputJoystick;
#else
typedef SDLInputJoystick InputJoystick;
#endif

class SDLInputJoystickManager
{
public:
	SDLInputJoystickManager()
	{
#ifdef __SWITCH__
		// Up to eight controllers of any standard kind; single Joy-Cons are held sideways.
		padConfigureInput(NX_NUM_PADS, HidNpadStyleSet_NpadStandard);
		hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);
		for (int i = 0; i < NX_NUM_PADS; i++)
		{
			Joysticks.Push(new NXInputJoystick(i));
		}
#else
		for(int i = 0;i < SDL_NumJoysticks();i++)
		{
			SDLInputJoystick *device = new SDLInputJoystick(i);
			if(device->IsValid())
				Joysticks.Push(device);
			else
				delete device;
		}
#endif
	}
	~SDLInputJoystickManager()
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
			delete Joysticks[i];
	}

	void AddAxes(float axes[NUM_JOYAXIS])
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
		{
			// [split-screen] only the controllers of the player whose input is being built
			if (Joy_AcceptDevice == nullptr || Joy_AcceptDevice(i))
				Joysticks[i]->AddAxes(axes);
		}
	}

	int DeviceFromInstance(int instance) const
	{
		for (unsigned int i = 0; i < Joysticks.Size(); i++)
		{
			if (Joysticks[i]->GetInstanceID() == instance)
				return i;
		}
		return -1;
	}
	void GetDevices(TArray<IJoystickConfig *> &sticks)
	{
		for(unsigned int i = 0;i < Joysticks.Size();i++)
		{
			M_LoadJoystickConfig(Joysticks[i]);
			sticks.Push(Joysticks[i]);
		}
	}

	void ProcessInput() const
	{
		for(unsigned int i = 0;i < Joysticks.Size();++i)
		{
			// [split-screen] tag generated axis button events with the controller they came from
			D_PostEventSource = i;
			if(Joysticks[i]->GetEnabled()) Joysticks[i]->ProcessInput();
		}
		D_PostEventSource = -1;
	}

#ifdef __SWITCH__
	void ResetAfterApplet() const
	{
		for (unsigned int i = 0; i < Joysticks.Size(); ++i)
		{
			D_PostEventSource = i;
			Joysticks[i]->ResetAfterApplet();
		}
		D_PostEventSource = -1;
	}
#endif
protected:
	TArray<InputJoystick *> Joysticks;
};
static SDLInputJoystickManager *JoystickManager;

void I_StartupJoysticks()
{
#if defined(__SWITCH__)
	JoystickManager = new SDLInputJoystickManager();
#elif !defined(NO_SDL_JOYSTICK)
	if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) >= 0)
		JoystickManager = new SDLInputJoystickManager();
#endif
}
void I_ShutdownInput()
{
	if(JoystickManager)
	{
		delete JoystickManager;
		JoystickManager = nullptr;
#ifndef __SWITCH__
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
#endif
	}
}

#ifdef __SWITCH__
//==========================================================================
//
// Shows the system's controller setup screen for up to maxplayers players.
// Returns the number of controllers connected, or -1 if it couldn't be shown.
//
//==========================================================================

int I_ShowControllerSetup(int maxplayers)
{
	HidLaControllerSupportArg arg;
	hidLaCreateControllerSupportArg(&arg);
	arg.hdr.player_count_min = 1;
	arg.hdr.player_count_max = (s8)clamp(maxplayers, 1, 4);
	arg.hdr.enable_take_over_connection = 1;
	arg.hdr.enable_left_justify = 1;
	arg.hdr.enable_permit_joy_dual = 1;

	HidLaControllerSupportResultInfo info = {};
	Result rc = hidLaShowControllerSupportForSystem(&info, &arg, false);

	if (JoystickManager) JoystickManager->ResetAfterApplet();
	// The game was frozen while the applet was up; don't make it catch up.
	I_ResetFrameTime();

	return R_SUCCEEDED(rc) ? info.player_count : -1;
}
#endif

void I_GetJoysticks(TArray<IJoystickConfig *> &sticks)
{
	sticks.Clear();

	if (JoystickManager)
		JoystickManager->GetDevices(sticks);
}

// On the Switch the controllers are the only way to play or to use the menus, so they
// are always read. (use_joystick is off by default, and with a fresh config nothing would
// respond, the option to turn it on included.)
static inline bool JoystickEnabled()
{
#ifdef __SWITCH__
	return true;
#else
	return use_joystick;
#endif
}

void I_GetAxes(float axes[NUM_JOYAXIS])
{
	for (int i = 0; i < NUM_JOYAXIS; ++i)
	{
		axes[i] = 0;
	}
	if (JoystickEnabled() && JoystickManager)
	{
		JoystickManager->AddAxes(axes);
	}
}

void I_ProcessJoysticks()
{
	if (JoystickEnabled() && JoystickManager)
		JoystickManager->ProcessInput();
}

IJoystickConfig *I_UpdateDeviceList()
{
	return NULL;
}

// [split-screen] Maps an SDL joystick instance ID to the controller index used for event sources.
int I_JoystickDeviceFromInstance(int instance)
{
	if (JoystickManager)
		return JoystickManager->DeviceFromInstance(instance);
	return -1;
}
