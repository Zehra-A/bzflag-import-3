/* bzflag
* Copyright (c) 1993 - 2005 Tim Riker
*
* This package is free software;  you can redistribute it and/or
* modify it under the terms of the license found in the file
* named COPYING that should have accompanied this file.
*
* THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
* WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
*/

// implementation wrapers for all the bza_ API functions
#include "bzfsAPI.h"

#include "common.h"
#include "bzfs.h"
#include "WorldWeapons.h"
#include "WorldEventManager.h"
#include "GameKeeper.h"
#include "FlagInfo.h"

#include "commands.h"
#include "SpawnPosition.h"
#include "WorldInfo.h"

#include "BzMaterial.h"

TimeKeeper sync = TimeKeeper::getCurrent();

extern void sendMessage(int playerIndex, PlayerId dstPlayer, const char *message);
extern void removePlayer(int playerIndex, const char *reason, bool notify);
extern void zapFlagByPlayer(int playerIndex);

extern CmdLineOptions *clOptions;
extern uint16_t curMaxPlayers;
extern WorldInfo *world;
extern float pluginWorldSize;
extern float pluginWorldHeight;


// utility
void setBZMatFromAPIMat (BzMaterial &bzmat, bz_MaterialInfo* material )
{
	if (!material)
		return;

	bzmat.setName(material->name);
	bzmat.setAmbient(material->ambient);
	bzmat.setDiffuse(material->diffuse);
	bzmat.setSpecular(material->specular);
	bzmat.setEmission(material->emisive);
	bzmat.setShininess(material->shine);

	bzmat.setNoCulling(!material->culling);
	bzmat.setNoSorting(!material->sorting);
	bzmat.setAlphaThreshold(material->alphaThresh);

	for( unsigned int i = 0; i < material->textures.size();i++ )
	{
		bzmat.addTexture(material->textures[i].texture);
		bzmat.setCombineMode(material->textures[i].combineMode);
		bzmat.setUseTextureAlpha(material->textures[i].useAlpha);
		bzmat.setUseColorOnTexture(material->textures[i].useColorOnTexture);
		bzmat.setUseSphereMap(material->textures[i].useSphereMap);
	}
}


// versioning
BZF_API int bz_APIVersion ( void )
{
	return BZ_API_VERSION;
}

BZF_API bool bz_registerEvent ( bz_teEventType eventType, int team, bz_EventHandler* eventHandler )
{
	if (!eventHandler)
		return false;
	
	worldEventManager.addEvent((teEventType)eventType,team,(BaseEventHandler*)eventHandler);
	return true;
}

BZF_API bool bz_removeEvent ( bz_teEventType eventType, int team, bz_EventHandler* eventHandler )
{
	if (!eventHandler)
		return false;

	worldEventManager.removeEvent((teEventType)eventType,team,(BaseEventHandler*)eventHandler);
	return true;
}

BZF_API bool bz_updatePlayerData ( bz_PlayerRecord *playerRecord )
{
	if (!playerRecord)
		return false;

	GameKeeper::Player *player = GameKeeper::Player::getPlayerByIndex(playerRecord->playerID);
	if (!player)
		return false;

	memcpy(playerRecord->pos,player->lastState->pos,sizeof(float)*3);

	playerRecord->rot = player->lastState->azimuth;

	int flagid = player->player.getFlag();
	FlagInfo *flagInfo = FlagInfo::get(flagid);

	playerRecord->currentFlag = flagInfo->flag.type->label();

	std::vector<FlagType*>	flagHistoryList = player->flagHistory.get();

	playerRecord->flagHistory.clear();
	for ( unsigned int i = 0; i < flagHistoryList.size(); i ++)
		playerRecord->flagHistory.push_back(flagHistoryList[i]->label());

	playerRecord->groups.clear();
	playerRecord->groups = player->accessInfo.groups;

	playerRecord->admin = player->accessInfo.isVerified();

	playerRecord->wins = player->score.getWins();
	playerRecord->losses = player->score.getLosses();

	return true;
}

