#pragma once
#ifndef UI_LUA_H
#define UI_LUA_H

#include <stdbool.h>

struct lua_State;

// Immediate-mode UI toolkit exposed to Lua as a global `ui` table:
//
//   ui.text(x, y, str [, {size=, color={r,g,b,a}, font=}])
//   ui.rect(x, y, w, h [, {color={r,g,b,a}, radius=}])
//   ui.button(id, x, y, w, h, label [, {size=, color=, hover_color=, text_color=}])
//                                                              -> true when clicked
//
// All coordinates are absolute (anchored top-left of the window). The functions
// emit Clay elements, so they may only be called inside the Clay layout pass,
// i.e. from a script's on_ui() callback.

// Register the `ui` table into a Lua state.
void ui_lua_register(struct lua_State *L);

// Per-frame bookkeeping, called by the layout driver around on_ui():
void ui_lua_begin_frame(void);  // reset the per-frame string/id arena
void ui_lua_end_frame(void);    // clear a pending click

// Record that a mouse press happened this frame (consumed by ui.button).
void ui_lua_note_click(void);

#endif
