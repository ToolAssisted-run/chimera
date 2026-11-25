// The windows menu kept in the menu bar shown (mac_window_menu.hpp).
#include "mac_window_menu.hpp"

#import <AppKit/AppKit.h>

namespace chimera::qt {

/* the hidden item holding the windows menu, moved to the end of the menu
 * bar shown when it is in another one (or in none) */
void keepWindowsMenu()
{
	NSMenu *windows = NSApp.windowsMenu;
	NSMenu *shown = NSApp.mainMenu;
	if (windows == nil || shown == nil) return;
	NSMenu *holderMenu = windows.supermenu;
	if (holderMenu == shown) return;
	NSMenuItem *holder = nil;
	if (holderMenu != nil)
	{
		const NSInteger at = [holderMenu indexOfItemWithSubmenu:windows];
		if (at >= 0) holder = [holderMenu itemAtIndex:at];
	}
	if (holder == nil) return; // Qt makes it in an item of a menu bar; anything else is not ours to move
	[holder retain];
	[holderMenu removeItem:holder];
	[shown addItem:holder];
	[holder release];
}

void followWindowsMenu()
{
	/* Qt shows a window's menu bar as the window becomes the key window;
	 * looked at after it has, and after every pass of the event loop, which
	 * covers a menu bar Qt changes at any other time */
	NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
	void (^later)(NSNotification *) = ^(NSNotification *) {
		dispatch_async(dispatch_get_main_queue(), ^{ keepWindowsMenu(); });
	};
	[center addObserverForName:NSWindowDidBecomeKeyNotification object:nil queue:nil usingBlock:later];
	[center addObserverForName:NSWindowDidBecomeMainNotification object:nil queue:nil usingBlock:later];
	[center addObserverForName:NSApplicationDidUpdateNotification object:nil queue:nil
		usingBlock:^(NSNotification *) { keepWindowsMenu(); }];
}

}
