/* bzflag
 * Copyright (c) 1993 - 2008 Tim Riker
 *
 * This package is free software;  you can redistribute it and/or
 * modify it under the terms of the license found in the file
 * named COPYING that should have accompanied this file.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* interface header */
#include "ServerList.h"

/* system headers */
#include <iostream>
#include <vector>
#include <string>
#include <string.h>
#if !defined(_WIN32)
#include <errno.h>
#endif
#include <ctype.h>

/* common implementation headers */
#include "version.h"
#include "bzsignal.h"
#include "Ping.h"
#include "Protocol.h"
#include "TimeKeeper.h"
#include "TextUtils.h"
#include "ErrorHandler.h"

/* local implementation headers */
#include "ServerListCache.h"
#include "StartupInfo.h"

// initialize the singleton
template <>
ServerList* Singleton<ServerList>::_instance = (ServerList*)0;


ServerList::ServerList() :
	phase(-1),
	serverCache(ServerListCache::get()),
	pingBcastSocket(-1)
{
}

ServerList::~ServerList() {
  _shutDown();
}

void ServerList::startServerPings(StartupInfo *info) {

  // schedule lookup of server list url.  dereference URL chain every
  // time instead of only first time just in case one of the pointers
  // has changed.
  if (phase > -1 && phase < 4)
    return;
  if (info->listServerURL.size() == 0)
    phase = -1;
  else
    phase = 0;

  // also try broadcast
  pingBcastSocket = openBroadcast(BroadcastPort, NULL, &pingBcastAddr);
  if (pingBcastSocket != -1)
    PingPacket::sendRequest(pingBcastSocket, &pingBcastAddr);
}

void ServerList::readServerList()
{
  char *base = (char *)theData;
  char *endS = base + theLen;
  const char *tokenIdentifier = "TOKEN: ";
  const char *noTokenIdentifier = "NOTOK: ";
  const char *errorIdentifier = "ERROR: ";
  const char *noticeIdentifier = "NOTICE: ";
  // walks entire reply including HTTP headers
  while (base < endS) {
    // find next newline
    char* scan = base;
    while (scan < endS && *scan != '\n')
      scan++;

    // if no newline then no more complete replies
    if (scan >= endS)
      break;
    *scan++ = '\0';

    // look for TOKEN: and save token if found also look for NOTOK:
    // and record "badtoken" into the token string and print an
    // error
    if (strncmp(base, tokenIdentifier, strlen(tokenIdentifier)) == 0) {
      strncpy(startupInfo->token, (char *)(base + strlen(tokenIdentifier)),
	      TokenLen);
#ifdef DEBUG
      printError("got token:");
      printError(startupInfo->token);
#endif
      base = scan;
      continue;
    } else if (!strncmp(base, noTokenIdentifier,
			strlen(noTokenIdentifier))) {
      printError("ERROR: did not get token:");
      printError(base);
      strcpy(startupInfo->token, "badtoken\0");
      base = scan;
      continue;
    } else if (!strncmp(base, errorIdentifier, strlen(errorIdentifier))) {
      printError(base);
      strcpy(startupInfo->token, "badtoken\0");
      base = scan;
      continue;
    } else if (!strncmp(base, noticeIdentifier, strlen(noticeIdentifier))) {
      printError(base);
      base = scan;
      continue;
    }
    // parse server info
    char *scan2, *name, *version, *infoServer, *address, *title;
    name = base;
    version = name;
    while (*version && !isspace(*version))  version++;
    while (*version &&  isspace(*version)) *version++ = 0;
    infoServer = version;
    while (*infoServer && !isspace(*infoServer))  infoServer++;
    while (*infoServer &&  isspace(*infoServer)) *infoServer++ = 0;
    address = infoServer;
    while (*address && !isspace(*address))  address++;
    while (*address &&  isspace(*address)) *address++ = 0;
    title = address;
    while (*title && !isspace(*title))  title++;
    while (*title &&  isspace(*title)) *title++ = 0;

    // extract port number from address
    unsigned int port = ServerPort;
    scan2 = strchr(name, ':');
    if (scan2) {
      port = atoi(scan2 + 1);
      *scan2 = 0;
    }

    // check info
    if (strcmp(version, getServerVersion()) == 0 &&
	(int)strlen(infoServer) == PingPacketHexPackedSize &&
	port >= 1 && port <= 65535) {
      // store info
      ServerItem serverInfo;
      serverInfo.ping.unpackHex(infoServer);
      int dot[4] = {127,0,0,1};
      if (sscanf(address, "%d.%d.%d.%d", dot+0, dot+1, dot+2, dot+3) == 4) {
	if (dot[0] >= 0 && dot[0] <= 255 &&
	    dot[1] >= 0 && dot[1] <= 255 &&
	    dot[2] >= 0 && dot[2] <= 255 &&
	    dot[3] >= 0 && dot[3] <= 255) {
	  InAddr addr;
	  unsigned char* paddr = (unsigned char*)&addr.s_addr;
	  paddr[0] = (unsigned char)dot[0];
	  paddr[1] = (unsigned char)dot[1];
	  paddr[2] = (unsigned char)dot[2];
	  paddr[3] = (unsigned char)dot[3];
	  serverInfo.ping.serverId.serverHost = addr;
	}
      }
      serverInfo.ping.serverId.port = htons((int16_t)port);
      serverInfo.name = name;
      serverInfo.port = port;

      // construct description
      serverInfo.description = serverInfo.name;
      if (port != ServerPort) {
	char portBuf[20];
	sprintf(portBuf, "%d", port);
	serverInfo.description += ":";
	serverInfo.description += portBuf;
      }
      if (strlen(title) > 0) {
	serverInfo.description += "; ";
	serverInfo.description += title;
      }

      serverInfo.cached = false;
      // add to list & add it to the server cache
      addToList(serverInfo, true);
    }

    // next reply
    base = scan;
  }

  // remove parsed replies
  theLen -= int(base - (char *)theData);
  memmove(theData, base, theLen);
}


