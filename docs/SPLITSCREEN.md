# Split-screen co-op (2–4 players)

This build adds local split-screen co-op. Up to four players share one Switch,
each with their own view, weapon, status bar or HUD, automap and player colour.

## Starting a game

* **Main menu → Split-Screen Co-op**
  * **Players**: 2, 3 or 4
  * **Two player layout**: top and bottom, or side by side. Three and four
    players always use four quarters; with three players the fourth quarter
    is left empty.
  * **Divider lines** between the views
  * **HUD size in split views**: *Auto* picks the largest size that fits.
  * **Set up controllers** opens the Switch's controller screen with four
    player slots. It only opens when you pick it; connecting or detaching a
    controller doesn't pop it up.
  * **Start new game**, then pick the episode and skill as usual.
* The same menu is also under **Options → Split-Screen Co-op**.
* Mods that replace the main menu (Project Brutality, for example) get the
  **Split-Screen Co-op** entry added below their **New Game** automatically.
* A normal **New Game** from the main menu is still single player.
* Console: `splitscreen <2-4> [map]` starts a split-screen game right away.
  `splitscreen 1` goes back to single player.
* Command line: `-splitscreen <2-4>` (works together with `+map` / `-warp` / `-skill`).

## Controllers

* Controller 1 is player 1, controller 2 is player 2, and so on, following the
  player numbers the Switch shows on the controllers.
* A Pro Controller, a Joy-Con pair, or **a single Joy-Con held sideways** can
  each be one player. To use single Joy-Cons, pick **Set up controllers** and
  have each player hold their Joy-Con sideways and press SL + SR.
* A single Joy-Con held sideways (left or right one, same layout):

  | Button (held sideways) | Name in Customize Controls | Default |
  |---|---|---|
  | Stick up/down | | Walk forward/back |
  | Stick left/right | | Turn |
  | SR | JC_SR | Fire |
  | SL | JC_SL | Alt fire |
  | Right face button | JC_Right | Use / open |
  | Bottom face button | JC_Down | Next weapon |
  | Left face button | JC_Left | Previous weapon |
  | Top face button | JC_Up | Automap |
  | Press the stick (hold) | JC_Stick | Strafe: the stick sidesteps instead of turning |
  | + or − | JC_Menu | Menu |

  In menus the stick moves, the right button selects and the bottom button
  goes back. These buttons have their own bindings, separate from a Pro
  Controller's, so either can be changed without affecting the other.
* Every player uses the same bindings (Options → Customize Controls).
* Handheld mode can't be combined with other controllers. Detach the Joy-Cons
  or play docked/tabletop, and use **Set up controllers** if the numbering is
  wrong.
* Any controller can drive the menus. The menu pauses the game for everybody.

## How it plays

* Co-op rules apply: dead players press *Use* to respawn at their start.
* Everyone has their own automap (the *togglemap* button, D-pad up by default).
  It shows in that player's view only.
* At the end of a level, every player has to press a button to continue,
  like in a network co-op game.
* Saving and loading work. A split-screen save brings back the same number of
  players when it's loaded.
* Sound is heard from player 1's position.
* Split-screen needs the hardware renderer (the default). With the software
  renderer selected, each view shows a notice instead of the game.

## Console variables

| Variable | Default | Meaning |
|---|---|---|
| `splitscreen_players` | 2 | Players for *Start new game* in the split-screen menu |
| `splitscreen_layout` | 0 | Two players: 0 = top/bottom, 1 = side by side |
| `splitscreen_dividers` | true | Black lines between the views |
| `splitscreen_hudscale` | 0 | HUD scale in the views, 0 = automatic |
| `splitscreen_keyboard` | false | PC builds: keyboard/mouse is player 1 and controller 1 is player 2 |

## For developers

The feature lives mainly in `src/g_splitscreen.cpp` / `.h`. Short overview:

* **Players**: the extra players are added to the game like in a local co-op
  game (`multiplayer` on, no network). Each one gets a copy of player 1's
  settings with its own name and colour.
* **Input**: each queued input event remembers which controller it came from.
  Controller input for players 2–4 is handled in that player's own input
  context: button states, view angles, pending inventory use and its own
  stream of special network commands. `consoleplayer` is switched to that
  player while this happens, so existing console commands such as
  `weapnext` or `invuse` just work. Each player's ticcmd is put straight
  into `netcmds`, without going through the network layer.
* **Switch controllers** are read straight from libnx (`NXInputJoystick` in
  `i_joystick.cpp`), not through SDL's joystick driver, which opens the
  system controller applet with 8 slots whenever any controller changes.
  Sideways single Joy-Cons report their buttons as `JC_*` keys (the `Joy1`–`Joy8`
  key codes, renamed on Switch) with defaults in `engine/joyconbinds.txt`.
* **Rendering**: one frame renders every player's viewpoint into its own part
  of the screen (`RenderSplitViews` in `hw_entrypoint.cpp`). Then each
  player's HUD is drawn with the 2D drawer, offset and resized to that part
  of the screen. Each player has a separate status bar object, so mug shot,
  inventory bar and messages don't mix.
