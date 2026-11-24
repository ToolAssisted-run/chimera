// The windows menu follows the menu bar shown (qt/mac_window_menu.hpp): made,
// as Qt makes it, hidden in the first menu bar, it is in each bar put in its
// place, and back in the first when that is shown again; by the move itself,
// and by followWindowsMenu as the event loop passes. With it left behind,
// AppKit's Move & Resize keys did nothing while a window with a menu bar of
// its own was in front.
#include "../mac_window_menu.hpp"

#import <AppKit/AppKit.h>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *what)
{
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) ++failures;
}

static NSMenu *bar(NSString *first)
{
	NSMenu *m = [[[NSMenu alloc] initWithTitle:@""] autorelease];
	NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:first action:nil keyEquivalent:@""] autorelease];
	item.submenu = [[[NSMenu alloc] initWithTitle:first] autorelease];
	[m addItem:item];
	return m;
}

// the hidden item holding the windows menu, as QCocoaMenuBar::insertWindowMenu makes it
static NSMenu *windowsMenuIn(NSMenu *mainMenu)
{
	NSMenuItem *holder = [[[NSMenuItem alloc] initWithTitle:@"QtWindowMenu" action:nil keyEquivalent:@""] autorelease];
	holder.hidden = YES;
	holder.submenu = [[[NSMenu alloc] initWithTitle:@"QtWindowMenu"] autorelease];
	NSMenuItem *dummy = [[[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""] autorelease];
	dummy.hidden = YES;
	[holder.submenu addItem:dummy];
	[mainMenu addItem:holder];
	NSApp.windowsMenu = holder.submenu;
	return holder.submenu;
}

static void passEventLoop()
{
	[NSNotificationCenter.defaultCenter postNotificationName:NSApplicationDidUpdateNotification object:NSApp];
	[NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
}

int main()
{
	@autoreleasepool {
		[NSApplication sharedApplication];
		NSMenu *mainBar = bar(@"File");
		NSMenu *toolBar = bar(@"Watches");
		NSApp.mainMenu = mainBar;
		NSMenu *windows = windowsMenuIn(mainBar);
		check(windows.supermenu == mainBar, "the windows menu is made in the first menu bar");

		NSApp.mainMenu = toolBar;
		chimera::qt::keepWindowsMenu();
		check(windows.supermenu == toolBar, "another menu bar shown: the windows menu is in it");
		check([mainBar indexOfItemWithSubmenu:windows] < 0, "and no longer in the first");
		check(NSApp.windowsMenu == windows, "and is still the application's windows menu");
		check(toolBar.itemArray.lastObject.hidden, "it stays hidden, at the end of the bar");

		NSApp.mainMenu = mainBar;
		chimera::qt::keepWindowsMenu();
		check(windows.supermenu == mainBar, "the first menu bar shown again: the windows menu is back in it");
		check(mainBar.itemArray.count == 2 && toolBar.itemArray.count == 1, "one of it, never two");

		chimera::qt::followWindowsMenu();
		NSApp.mainMenu = toolBar;
		passEventLoop();
		check(windows.supermenu == toolBar, "followed: a menu bar shown has it after the event loop passes");
		NSApp.mainMenu = mainBar;
		passEventLoop();
		check(windows.supermenu == mainBar, "followed back to the first");
	}
	std::printf("%d failed\n", failures);
	return failures == 0 ? 0 : 1;
}