void ServerList::sort()
{
  // make sure the list is sorted before we go inserting in order!
  //std::sort(servers.begin(), servers.end());
}


// FIXME: the list gets jacked.  maybe the stl::erase below are a
// bunch of other things, but needs to be tested/rewritten more.
void ServerList::addToList(ServerItem info, bool doCache)
{
  // update if we already have it
  int i;

  // search and delete entry for this item if it exists
  std::map<std::string, ServerItem>::iterator serverIterator;

  for (serverIterator = servers.begin(); serverIterator != servers.end(); serverIterator++) {
    const ServerItem& server = (*serverIterator).second;
    if ((server.ping.serverId.serverHost.s_addr == info.ping.serverId.serverHost.s_addr) && 
	(server.ping.serverId.port == info.ping.serverId.port)) {
      // retain age so it can stay sorted same agewise
      info.setAge(server.getAgeMinutes(), server.getAgeSeconds());
      servers.erase(serverIterator); // erase this item
      break;
    }
  }

  // sort before iterating through
  sort();

  // find point to insert new player at
  int insertPoint = -1; // point to insert server into

  i = 0;
  // insert new item before the first serveritem with is deemed to be less
  // in value than the item to be inserted -- cached items are less than
  // non-cached, items that have more players are more, etc..
  for (serverIterator = servers.begin(); serverIterator != servers.end(); serverIterator++) {
    const ServerItem& server = (*serverIterator).second;
    if (info < server){
      insertPoint = i;
      break;
    }
    i++;
  }

  // mark server in current list if it is a favorite server
  std::string serverAddress = info.getAddrName();
  if (serverCache->isFavorite(serverAddress))
    info.favorite = true;

  if (serverCache->isRecent(serverAddress))
    info.recent = true;

  // Get the server's server key
  const ServerItem& constInfo = info;
  std::string serverKey = constInfo.name;
  const unsigned int port = (int)ntohs((unsigned short)constInfo.port);
  if (port != ServerPort) {
    char portBuf[20];
    sprintf(portBuf, "%d", port);
    serverKey += ":";
    serverKey += portBuf;
  }

  if (insertPoint == -1){ // no spot to insert it into -- goes on back
    //servers.push_back(info);
    servers.insert(std::pair<std::string, ServerItem>(serverKey, constInfo));
  } else {  // found a spot to insert it into
    //servers.insert(servers.begin() + insertPoint, info);
    std::map<std::string, ServerItem>::iterator location = servers.begin();
    std::advance(location, insertPoint);
    servers.insert(location, std::pair<std::string, ServerItem>(serverKey, constInfo));
  }

  // check if we need to show cached values
  if (doCache) {
    info.cached = true; // values in cache are "cached"
    // update the last updated time to now
    info.resetAge();

    ServerListCache::SRV_STR_MAP::iterator iter;
    iter = serverCache->find(serverAddress);  // find entry to allow update
    if (iter != serverCache->end()){ // if we find it, update it
      iter->second = info;
    } else {
      // insert into cache -- wasn't found
      serverCache->insert(serverAddress, info);
    }
  }

  for (ServerCallbackList::iterator itr = serverCallbackList.begin();
       itr != serverCallbackList.end(); ++itr)
  {
    (*itr).first(&info, (*itr).second);
  }

  if (serverKeyCallbackList.find(info.getServerKey()) != serverKeyCallbackList.end())
  {
    for (size_t i=0; i<serverKeyCallbackList[info.getServerKey()].size(); i++)
    {
      serverKeyCallbackList[info.getServerKey()][i].first(&info, serverKeyCallbackList[info.getServerKey()][i].second);
    }
  }

  if (info.favorite)
  {
    for (ServerCallbackList::iterator itr = favoritesCallbackList.begin();
	 itr != favoritesCallbackList.end(); ++itr)
    {
      (*itr).first(&info, (*itr).second);
    }
  }

  if (info.recent)
  {
    for (ServerCallbackList::iterator itr = recentCallbackList.begin();
	 itr != recentCallbackList.end(); ++itr)
    {
      (*itr).first(&info, (*itr).second);
    }
  }
}

