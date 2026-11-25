#pragma once

namespace chimera::qt {

/* macOS: the Window menu's Move & Resize, and its keys (Fn-Control-arrows,
 * Fn-Control-C, Fn-Control-F), for every window and not the main window
 * alone. AppKit puts those items, keys and all, in the application's
 * windows menu, which Qt makes once, hidden, in the menu bar it shows
 * first: the main window's. A window with a menu bar of its own (TAStudio,
 * the RAM tools, the Hex Editor, the Lua Console) has Qt show that bar in
 * its place while it is in front, and the hidden menu, and the keys with it,
 * stayed behind in the main window's. This keeps that menu in whichever menu
 * bar is shown. Called once, after the application is made; built on macOS
 * only. */
void followWindowsMenu();

/* the move itself, made once: the windows menu put in the menu bar shown
 * when it is in another (followWindowsMenu makes it whenever that changes) */
void keepWindowsMenu();

}
