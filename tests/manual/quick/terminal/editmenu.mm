// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#import <AppKit/AppKit.h>

// AppKit exposes the Character Viewer (ctrl+cmd+space) as the auto-added
// "Emoji & Symbols" item of an Edit menu; without one the hotkey has no
// in-app target. Creator's menu bar provides this, the bare test app must
// bring its own.
void installEditMenu()
{
    NSMenu *mainMenu = NSApp.mainMenu;
    if (!mainMenu) {
        mainMenu = [[NSMenu alloc] init];
        NSApp.mainMenu = mainMenu;
    }
    NSMenuItem *editItem = [[NSMenuItem alloc] initWithTitle:@"Edit"
                                                      action:nil
                                               keyEquivalent:@""];
    editItem.submenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [mainMenu addItem:editItem];
}