/*
// mark server identified by host:port string as favorite
void		    ServerList::markFav(const std::string &serverAddress, bool fav)
{
  std::map<std::string, ServerItem>::iterator serverIterator;

  //for (int i = 0; i < (int)servers.size(); i++) {
  for (serverIterator = servers.begin(); serverIterator != servers.end(); serverIterator++) {
    ServerItem& server = (*serverIterator).second;
    if (serverAddress == server.getAddrName()) {
      server.favorite = fav;
      break;
    }
  }
}
*/
void ServerList::markAsRecent(ServerItem* item)
{
  std::string addrname = item->getAddrName();
  ServerListCache::SRV_STR_MAP::iterator i = serverCache->find(addrname);
  if (i!= serverCache->end()) {
    i->second.recent = true;
    i->second.recentTime = item->getNow();
  }

  item->recent = true;
  item->recentTime = item->getNow();

  for (ServerCallbackList::iterator itr = recentCallbackList.begin();
       itr != recentCallbackList.end(); ++itr)
  {
    (*itr).first(item, (*itr).second);
  }
}

void ServerList::unmarkAsRecent(ServerItem* item)
{
  std::string addrname = item->getAddrName();
  ServerListCache::SRV_STR_MAP::iterator i = serverCache->find(addrname);
  if (i!= serverCache->end()) {
    i->second.recent = false;
  }

  item->recent = false;
  item->recentTime = 0;
}

void ServerList::markAsFavorite(ServerItem* item)
{
  std::string addrname = item->getAddrName();
  ServerListCache::SRV_STR_MAP::iterator i = serverCache->find(addrname);
  if (i!= serverCache->end()) {
    i->second.favorite = true;
  }

  item->favorite = true;

  for (ServerCallbackList::iterator itr = favoritesCallbackList.begin();
       itr != favoritesCallbackList.end(); ++itr)
  {
    (*itr).first(item, (*itr).second);
  }
}

void ServerList::unmarkAsFavorite(ServerItem* item)
{
  std::string addrname = item->getAddrName();
  ServerListCache::SRV_STR_MAP::iterator i = serverCache->find(addrname);
  if (i!= serverCache->end()) {
    i->second.favorite = false;
  }

  item->favorite = false;
}

ServerItem* ServerList::lookupServer(std::string key)
{
  if (servers.find(key) != servers.end())
    return &(servers[key]);
  else
    return NULL;
}

ServerItem* ServerList::getServerAt(size_t index)
{
  std::map<std::string, ServerItem>::iterator serverIterator;
  
  serverIterator = servers.begin();
  std::advance(serverIterator, index);

  return &((*serverIterator).second);
}

void			ServerList::checkEchos(StartupInfo *info)
{
  startupInfo = info;

  // *** NOTE *** searching spinner update was here

  // lookup server list in phase 0
  if (phase == 0) {

    std::string url = info->listServerURL;

    std::string msg = "action=LIST&version=";
    msg	    += getServerVersion();
    msg	    += "&clientversion=";
    msg	    += TextUtils::url_encode(std::string(getAppVersion()));
    msg	    += "&callsign=";
    msg	    += TextUtils::url_encode(info->callsign);
    msg	    += "&password=";
    msg	    += TextUtils::url_encode(info->password);
    setPostMode(msg);
    setURL(url);
    addHandle();

    // do phase 1 only if we found a valid list server url
    phase = 1;
  }

  // get echo messages
  while (true) {
    // *** NOTE *** searching spinner update was here

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 250;

    fd_set read_set, write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    if (pingBcastSocket != -1) {
      FD_SET((unsigned int)pingBcastSocket, &read_set);
    }
    int fdMax = pingBcastSocket;

    const int nfound = select(fdMax+1, (fd_set*)&read_set,
					(fd_set*)&write_set, 0, &timeout);
    if (nfound <= 0)
      break;

    // check broadcast sockets
    ServerItem serverInfo;
    sockaddr_in addr;

    if (pingBcastSocket != -1 && FD_ISSET(pingBcastSocket, &read_set)) {
      if (serverInfo.ping.read(pingBcastSocket, &addr)) {
	serverInfo.ping.serverId.serverHost = addr.sin_addr;
	serverInfo.cached = false;
	addToListWithLookup(serverInfo);
      }
    }
  } // end loop waiting for input/output on any list server
}

