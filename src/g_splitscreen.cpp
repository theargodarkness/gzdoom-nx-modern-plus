//-----------------------------------------------------------------------------
//
// Copyright 2026 GZDoom-NX contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// Local split-screen co-op for up to four players. See g_splitscreen.h for
// an overview of how it hooks into the engine.
//
//-----------------------------------------------------------------------------

#include "g_splitscreen.h"
#include "doomstat.h"
#include "d_player.h"
#include "d_net.h"
#include "d_netinf.h"
#include "d_protocol.h"
#include "d_event.h"
#include "d_main.h"
#include "c_buttons.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "g_game.h"
#include "g_level.h"
#include "g_levellocals.h"
#include "gi.h"
#include "sbar.h"
#include "hu_stuff.h"
#include "d_buttons.h"
#include "r_utility.h"
#include "r_state.h"
#include "r_sky.h"
#include "v_video.h"
#include "v_draw.h"
#include "v_font.h"
#include "v_2ddrawer.h"
#include "m_joy.h"
#include "menu.h"
#include "serializer.h"
#include "p_setup.h"
#include "intrect.h"
#include "texturemanager.h"
#include "r_data/r_translate.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hw_cvars.h"

#ifdef __SWITCH__
int I_ShowControllerSetup(int maxplayers);	// i_joystick.cpp
#endif

#include <functional>

//-----------------------------------------------------------------------------
//
// External things this module needs
//
//-----------------------------------------------------------------------------

void G_BuildTiccmd(ticcmd_t *cmd);
uint8_t *Net_GetLocalSlotSpecials(int slot, int tic, int *len);
bool RenderSplitViews(int numviews, const std::function<player_t*(int)>& beginview, const std::function<void(int, sector_t*)>& endview);
DBaseStatusBar *ST_CreateExtraStatusBar(int playernum);
void V_DrawBlend(sector_t* viewsector);
void AM_DrawSplit(int bottom, bool start);
bool AM_SplitAllowed();

extern int turnheld;
extern float mousex, mousey;
extern bool SendLand;
extern bool sendturn180;
extern bool DrawFSHUD;
extern int UIScaleOverride;

EXTERN_CVAR(Bool, hud_althud)
EXTERN_CVAR(Int, screenblocks)

//-----------------------------------------------------------------------------
//
// Settings
//
//-----------------------------------------------------------------------------

CUSTOM_CVAR(Int, splitscreen_players, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 2) self = 2;
	else if (self > SS_MAXLOCALPLAYERS) self = SS_MAXLOCALPLAYERS;
}