BZF_API bool bz_getPlayerIndexList ( std::vector<int> *playerList )
{
	playerList->clear();

	for (int i = 0; i < curMaxPlayers; i++)
	{
		GameKeeper::Player *p = GameKeeper::Player::getPlayerByIndex(i);
		if ((p == NULL))
			continue;

		playerList->push_back(i);
	}
	return playerList->size() > 0;
}

BZF_API bool bz_getPlayerByIndex ( int index, bz_PlayerRecord *playerRecord )
{
	GameKeeper::Player *player = GameKeeper::Player::getPlayerByIndex(index);

	if (!player || !playerRecord)
		return false;

	playerRecord->callsign = player->player.getCallSign();
	playerRecord->playerID = index;
	playerRecord->team = player->player.getTeam();

	playerRecord->spawned = player->player.isAlive();
	playerRecord->verified = player->accessInfo.isVerified();
	playerRecord->globalUser = player->authentication.isGlobal();

	playerRecord->ipAddress = player->netHandler->getTargetIP();
	playerRecord->update();
	return true;
}

BZF_API bool bz_sendTextMessage(int from, int to, const char* message)
{
	if (!message)
		return false;

	int playerIndex;
	PlayerId dstPlayer;

	if (to == BZ_ALL_USERS)
		dstPlayer = AllPlayers;
	else
		dstPlayer = (PlayerId)to;

	if (from == BZ_SERVER)
		playerIndex = ServerPlayer;
	else
		playerIndex = from;

	sendMessage(playerIndex, dstPlayer, message);
	return true;
}

BZF_API bool bz_fireWorldWep ( std::string flagType, float lifetime, int fromPlayer, float *pos, float tilt, float direction, int shotID, float dt )
{
	if (!pos || !flagType.size())
		return false;

	FlagTypeMap &flagMap = FlagType::getFlagMap();
	if (flagMap.find(flagType) == flagMap.end())
		return false;

	FlagType *flag = flagMap.find(flagType)->second;

	PlayerId player;
	if ( fromPlayer == BZ_SERVER )
		player = ServerPlayer;
	else
		player = fromPlayer;

	return fireWorldWep(flag,lifetime,player,pos,tilt,direction,shotID,dt) == shotID;
}

// time API
BZF_API double bz_getCurrentTime ( void )
{
	return TimeKeeper::getCurrent().getSeconds();
}

// info
BZF_API double bz_getBZDBDouble ( const char* variable )
{
	if (!variable)
		return 0.0;

	return BZDB.eval(std::string(variable));
}

BZF_API std::string bz_getBZDString( const char* variable )
{
	if (!variable)
		return std::string("");

	return BZDB.get(std::string(variable));
}

BZF_API bool bz_getBZDBool( const char* variable )
{
	if (!variable)
		return false;

	return BZDB.eval(std::string(variable)) > 0.0;
}

BZF_API int bz_getBZDInt( const char* variable )
{
	return (int)BZDB.eval(std::string(variable));
}

// loging
BZF_API void bz_debugMessage ( int _debugLevel, const char* message )
{
	if (!message)
		return;

	if (debugLevel >= _debugLevel)
		formatDebug("%s\n",message);
}

// admin
BZF_API bool bz_kickUser ( int playerIndex, const char* reason, bool notify )
{
	GameKeeper::Player *player = GameKeeper::Player::getPlayerByIndex(playerIndex);
	if (!player || !reason)
		return false;

	removePlayer(playerIndex,reason,notify);
	return true;
}

BZF_API bool bz_IPBanUser ( int playerIndex, const char* ip, int time, const char* reason )
{
	GameKeeper::Player *player = GameKeeper::Player::getPlayerByIndex(playerIndex);
	if (!player || !reason || !ip)
		return false;

	// reload the banlist in case anyone else has added
	clOptions->acl.load();

	if (clOptions->acl.ban(ip, player->player.getCallSign(), time,reason))
		clOptions->acl.save();
	else
		return false;

	return true;
}

BZF_API bool bz_registerCustomSlashCommand ( const char* command, bz_CustomSlashCommandHandler *handler )
{
	if (!command || !handler)
		return false;

	registerCustomSlashCommand(std::string(command),(CustomSlashCommandHandler*)handler);
	return true;
}

BZF_API bool bz_removeCustomSlashCommand ( const char* command )
{
	if (!command)
		return false;

	removeCustomSlashCommand(std::string(command));
	return true;
}