void			ServerList::addToListWithLookup(ServerItem& info)
{
  info.name = Address::getHostByAddress(info.ping.serverId.serverHost);

  // tack on port number to description if not default
  info.description = info.name;
  const unsigned int port = (int)ntohs((unsigned short)info.ping.serverId.port);
  if (port != ServerPort) {
    char portBuf[20];
    sprintf(portBuf, "%d", port);
    info.description += ":";
    info.description += portBuf;
  }

  addToList(info); // do not cache network lan - etc. servers
}

// add the entire cache to the server list
void			ServerList::addCacheToList()
{
  if (addedCacheToList)
    return;
  addedCacheToList = true;
  for (ServerListCache::SRV_STR_MAP::iterator iter = serverCache->begin();
       iter != serverCache->end(); iter++){
    addToList(iter->second);
  }
}

void ServerList::collectData(char *ptr, int len)
{
  phase = 2;

  cURLManager::collectData(ptr, len);

  readServerList();
}

void ServerList::finalization(char *, unsigned int, bool good)
{
  if (!good) {
    printError("Can't talk with list server");
    addCacheToList();
    phase = -1;
  } else {
    phase = 4;
  }
}

const std::map<std::string, ServerItem>& ServerList::getServers() {
  return servers;
}

std::map<std::string, ServerItem>::size_type ServerList::size() {
  return servers.size();
}

void ServerList::clear() {
  servers.clear();
}

int ServerList::updateFromCache() {
  // clear server list
  clear();

  int numItemsAdded = 0;

  for (ServerListCache::SRV_STR_MAP::const_iterator iter = serverCache->begin();
       iter != serverCache->end(); iter++) {
    // if maxCacheAge is 0 we add nothing
    // if the item is young enough we add it
    if (serverCache->getMaxCacheAge() != 0
	&& iter->second.getAgeMinutes() < serverCache->getMaxCacheAge()) {
      addToList(iter->second);
      numItemsAdded ++;
    }
  }

  return numItemsAdded;
}

bool ServerList::searchActive() const {
  return (phase < 4) ? true : false;
}

bool ServerList::serverFound() const {
  return (phase >= 2) ? true : false;
}

void ServerList::addServerCallback(ServerListCallback _cb, void* _data)
{
  serverCallbackList.push_back(std::make_pair<ServerListCallback, void*>(_cb, _data));
}

void ServerList::removeServerCallback(ServerListCallback _cb, void* data)
{
  for (ServerCallbackList::iterator itr = serverCallbackList.begin();
       itr != serverCallbackList.end(); ++itr) {
    if (itr->first == _cb && itr->second == data) {
      serverCallbackList.remove(*itr);
      return;
    }
  }
}

void ServerList::addServerKeyCallback(std::string key, ServerListCallback _cb, void* _data)
{
  serverKeyCallbackList[key].push_back(std::make_pair<ServerListCallback, void*>(_cb, _data));
}

void ServerList::removeServerKeyCallback(std::string key, ServerListCallback _cb, void* data)
{
  std::vector<std::pair<ServerListCallback, void*> >::iterator it = std::find(serverKeyCallbackList[key].begin(), serverKeyCallbackList[key].end(), std::make_pair<ServerListCallback, void*>(_cb, data));
  if (it != serverKeyCallbackList[key].end())
    serverKeyCallbackList[key].erase(it);
}

void ServerList::addFavoriteServerCallback(ServerListCallback _cb, void* _data)
{
  favoritesCallbackList.push_back(std::make_pair<ServerListCallback, void*>(_cb, _data));
}

void ServerList::removeFavoriteServerCallback(ServerListCallback _cb, void* data)
{
  for (ServerCallbackList::iterator itr = favoritesCallbackList.begin();
       itr != favoritesCallbackList.end(); ++itr) {
    if (itr->first == _cb && itr->second == data) {
      favoritesCallbackList.remove(*itr);
      return;
    }
  }
}

void ServerList::addRecentServerCallback(ServerListCallback _cb, void* _data)
{
  recentCallbackList.push_back(std::make_pair<ServerListCallback, void*>(_cb, _data));
}

void ServerList::removeRecentServerCallback(ServerListCallback _cb, void* data)
{
  for (ServerCallbackList::iterator itr = recentCallbackList.begin();
       itr != recentCallbackList.end(); ++itr) {
    if (itr->first == _cb && itr->second == data) {
      recentCallbackList.remove(*itr);
      return;
    }
  }
}

void ServerList::_shutDown() {
  // close broadcast socket
  closeBroadcast(pingBcastSocket);
  pingBcastSocket = -1;
}

// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8
