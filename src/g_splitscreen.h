#pragma once
//-----------------------------------------------------------------------------
//
// Local split-screen co-op (up to 4 players on one machine)
//
// One machine controls several players without going through the network
// layer. Player 1 is always consoleplayer. Each extra local player gets its
// own input state (buttons, view angles, pending inventory use etc.), its own
// stream of special net commands, its own status bar and its own viewport.
//
// Most of the engine assumes that "the local player" is consoleplayer. Instead
// of rewriting every one of those places, the code temporarily switches
// consoleplayer (plus the related input globals) to the player being
// processed: while handling that player's controller input, while building its
// ticcmd and while rendering its view and HUD. FSplitSlotScope does this.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "vectors.h"

struct event_t;
struct ticcmd_t;
struct sector_t;
class DBaseStatusBar;
class FSerializer;
struct IntRect;

enum
{
	SS_MAXLOCALPLAYERS = 4,
};

// Number of local players in the current game. 0 when split-screen is off.
extern int SS_NumLocal;
// Player numbers of the local players. SS_LocalPlayers[0] == consoleplayer.
extern int SS_LocalPlayers[SS_MAXLOCALPLAYERS];
// The local slot whose context is currently selected (0 most of the time).
extern int SS_ActiveSlot;

inline bool SS_Active() { return SS_NumLocal > 1; }

// Returns the local slot controlling the given player, or -1.
int SS_SlotForPlayer(int playernum);
inline bool SS_IsLocalPlayer(int playernum) { return SS_Active() && SS_SlotForPlayer(playernum) >= 0; }

// Selects the input/view context of a local slot. Returns the previous slot.
int SS_SelectSlot(int slot);

struct FSplitSlotScope
{
	int prev;
	explicit FSplitSlotScope(int slot) { prev = SS_SelectSlot(slot); }
	~FSplitSlotScope() { SS_SelectSlot(prev); }
};

// Game setup
void SS_RequestNewGame(int numplayers);	// next new game becomes a split-screen game
int  SS_ConsumePendingNewGame();		// called by G_DoNewGame
void SS_SetupGame(int numplayers);		// called after G_NewInit
void SS_Shutdown();						// return to normal single-view mode
void SS_SerializeGlobals(FSerializer &arc);
void SS_MenuClosed();
void SS_InjectMenuEntries();			// add the split-screen entry to mod-replaced main/options menus

// Input
int  SS_SlotForEventSource(int source);	// -1: not assigned to any local player
bool SS_Responder(event_t *ev);			// returns true if the event was routed (handled)
void SS_BuildTiccmds();					// called from NetUpdate right after G_BuildTiccmd
void SS_CommitTic(int tic);				// called from NetUpdate after Net_NewMakeTic
void SS_ResetInputStates();				// clear all stored button states
bool SS_ToggleAutomap();				// returns true if handled per-player

// Status bars
void SS_CreateStatusBars(bool bTitleLevel);
void SS_DestroyStatusBars();
void SS_TickStatusBars();
void SS_AttachStatusBars();
void SS_MarkStatusBars();

// Rendering
bool SS_GetSceneRect(IntRect &rect);	// true when a split view is being rendered
bool SS_InSplitView();
bool SS_GetViewRect(int slot, int &x, int &y, int &w, int &h);
void SS_Display();
