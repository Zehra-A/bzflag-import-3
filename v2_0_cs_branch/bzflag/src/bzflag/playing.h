/* bzflag
 * Copyright (c) 1993 - 2006 Tim Riker
 *
 * This package is free software;  you can redistribute it and/or
 * modify it under the terms of the license found in the file
 * named COPYING that should have accompanied this file.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * main game loop stuff
 */

#ifndef	BZF_PLAYING_H
#define	BZF_PLAYING_H

#include "common.h"

// system includes
#include <string>
#include <vector>
#include <crystalspace.h>

/* common headers */
#include "StartupInfo.h"
#include "AutoCompleter.h"

/* local headers */
#include "MainWindow.h"
#include "ControlPanel.h"
#include "HUDRenderer.h"
#include "RadarRenderer.h"
#include "BackgroundRenderer.h"

#define MAX_MESSAGE_HISTORY (20)

typedef void		(*JoinGameCallback)(bool success, void* data);
typedef void		(*ConnectStatusCallback)(std::string& str);
typedef void		(*PlayingCallback)(void*);
struct PlayingCallbackItem {
  public:
    PlayingCallback	cb;
    void*		data;
};

class BzfDisplay;
class MainWindow;
class SceneRenderer;
class Player;
class ServerLink;

BzfDisplay*		getDisplay();
MainWindow*		getMainWindow();
SceneRenderer*		getSceneRenderer();
void			setSceneDatabase();
StartupInfo*		getStartupInfo();
void			notifyBzfKeyMapChanged();
bool			setVideoFormat(int, bool test = false);
Player*			lookupPlayer(PlayerId id);

bool			addExplosion(const float* pos,
				float size, float duration);
void			addTankExplosion(const float* pos);
void			addShotExplosion(const float* pos);
void			addShotPuff(const float* pos, float azimuth, float elevation);
void			warnAboutMainFlags();
void			warnAboutRadarFlags();
void		    warnAboutRadar();
void		    warnAboutConsole();
void			addPlayingCallback(PlayingCallback, void* data);
void			removePlayingCallback(PlayingCallback, void* data);

void			joinGame(JoinGameCallback, void* userData);
void		    leaveGame();
std::vector<std::string>& getSilenceList();
void			updateEvents();
void			addMessage(const Player* player,
				   const std::string& msg,
				   int mode = 3,
				   bool highlight = false,
				   const char* oldColor = NULL);

int			curlProgressFunc(void* clientp,
					 double dltotal, double dlnow,
					 double ultotal, double ulnow);

void selectNextRecipient (bool forward, bool robotIn);
void handleFlagDropped(Player* tank);
void setTarget();
bool shouldGrabMouse();
void setRoamingLabel();

extern void joinGame();

extern HUDRenderer	*hud;
extern char		messageMessage[PlayerIdPLen + MessageLen];
extern ServerLink*	serverLink;
extern int		numFlags;
extern StartupInfo	startupInfo;
extern DefaultCompleter completer;
extern bool	     gameOver;
extern ControlPanel    *controlPanel;
extern bool	     fireButton;
extern float	    destructCountdown;
extern bool	     pausedByUnmap;
extern int	      savedVolume;
extern MainWindow      *mainWindow;
extern float	    pauseCountdown;
extern float	    clockAdjust;
extern float	    roamDZoom;
extern bool	     roamButton;

class Playing {
public:
  Playing(BzfDisplay      *_display,
	  SceneRenderer   &renderer);
  ~Playing();

  void playingLoop();
  void doKey(const BzfKeyEvent &key, bool pressed);
  void drawFrame();
  void drawUI();

  float dt;

  csRef<iMeshFactoryWrapper> tankFactory;

  /// A pointer to the 3D engine.
  csRef<iEngine> engine;

  /// A pointer to the sector the camera will be in.
  iSector *room;

private:
  bool SetupModules();
  void CreateRoom();
  void setupRoamingCamera(float dt);

  ControlPanel       _controlPanel;
  RadarRenderer      _radar;
  HUDRenderer        _hud;
  BackgroundRenderer background;

  GLfloat            fov;
  GLfloat            eyePoint[3];
  GLfloat            targetPoint[3];

  /// A pointer to the map loader plugin.
  csRef<iLoader> loader;

  /// A pointer to the 3D renderer plugin.
  csRef<iGraphics3D> g3d;

  /// A pointer to the 2D renderer plugin.
  csRef<iGraphics2D> g2d;

  /// A pointer to the keyboard driver.
  csRef<iKeyboardDriver> kbd;

  /// A pointer to the virtual clock.
  csRef<iVirtualClock> vc;

  /// A pointer to the view which contains the camera.
  csRef<iView> view;

  /// Event handlers to draw and print the 3D canvas on each frame
  csRef<FrameBegin3DDraw> drawer;
  csRef<FramePrinter>     printer;
};

#endif // BZF_PLAYING_H

// Local Variables: ***
// mode:C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8