BZF_API bool bz_getStandardSpawn ( int playeID, float pos[3], float *rot )
{
	GameKeeper::Player *player = GameKeeper::Player::getPlayerByIndex(playeID);
	if (!player)
		return false;

	// get the spawn position
	SpawnPosition* spawnPosition = new SpawnPosition(playeID,
		(!clOptions->respawnOnBuildings) || (player->player.isBot()),
		clOptions->gameStyle & TeamFlagGameStyle);

	pos[0] = spawnPosition->getX();
	pos[1] = spawnPosition->getY();
	pos[2] = spawnPosition->getZ();
	if (rot)
		*rot = spawnPosition->getAzimuth();

	return true;
}

BZF_API bool bz_killPlayer ( int playeID, bool spawnOnBase )
{
	GameKeeper::Player *player = GameKeeper::Player::getPlayerByIndex(playeID);
	if (!player)
		return false;

	if (!player->player.isAlive())
		return false;

	player->player.setDead();
	player->player.setRestartOnBase(spawnOnBase);
	zapFlagByPlayer(playeID);

	return true;
}

BZF_API bool bz_removePlayerFlag ( int playeID )
{
	GameKeeper::Player *player = GameKeeper::Player::getPlayerByIndex(playeID);
	if (!player)
		return false;

	if (!player->player.isAlive())
		return false;

	zapFlagByPlayer(playeID);

	return true;
}

BZF_API bool bz_addWorldBox ( float *pos, float rot, float* scale, bz_WorldObjectOptions options )
{
	if (!world || world->isFinisihed() || !pos || !scale)
		return false;

	world->addBox(pos[0],pos[1],pos[2],rot,scale[0],scale[1],scale[2],options.driveThru,options.shootThru);
	return true;
}

BZF_API bool bz_addWorldPyramid ( float *pos, float rot, float* scale, bool fliped, bz_WorldObjectOptions options )
{
	if (!world || world->isFinisihed() || !pos || !scale)
		return false;

	world->addPyramid(pos[0],pos[1],pos[2],rot,scale[0],scale[1],scale[2],options.driveThru,options.shootThru,fliped);
	return true;
}

BZF_API bool bz_addWorldBase( float *pos, float rot, float* scale, int team, bz_WorldObjectOptions options )
{
	if (!world || world->isFinisihed() || !pos || !scale)
		return false;

	world->addBase(pos,rot,scale,team,options.driveThru,options.shootThru);
	return true;
}

BZF_API bool bz_addWorldTeleporter ( float *pos, float rot, float* scale, float border, bz_WorldObjectOptions options )
{
	if (!world || world->isFinisihed() || !pos || !scale)
		return false;

	world->addTeleporter(pos[0],pos[1],pos[2],rot,scale[0],scale[1],scale[2],border,false,options.driveThru,options.shootThru);
	return true;
}

BZF_API bool bz_addWorldLink( int from, int to )
{
	if (!world || world->isFinisihed() )
		return false;

	world->addLink(from,to);
	return true;
}

BZF_API bool bz_addWorldWaterLevel( float level, bz_MaterialInfo *material )
{
	if (!world || world->isFinisihed() )
		return false;

	if (!material)
	{
		world->addWaterLevel(level,NULL);
		return true;
	}

	BzMaterial	bzmat;
	setBZMatFromAPIMat(bzmat,material);
	world->addWaterLevel(level,MATERIALMGR.addMaterial(&bzmat));
	return true;
}

BZF_API bool bz_addWorldWeapon( std::string flagType, float *pos, float rot, float tilt, float initDelay, std::vector<float> delays )
{
	if (!world || world->isFinisihed() )
		return false;

	FlagTypeMap &flagMap = FlagType::getFlagMap();
	if (flagMap.find(flagType) == flagMap.end())
		return false;

	FlagType *flag = flagMap.find(flagType)->second;

	world->addWeapon(flag, pos, rot, tilt, initDelay, delays, sync);
	return true;
}


BZF_API bool bz_setWorldSize( float size, float wallHeight )
{
	pluginWorldHeight = wallHeight;
	pluginWorldSize = size;

	return true;
}



// Local Variables: ***
// mode:C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8
