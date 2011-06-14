/* bzflag
 * Copyright (c) 1993-2010 Tim Riker
 *
 * This package is free software;  you can redistribute it and/or
 * modify it under the terms of the license found in the file
 * named COPYING that should have accompanied this file.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __SERVERMENU_H__
#define __SERVERMENU_H__

#include "common.h"

/* local interface headers */
#include "HUDDialog.h"
#include "HUDuiControl.h"
#include "HUDuiDefaultKey.h"
#include "MenuDefaultKey.h"

#include "HUDuiServerInfo.h"
#include "HUDuiServerList.h"
#include "HUDuiServerListCache.h"
#include "HUDuiServerListCustomTab.h"
#include "HUDuiTabbedControl.h"
#include "game/ServerItem.h"
#include "game/ServerList.h"
#include "game/ServerPing.h"

class ServerMenu;

class ServerMenuDefaultKey : public MenuDefaultKey {
  public:
    ServerMenuDefaultKey(ServerMenu* _menu) :
      menu(_menu) { }
    ~ServerMenuDefaultKey() { }

    bool keyPress(const BzfKeyEvent&);
    bool keyRelease(const BzfKeyEvent&);

  private:
    ServerMenu* menu;
};

class ServerMenu : public HUDDialog {
    typedef std::map<std::string, std::pair<ServerPing*, std::vector<HUDuiServerList*> > > PingsMap;
  public:
    ServerMenu();
    ~ServerMenu();

    HUDuiDefaultKey* getDefaultKey() { return &defaultKey; }

    void    execute();
    void    resize(int width, int height);

    static void newServer(ServerItem* addedServer, void* data);

  public:       // need better encapsulation
    HUDuiServerList* normalList;
    HUDuiServerList* recentList;
    HUDuiServerList* favoritesList;

    HUDuiTabbedControl* tabbedControl;
    HUDuiServerInfo* serverInfo;
    HUDuiServerListCustomTab* customTabControl;

  private:
    HUDuiServerListCache& listsCache;
    ServerList& serverList;

    static void callback(HUDuiControl* w, void* data);

    void updateStatus();
    void markAsFavorite(ServerItem* item);

    static void playingCB(void*);

    static PingsMap activePings;

    ServerMenuDefaultKey  defaultKey;

    HUDuiLabel* title;
    HUDuiLabel* help;
};


#endif /* __SERVERMENU_H__ */

// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: nil ***
// End: ***
// ex: shiftwidth=2 tabstop=8 expandtab