// Two-player layout: 0 = one above the other, 1 = side by side
CVAR(Int, splitscreen_layout, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// Draw thin black lines between the views
CVAR(Bool, splitscreen_dividers, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// HUD scale inside the split views. 0 picks the largest scale that fits a 320x200 screen into a view.
CVAR(Int, splitscreen_hudscale, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// On computers: the keyboard and mouse control player 1 and the first game controller controls player 2.
// Off (the default, and how it works on the Switch): game controller N controls player N.
CVAR(Bool, splitscreen_keyboard, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

//-----------------------------------------------------------------------------
//
// State
//
//-----------------------------------------------------------------------------

int SS_NumLocal;
int SS_LocalPlayers[SS_MAXLOCALPLAYERS] = { 0, 1, 2, 3 };
int SS_ActiveSlot;

static int ss_pendingNewGame;			// number of players for the next new game
static bool ss_hooksInstalled;

// Everything that holds a local player's not yet sent input. These are globals
// that G_Responder and G_BuildTiccmd work with, so they get swapped in and out.
struct FLocalInput
{
	TArray<FButtonStatus> Buttons;
	int turnheld = 0;
	angle_t LocalViewAngle = 0;
	int LocalViewPitch = 0;
	bool LocalKeyboardTurner = false;
	float mousex = 0, mousey = 0;
	bool SendLand = false;
	bool sendturn180 = false;
	const AActor *SendItemUse = nullptr;
	const AActor *SendItemDrop = nullptr;
	int SendItemDropAmount = 0;
};

static FLocalInput ss_input[SS_MAXLOCALPLAYERS];

// Per player automap
static bool ss_automap[SS_MAXLOCALPLAYERS];
static bool ss_automapstart[SS_MAXLOCALPLAYERS];

// Status bars of the extra players (index 0 is unused, player 1 uses the regular StatusBar)
static DBaseStatusBar *ss_bars[SS_MAXLOCALPLAYERS];
static DBaseStatusBar *ss_mainbar;		// the regular StatusBar while another player's is selected
static bool ss_barswapped;

// The view being rendered
static int ss_renderslot = -1;
static IntRect ss_scenerect;

//-----------------------------------------------------------------------------
//
// Slot lookups
//
//-----------------------------------------------------------------------------

int SS_SlotForPlayer(int playernum)
{
	if (!SS_Active()) return -1;
	for (int i = 0; i < SS_NumLocal; i++)
	{
		if (SS_LocalPlayers[i] == playernum) return i;
	}
	return -1;
}

int SS_SlotForEventSource(int source)
{
	if (!SS_Active()) return 0;
	if (source < 0) return 0;	// keyboard and mouse always belong to player 1
	int slot = source + (splitscreen_keyboard ? 1 : 0);
	if (slot >= SS_NumLocal) return -1;
	return slot;
}

//-----------------------------------------------------------------------------
//
// Input context switching
//
//-----------------------------------------------------------------------------

static void SaveInput(FLocalInput &in)
{
	buttonMap.GetStates(in.Buttons);
	in.turnheld = turnheld;
	in.LocalViewAngle = LocalViewAngle;
	in.LocalViewPitch = LocalViewPitch;
	in.LocalKeyboardTurner = LocalKeyboardTurner;
	in.mousex = mousex;
	in.mousey = mousey;
	in.SendLand = SendLand;
	in.sendturn180 = sendturn180;
	in.SendItemUse = SendItemUse;
	in.SendItemDrop = SendItemDrop;
	in.SendItemDropAmount = SendItemDropAmount;
}

static void LoadInput(const FLocalInput &in)
{
	buttonMap.SetStates(in.Buttons);
	turnheld = in.turnheld;
	LocalViewAngle = in.LocalViewAngle;
	LocalViewPitch = in.LocalViewPitch;
	LocalKeyboardTurner = in.LocalKeyboardTurner;
	mousex = in.mousex;
	mousey = in.mousey;
	SendLand = in.SendLand;
	sendturn180 = in.sendturn180;
	SendItemUse = in.SendItemUse;
	SendItemDrop = in.SendItemDrop;
	SendItemDropAmount = in.SendItemDropAmount;
}

static void ResetInput(FLocalInput &in)
{
	in = FLocalInput();
	buttonMap.GetStates(in.Buttons);
	ButtonMap::ResetStates(in.Buttons);
}

int SS_SelectSlot(int slot)
{
	int prev = SS_ActiveSlot;
	if (slot == prev || slot < 0 || slot >= SS_MAXLOCALPLAYERS) return prev;
	if (slot != 0 && slot >= SS_NumLocal) return prev;

	SaveInput(ss_input[prev]);
	LoadInput(ss_input[slot]);

	if (ss_barswapped)
	{
		StatusBar = ss_mainbar;
		ss_mainbar = nullptr;
		ss_barswapped = false;
	}
	if (slot != 0 && ss_bars[slot] != nullptr)
	{
		ss_mainbar = StatusBar;
		StatusBar = ss_bars[slot];
		ss_barswapped = true;
	}

	consoleplayer = SS_LocalPlayers[slot];
	SS_ActiveSlot = slot;
	return prev;
}

void SS_ResetInputStates()
{
	for (int i = 0; i < SS_MAXLOCALPLAYERS; i++)
	{
		if (i != SS_ActiveSlot) ButtonMap::ResetStates(ss_input[i].Buttons);
	}
}

static bool SS_AcceptJoystick(int device)
{
	return !SS_Active() || SS_SlotForEventSource(device) == SS_ActiveSlot;
}

static void InstallHooks()
{
	if (ss_hooksInstalled) return;
	ss_hooksInstalled = true;
	Joy_AcceptDevice = SS_AcceptJoystick;
	ButtonMap::OnResetButtonStates = SS_ResetInputStates;
	GC::AddMarkerFunc(SS_MarkStatusBars);
}

//-----------------------------------------------------------------------------
//
// Game setup
//
//-----------------------------------------------------------------------------

void SS_RequestNewGame(int numplayers)
{
	ss_pendingNewGame = numplayers;
}

int SS_ConsumePendingNewGame()
{
	int n = ss_pendingNewGame;
	ss_pendingNewGame = 0;
	return netgame ? 0 : n;
}

void SS_MenuClosed()
{
	// Backing out of the menus without starting a game cancels a split-screen new game request.
	if (gameaction != ga_newgame && gameaction != ga_newgame2)
	{
		ss_pendingNewGame = 0;
	}
}

//-----------------------------------------------------------------------------
//
// Menu entries
//
// Mods often replace the main menu (and sometimes the options menu) with
// their own, which would hide the split-screen entry. After all MENUDEFs are
// parsed, make sure every main/options menu has a way to reach it.
//
//-----------------------------------------------------------------------------

static const FName NAME_SplitscreenMenu("SplitscreenMenu");
static const char *const SS_MENU_LABEL = "$SPLITMNU_MAINITEM";

static bool MenuHasSplitEntry(DMenuDescriptor *desc)
{
	for (auto item : desc->mItems)
	{
		if (item != nullptr && item->mAction == NAME_SplitscreenMenu) return true;
	}
	return false;
}

static void InjectIntoListMenu(DListMenuDescriptor *ld)
{
	auto &items = ld->mItems;
	int newgame = -1, quit = -1;
	for (unsigned i = 0; i < items.Size(); i++)
	{
		if (items[i] == nullptr) continue;
		if (newgame < 0 && items[i]->mAction == NAME_Playerclassmenu) newgame = i;
		if (items[i]->mAction == NAME_Quitmenu) quit = i;
	}

	// Put it right below "New Game", or above "Quit" if there is no "New Game".
	int anchor = newgame >= 0 ? newgame : quit;
	if (anchor < 0) return;

	double spacing = ld->mLinespacing;
	for (unsigned i = anchor + 1; i < items.Size(); i++)
	{
		if (items[i] != nullptr && items[i]->mAction != NAME_None)
		{
			double d = items[i]->mYpos - items[anchor]->mYpos;
			if (d > 0) spacing = d;
			break;
		}
	}
	if (spacing <= 0) return;

	int insertAt = newgame >= 0 ? newgame + 1 : quit;
	double x = items[anchor]->mXpos;
	double y = newgame >= 0 ? items[anchor]->mYpos + spacing : items[anchor]->mYpos;

	// Everything from the insertion point down moves one line lower; leave the menu alone if that won't fit.
	double lowest = y;
	for (unsigned i = insertAt; i < items.Size(); i++)
	{
		if (items[i] != nullptr && items[i]->mYpos >= y) lowest = std::max(lowest, items[i]->mYpos + spacing);
	}
	if (lowest + spacing > 200) return;

	for (unsigned i = insertAt; i < items.Size(); i++)
	{
		if (items[i] != nullptr && items[i]->mYpos >= y) items[i]->OffsetPositionY(int(spacing));
	}

	FFont *font = ld->mFont ? ld->mFont : BigFont;
	auto it = CreateListMenuItemText(x, y, int(spacing), 'c', SS_MENU_LABEL, font, ld->mFontColor, ld->mFontColor2, NAME_SplitscreenMenu, 0);
	items.Insert(insertAt, it);
	if (ld->mSelectedItem >= insertAt) ld->mSelectedItem++;
	GC::WriteBarrier(ld, it);
}

static void InjectIntoOptionMenu(DOptionMenuDescriptor *od)
{
	auto &items = od->mItems;
	unsigned insertAt = 0;
	for (unsigned i = 0; i < items.Size(); i++)
	{
		if (items[i] != nullptr && items[i]->mAction == NAME_NewPlayerMenu)
		{
			insertAt = i + 1;
			break;
		}
	}
	auto it = CreateOptionMenuItemSubmenu(SS_MENU_LABEL, NAME_SplitscreenMenu, 0);
	items.Insert(insertAt, it);
	if (od->mSelectedItem >= int(insertAt)) od->mSelectedItem++;
	GC::WriteBarrier(od, it);
}

void SS_InjectMenuEntries()
{
	if (MenuDescriptors.CheckKey(NAME_SplitscreenMenu) == nullptr) return;

	for (FName name : { NAME_Mainmenu, NAME_MainmenuTextOnly })
	{
		DMenuDescriptor **desc = MenuDescriptors.CheckKey(name);
		if (desc == nullptr || *desc == nullptr || MenuHasSplitEntry(*desc)) continue;
		if ((*desc)->IsKindOf(RUNTIME_CLASS(DListMenuDescriptor)))
		{
			InjectIntoListMenu(static_cast<DListMenuDescriptor *>(*desc));
		}
		else if ((*desc)->IsKindOf(RUNTIME_CLASS(DOptionMenuDescriptor)))
		{
			InjectIntoOptionMenu(static_cast<DOptionMenuDescriptor *>(*desc));
		}
	}

	// Always reachable from the options menu too, whatever the main menu looks like.
	for (FName name : { NAME_Optionsmenu, NAME_OptionsmenuSimple })
	{
		DMenuDescriptor **desc = MenuDescriptors.CheckKey(name);
		if (desc == nullptr || *desc == nullptr || MenuHasSplitEntry(*desc)) continue;
		if ((*desc)->IsKindOf(RUNTIME_CLASS(DOptionMenuDescriptor)))
		{
			InjectIntoOptionMenu(static_cast<DOptionMenuDescriptor *>(*desc));
		}
	}
}

static void SetupLocalUserInfo(int slot, int pnum)
{
	static const char *const colors[SS_MAXLOCALPLAYERS] = { nullptr, "70 70 ff", "a0 68 30", "ff 30 30" };

	// Start out with a copy of player 1's settings...
	FString info = D_GetUserInfoStrings(SS_LocalPlayers[0], false);

	// ...and give the player its own name and color. Later keys override earlier ones.
	info.AppendFormat("\\name\\Player %d\\color\\%s", slot + 1, colors[slot]);

	uint8_t *infop = (uint8_t *)info.LockBuffer();
	D_ReadUserInfoStrings(pnum, &infop, false);
	info.UnlockBuffer();
	R_BuildPlayerTranslation(pnum);
}

static void ActivateLocalPlayers(int numplayers)
{
	InstallHooks();

	SS_NumLocal = numplayers;
	SS_ActiveSlot = 0;
	SS_LocalPlayers[0] = consoleplayer;

	// The other local players take the lowest free player numbers.
	int next = 0;
	for (int slot = 1; slot < numplayers; slot++)
	{
		while (next == consoleplayer || playeringame[next]) next++;
		SS_LocalPlayers[slot] = next++;
	}

	for (int i = 0; i < SS_MAXLOCALPLAYERS; i++)
	{
		ResetInput(ss_input[i]);
		ss_automap[i] = false;
		ss_automapstart[i] = false;
	}

	for (int slot = 1; slot < numplayers; slot++)
	{
		int p = SS_LocalPlayers[slot];
		playeringame[p] = true;
		players[p].playerstate = PST_ENTER;
		SetupLocalUserInfo(slot, p);
	}

	multiplayer = true;		// co-op rules: respawning, multiplayer things, co-op intermission
	automapactive = false;
	viewactive = true;
}

void SS_SetupGame(int numplayers)
{
	SS_Shutdown();
	numplayers = clamp(numplayers, 1, (int)SS_MAXLOCALPLAYERS);
	if (numplayers < 2 || netgame || demoplayback || demorecording || ticdup != 1)
	{
		return;
	}

	deathmatch = 0;
	teamplay = false;
	ActivateLocalPlayers(numplayers);
	Printf("Split-screen co-op with %d players\n", numplayers);
}

void SS_Shutdown()
{
	if (SS_ActiveSlot != 0)
	{
		SS_SelectSlot(0);
	}
	bool wasactive = SS_Active();
	SS_DestroyStatusBars();
	if (wasactive)
	{
		for (int slot = 1; slot < SS_NumLocal; slot++)
		{
			playeringame[SS_LocalPlayers[slot]] = false;
		}
		if (!netgame) multiplayer = false;
		automapactive = false;
		viewactive = true;
		setsizeneeded = true;
		// The main status bar was laid out for a quarter of the screen.
		if (StatusBar != nullptr)
		{
			StatusBar->CallScreenSizeChanged();
		}
	}
	SS_NumLocal = 0;
	SS_ActiveSlot = 0;
	ss_renderslot = -1;
	for (int i = 0; i < SS_MAXLOCALPLAYERS; i++)
	{
		ss_automap[i] = false;
	}
}

void SS_SerializeGlobals(FSerializer &arc)
{
	if (arc.isWriting())
	{
		if (SS_Active())
		{
			int n = SS_NumLocal;
			arc("splitscreenplayers", n);
		}
	}
	else
	{
		int n = 0;
		arc("splitscreenplayers", n);
		ss_pendingNewGame = 0;
		SS_Shutdown();
		if (n > 1 && !netgame && !demoplayback)
		{
			// Bring the local players back before the savegame's players are read.
			ActivateLocalPlayers(clamp(n, 2, (int)SS_MAXLOCALPLAYERS));
		}
	}
}

//-----------------------------------------------------------------------------
//
// Input
//
//-----------------------------------------------------------------------------

bool SS_Responder(event_t *ev)
{
	if (!SS_Active() || SS_ActiveSlot != 0)
	{
		return false;
	}
	int slot = SS_SlotForEventSource(D_EventSource);
	if (slot == 0)
	{
		return false;
	}
	if (slot < 0)
	{
		return true;	// a controller that does not belong to anybody
	}
	FSplitSlotScope scope(slot);
	G_Responder(ev);
	return true;
}

void SS_BuildTiccmds()
{
	for (int slot = 1; slot < SS_NumLocal; slot++)
	{
		ticcmd_t cmd;
		{
			FSplitSlotScope scope(slot);
			G_BuildTiccmd(&cmd);
		}
		// There is no network in between, so this goes right where the network code would put it.
		netcmds[SS_LocalPlayers[slot]][(maketic / ticdup) % BACKUPTICS] = cmd;
	}
}

void SS_CommitTic(int tic)
{
	for (int slot = 1; slot < SS_NumLocal; slot++)
	{
		int len;
		uint8_t *data = Net_GetLocalSlotSpecials(slot, tic, &len);
		NetSpecs[SS_LocalPlayers[slot]][tic % BACKUPTICS].SetData(len > 0 ? data : nullptr, len);
	}
}

bool SS_ToggleAutomap()
{
	if (!SS_Active())
	{
		return false;
	}
	if (AM_SplitAllowed())
	{
		int slot = SS_ActiveSlot;
		ss_automap[slot] = !ss_automap[slot];
		ss_automapstart[slot] = ss_automap[slot];
	}
	return true;
}

//-----------------------------------------------------------------------------
//
// Status bars
//
//-----------------------------------------------------------------------------

void SS_CreateStatusBars(bool bTitleLevel)
{
	SS_DestroyStatusBars();
	if (!SS_Active() || bTitleLevel)
	{
		return;
	}
	for (int slot = 1; slot < SS_NumLocal; slot++)
	{
		ss_bars[slot] = ST_CreateExtraStatusBar(SS_LocalPlayers[slot]);
	}
}

void SS_DestroyStatusBars()
{
	if (ss_barswapped)
	{
		StatusBar = ss_mainbar;
		ss_mainbar = nullptr;
		ss_barswapped = false;
	}
	for (auto &bar : ss_bars)
	{
		if (bar != nullptr)
		{
			bar->Destroy();
			bar = nullptr;
		}
	}
}

void SS_TickStatusBars()
{
	for (int slot = 1; slot < SS_NumLocal; slot++)
	{
		if (ss_bars[slot] != nullptr)
		{
			FSplitSlotScope scope(slot);
			StatusBar->CallTick();
		}
	}
}

void SS_AttachStatusBars()
{
	for (int slot = 1; slot < SS_NumLocal; slot++)
	{
		if (ss_bars[slot] != nullptr)
		{
			ss_bars[slot]->AttachToPlayer(&players[SS_LocalPlayers[slot]]);
		}
	}
}



void SS_MarkStatusBars()
{
	for (auto &bar : ss_bars) GC::Mark(bar);
	GC::Mark(ss_mainbar);
}

//-----------------------------------------------------------------------------
//
// Screen layout
//
//-----------------------------------------------------------------------------

// Returns the part of the screen (top-left origin) a local player's view goes into.
bool SS_GetViewRect(int slot, int &x, int &y, int &w, int &h)
{
	if (!SS_Active() || slot < 0 || slot >= SS_MAXLOCALPLAYERS || screen == nullptr)
	{
		return false;
	}
	int W = screen->GetWidth();
	int H = screen->GetHeight();
	int gap = splitscreen_dividers ? std::max(2, H / 360) : 0;

	// Splits a length into two parts with a gap in between.
	auto split = [gap](int len, int part, int &start, int &size)
	{
		int first = (len - gap) / 2;
		if (part == 0) { start = 0; size = first; }
		else { start = first + gap; size = len - start; }
	};

	if (SS_NumLocal == 2)
	{
		if (splitscreen_layout == 1)
		{
			split(W, slot, x, w);
			y = 0; h = H;
		}
		else
		{
			split(H, slot, y, h);
			x = 0; w = W;
		}
	}
	else
	{
		split(W, slot & 1, x, w);
		split(H, slot >> 1, y, h);
	}
	return true;
}

bool SS_GetSceneRect(IntRect &rect)
{
	if (ss_renderslot < 0) return false;
	rect = ss_scenerect;
	return true;
}

bool SS_InSplitView()
{
	return ss_renderslot >= 0;
}

//-----------------------------------------------------------------------------
//
// Saved global view and 2D state
//
//-----------------------------------------------------------------------------

struct FViewGlobals
{
	int viewwindowx, viewwindowy, viewwidth, viewheight, freelookviewheight;
	FViewWindow viewwindow;
	DAngle fov;
	bool drawfshud;

	void Save()
	{
		viewwindowx = ::viewwindowx;
		viewwindowy = ::viewwindowy;
		viewwidth = ::viewwidth;
		viewheight = ::viewheight;
		freelookviewheight = ::freelookviewheight;
		viewwindow = r_viewwindow;
		fov = r_viewpoint.FieldOfView;
		drawfshud = DrawFSHUD;
	}
	void Restore()
	{
		::viewwindowx = viewwindowx;
		::viewwindowy = viewwindowy;
		::viewwidth = viewwidth;
		::viewheight = viewheight;
		::freelookviewheight = freelookviewheight;
		r_viewwindow = viewwindow;
		r_viewpoint.FieldOfView = fov;
		DrawFSHUD = drawfshud;
	}
};

// Sets up the view window globals for a view of the given size (what R_SetWindow does for the whole screen).
static void SetViewWindow(int w, int h)
{
	viewwidth = w;
	viewheight = h;
	freelookviewheight = h;

	auto &vw = r_viewwindow;
	vw.WidescreenRatio = w / (float)h;
	vw.centery = h / 2;
	vw.centerx = w / 2;
	if (AspectTallerThanWide(vw.WidescreenRatio))
	{
		vw.centerxwide = vw.centerx;
	}
	else
	{
		vw.centerxwide = vw.centerx * AspectMultiplier(vw.WidescreenRatio) / 48;
	}

	DAngle fov = r_viewpoint.FieldOfView;
	if (vw.centerxwide != vw.centerx)
	{
		fov = DAngle::fromRad(2 * atan(vw.centerx * tan(fov.Radians() / 2) / double(vw.centerxwide)));
		if (fov > DAngle::fromDeg(170.)) fov = DAngle::fromDeg(170.);
	}
	vw.FocalTangent = tan(fov.Radians() / 2);
}

// Makes the 2D drawer, the clean scaling factors and the view window globals describe
// one player's part of the screen, so HUD code can draw as if it had the whole screen.
struct F2DViewContext
{
	int savedwidth, savedheight;
	DVector2 savedoffset;
	int savedxfac, savedyfac, savedcleanw, savedcleanh;
	int savedscale;
	FViewGlobals savedview;

	F2DViewContext(int x, int y, int w, int h, int viewh)
	{
		savedwidth = twod->GetWidth();
		savedheight = twod->GetHeight();
		savedxfac = CleanXfac;
		savedyfac = CleanYfac;
		savedcleanw = CleanWidth;
		savedcleanh = CleanHeight;
		savedscale = UIScaleOverride;
		savedview.Save();

		twod->SetSize(w, h);
		savedoffset = twod->SetOffset(DVector2(x, y));

		CleanXfac = std::max(std::min(w / 400, h / 240), 1);
		if (CleanXfac >= 4) CleanXfac--;
		CleanYfac = CleanXfac;
		CleanWidth = w / CleanXfac;
		CleanHeight = h / CleanYfac;

		UIScaleOverride = splitscreen_hudscale > 0 ? (int)splitscreen_hudscale : std::max(1, std::min(w / 320, h / 200));

		::viewwindowx = 0;
		::viewwindowy = 0;
		SetViewWindow(w, viewh);
		DrawFSHUD = (screenblocks == 11);
	}

	~F2DViewContext()
	{
		twod->SetSize(savedwidth, savedheight);
		twod->SetOffset(savedoffset);
		CleanXfac = savedxfac;
		CleanYfac = savedyfac;
		CleanWidth = savedcleanw;
		CleanHeight = savedcleanh;
		UIScaleOverride = savedscale;
		savedview.Restore();
	}
};

// Lets the current StatusBar calculate its layout for the current (view sized) 2D area.
// This is cheap, and doing it every frame picks up any change of the HUD settings right away.
static void UpdateStatusBarGeometry()
{
	if (StatusBar != nullptr)
	{
		StatusBar->CallScreenSizeChanged();
	}
}

static FTextureID GetBorderFlat()
{
	return TexMan.CheckForTexture(gameinfo.BorderFlat.GetChars(), ETextureType::Flat);
}

//-----------------------------------------------------------------------------
//
// Draws one player's HUD into the current 2D view context.
// Mirrors what D_Display does for the full screen.
//
//-----------------------------------------------------------------------------

static void DrawPlayerHUD(int slot, int w, int h, int viewh, sector_t *viewsec, double ticfrac)
{
	if (hud_toggled || StatusBar == nullptr || players[consoleplayer].camera == nullptr)
	{
		return;
	}

	// Some HUD code looks at the camera of the last rendered view.
	AActor *savedcamera = r_viewpoint.camera;
	r_viewpoint.camera = players[consoleplayer].camera;

	bool mapup = ss_automap[slot] && AM_SplitAllowed();

	// The status bar area is not covered by the 3D view.
	if (viewh < h)
	{
		Dim(twod, 0, 1.f, 0, viewh, w, h - viewh);
	}

	if (viewsec != nullptr)
	{
		V_DrawBlend(viewsec);
	}

	if (mapup)
	{
		int bottom = (hud_althud && viewh == h) ? viewh : StatusBar->GetTopOfStatusbar();
		AM_DrawSplit(bottom, ss_automapstart[slot]);
		ss_automapstart[slot] = false;
	}

	// The HUD code checks these to decide whether to draw the automap HUD.
	bool savedam = automapactive;
	bool savedview = viewactive;
	automapactive = mapup;
	viewactive = !mapup;

	if (hud_althud && viewh == h && screenblocks > 10)
	{
		StatusBar->DrawBottomStuff(HUD_AltHud);
		if (DrawFSHUD || mapup) StatusBar->DrawAltHUD();
		if (players[consoleplayer].camera && players[consoleplayer].camera->player && !mapup)
		{
			StatusBar->DrawCrosshair(ticfrac);
		}
		StatusBar->CallDraw(HUD_AltHud, ticfrac);
		StatusBar->DrawTopStuff(HUD_AltHud);
	}
	else if (viewh == h && !mapup && screenblocks > 10)
	{
		EHudState state = DrawFSHUD ? HUD_Fullscreen : HUD_None;
		StatusBar->DrawBottomStuff(state);
		StatusBar->CallDraw(state, ticfrac);
		StatusBar->DrawTopStuff(state);
	}
	else
	{
		StatusBar->DrawBottomStuff(HUD_StatusBar);
		StatusBar->CallDraw(HUD_StatusBar, ticfrac);
		StatusBar->DrawTopStuff(HUD_StatusBar);
	}

	automapactive = savedam;
	viewactive = savedview;

	// The scoreboard, while the player holds the button or is dead (CT_Drawer does this in single view mode).
	if (players[consoleplayer].camera != nullptr &&
		(buttonMap.ButtonDown(Button_ShowScores) || players[consoleplayer].camera->health <= 0 || SB_ForceActive))
	{
		HU_DrawScores(&players[consoleplayer]);
	}
	r_viewpoint.camera = savedcamera;
}

//-----------------------------------------------------------------------------
//
// SS_Display
//
// Renders the 3D views of all local players and then their HUDs.
// Called by D_Display in place of the single view.
//
//-----------------------------------------------------------------------------

void SS_Display()
{
	const int n = SS_NumLocal;
	const int H = screen->GetHeight();
	int rx[SS_MAXLOCALPLAYERS], ry[SS_MAXLOCALPLAYERS], rw[SS_MAXLOCALPLAYERS], rh[SS_MAXLOCALPLAYERS];
	int viewh[SS_MAXLOCALPLAYERS];
	sector_t *viewsec[SS_MAXLOCALPLAYERS] = {};
	bool rendered[SS_MAXLOCALPLAYERS] = {};

	FViewGlobals saved;
	saved.Save();

	// Work out each view's size. When the status bar is shown, the 3D view sits above it.
	for (int slot = 0; slot < n; slot++)
	{
		SS_GetViewRect(slot, rx[slot], ry[slot], rw[slot], rh[slot]);
		FSplitSlotScope scope(slot);
		F2DViewContext ctx(rx[slot], ry[slot], rw[slot], rh[slot], rh[slot]);
		UpdateStatusBarGeometry();
		viewh[slot] = rh[slot];
		if (screenblocks <= 10 && StatusBar != nullptr)
		{
			viewh[slot] = clamp(StatusBar->GetTopOfStatusbar(), rh[slot] / 2, rh[slot]);
		}
	}

	if (V_IsHardwareRenderer())
	{
		int prevslot = 0;
		D_Render([&]()
		{
			RenderSplitViews(n, [&](int slot) -> player_t*
			{
				prevslot = SS_SelectSlot(slot);
				player_t *player = &players[consoleplayer];
				if (player->camera == nullptr)
				{
					player->camera = player->mo;
				}
				if (player->camera == nullptr)
				{
					return nullptr;
				}

				AActor *cam = player->camera;
				double fov = cam->player ? cam->player->FOV : cam->CameraFOV;
				r_viewpoint.FieldOfView = DAngle::fromDeg(clamp(fov, 5., 170.));

				::viewwindowx = rx[slot];
				::viewwindowy = ry[slot];
				SetViewWindow(rw[slot], viewh[slot]);

				// The renderer works with OpenGL style coordinates (origin at the bottom left).
				ss_scenerect.left = rx[slot];
				ss_scenerect.top = H - (ry[slot] + viewh[slot]);
				ss_scenerect.width = rw[slot];
				ss_scenerect.height = viewh[slot];
				ss_renderslot = slot;
				return player;
			},
			[&](int slot, sector_t *sec)
			{
				viewsec[slot] = sec;
				rendered[slot] = sec != nullptr;
				ss_renderslot = -1;
				SS_SelectSlot(prevslot);
			});
		}, true);
	}
	saved.Restore();
	screen->SetViewportRects(nullptr);

	// Now the 2D part for each player.
	twod->Begin(screen->GetWidth(), screen->GetHeight());
	double ticfrac = r_viewpoint.TicFrac;
	for (int slot = 0; slot < n; slot++)
	{
		FSplitSlotScope scope(slot);
		F2DViewContext ctx(rx[slot], ry[slot], rw[slot], rh[slot], viewh[slot]);
		if (!rendered[slot])
		{
			Dim(twod, 0, 1.f, 0, 0, rw[slot], rh[slot]);
		}
		DrawPlayerHUD(slot, rw[slot], rh[slot], viewh[slot], viewsec[slot], ticfrac);
		if (!V_IsHardwareRenderer())
		{
			// The software renderer always draws a full screen image, so it cannot do split views.
			const char *text = "Split-screen needs the hardware renderer";
			int tw = NewSmallFont->StringWidth(text) * CleanXfac;
			DrawText(twod, NewSmallFont, CR_WHITE, (rw[slot] - tw) / 2, viewh[slot] / 2, text, DTA_CleanNoMove, true, TAG_DONE);
		}
	}

	// The unused quarter in a three player game.
	if (n == 3)
	{
		int x, y, w, h;
		SS_NumLocal = 4;	// get the fourth quarter's rectangle
		SS_GetViewRect(3, x, y, w, h);
		SS_NumLocal = 3;
		auto flat = GetBorderFlat();
		if (flat.isValid()) DrawBorder(twod, flat, x, y, x + w, y + h);
		else Dim(twod, 0, 1.f, x, y, w, h);
	}

	if (splitscreen_dividers)
	{
		const int W = screen->GetWidth();
		if (n == 2 && splitscreen_layout == 1)
		{
			Dim(twod, 0, 1.f, rx[0] + rw[0], 0, rx[1] - (rx[0] + rw[0]), H);
		}
		else if (n == 2)
		{
			Dim(twod, 0, 1.f, 0, ry[0] + rh[0], W, ry[1] - (ry[0] + rh[0]));
		}
		else
		{
			Dim(twod, 0, 1.f, rx[0] + rw[0], 0, rx[1] - (rx[0] + rw[0]), H);
			Dim(twod, 0, 1.f, 0, ry[0] + rh[0], W, ry[2] - (ry[0] + rh[0]));
		}
	}
}

//-----------------------------------------------------------------------------
//
// Console commands
//
//-----------------------------------------------------------------------------

// Used by the split-screen menu: opens the new game menus, and the game started from there
// will be a split-screen game.
CCMD(splitscreen_newgame)
{
	if (netgame)
	{
		Printf("Split-screen co-op is not available in network games.\n");
		return;
	}
	M_SetMenu(NAME_Playerclassmenu, -1);
	SS_RequestNewGame(splitscreen_players);
}

// splitscreen <players> [map] - starts a split-screen co-op game right away.
CCMD(splitscreen)
{
	if (argv.argc() < 2)
	{
		if (SS_Active()) Printf("Split-screen co-op is on with %d players.\n", SS_NumLocal);
		else Printf("Split-screen co-op is off.\n");
		Printf("Usage: splitscreen <2-%d> [map]\n", SS_MAXLOCALPLAYERS);
		return;
	}
	if (netgame)
	{
		Printf("Split-screen co-op is not available in network games.\n");
		return;
	}
	int numplayers = clamp(atoi(argv[1]), 1, (int)SS_MAXLOCALPLAYERS);
	FString mapname;
	if (argv.argc() > 2) mapname = argv[2];
	else if (gamestate == GS_LEVEL && primaryLevel != nullptr) mapname = primaryLevel->MapName;
	else if (AllEpisodes.Size() > 0) mapname = AllEpisodes[0].mEpisodeMap;
	else mapname = "MAP01";

	if (!P_CheckMapData(mapname.GetChars()))
	{
		Printf("No map %s\n", mapname.GetChars());
		return;
	}
	SS_RequestNewGame(numplayers);
	G_DeferedInitNew(mapname.GetChars());
	M_ClearMenus();
}

// Shows the system's controller setup screen for the configured number of players.
CCMD(splitscreen_pair)
{
#ifdef __SWITCH__
	int count = I_ShowControllerSetup(SS_MAXLOCALPLAYERS);
	if (count < 0)
	{
		Printf("Could not open the controller setup\n");
	}
	else
	{
		Printf("%d controllers connected\n", count);
	}
#else
	TArray<IJoystickConfig *> sticks;
	I_GetJoysticks(sticks);
	Printf("Game controllers are assigned in this order%s:\n",
		splitscreen_keyboard ? " (the keyboard and mouse control player 1)" : "");
	for (unsigned i = 0; i < sticks.Size(); i++)
	{
		Printf("  Player %u: %s\n", i + (splitscreen_keyboard ? 2 : 1), sticks[i]->GetName().GetChars());
	}
#endif
}
