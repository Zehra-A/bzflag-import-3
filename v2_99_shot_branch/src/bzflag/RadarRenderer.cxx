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

// interface header
#include "RadarRenderer.h"

// common implementation headers
#include "SceneRenderer.h"
#include "MainWindow.h"
#include "OpenGLGState.h"
#include "BZDBCache.h"
#include "TextureManager.h"
#include "PhysicsDriver.h"

#include "TimeKeeper.h"

// local implementation headers
#include "ObstacleMgr.h"
#include "ObstacleList.h"
#include "MeshSceneNode.h"
#include "WallObstacle.h"
#include "LocalPlayer.h"
#include "World.h"
#include "ShotPath.h"
#include "AutoHunt.h"
#include "ShotList.h"


const float RadarRenderer::colorFactor = 40.0f;


RadarRenderer::~RadarRenderer()
{
  clearRadarObjects();
}


void RadarRenderer::clearRadarObjects ( void )
{
  RadarObjectMap::iterator itr = radarObjectLists.begin();
  
  DisplayListSystem &ds = DisplayListSystem::Instance();

  while (itr != radarObjectLists.end()) {
    ds.freeList(itr->first);
    itr++;
  }

  radarObjectLists.clear();
}

RadarRenderer::RadarRenderer(const SceneRenderer&, World* _world)
  : world(_world),
    x(0),
    y(0),
    w(0),
    h(0),
    dimming(0.0f),
    decay(0.01f),
    jammed(false),
    colorblind(false),
    multiSampled(false)
{

  clearRadarObjects();
  lastFast = false;
  setControlColor();

#if defined(GLX_SAMPLES_SGIS) && defined(GLX_SGIS_multisample)
  GLint bits;
  glGetIntergerv(GL_SAMPLES_SGIS, &bits);
  if (bits > 0) multiSampled = true;
#endif
}

void RadarRenderer::setWorld(World* _world)
{
  world = _world;
  clearRadarObjects();
}


void RadarRenderer::setControlColor(const GLfloat *color)
{
  if (color)
    memcpy(teamColor, color, 3 * sizeof(float));
  else
    memset(teamColor, 0, 3 * sizeof(float));
}


void RadarRenderer::setShape(int _x, int _y, int _w, int _h)
{
  x = _x;
  y = _y;
  w = _w;
  h = _h;
}


void RadarRenderer::setJammed(bool _jammed)
{
  jammed = _jammed;
  decay = 0.01;
}


void RadarRenderer::setDimming(float newDimming)
{
  dimming = (1.0f - newDimming > 1.0f) ? 1.0f : (1.0f - newDimming < 0.0f) ? 0.0f : 1.0f - newDimming;
}


void RadarRenderer::drawShot(const ShotPath* shot)
{
  glBegin(GL_POINTS); {
    glVertex2fv(shot->getPosition());
  } glEnd();
}


void RadarRenderer::drawTank(const Player* player, bool allowFancy)
{
  glPushMatrix();

  const float* pos = player->getPosition();

  // 'ps' is pixel scale, setup in render()
  const float tankRadius = BZDBCache::tankRadius;
  float minSize = 1.5f + (ps * BZDBCache::radarTankPixels);
  GLfloat size;
  if (tankRadius < minSize) {
    size = minSize;
  } else {
    size = tankRadius;
  }
  if (pos[2] < 0.0f) {
    size = 0.5f;
  }

  // NOTE: myTank was checked in render()
  const float myAngle = LocalPlayer::getMyTank()->getAngle();

  // transform to the tanks location
  glTranslatef(pos[0], pos[1], 0.0f);

  // draw the tank
  if (!allowFancy || !useTankDimensions) {
    // align to the screen axes
    glRotatef(float(myAngle * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);
    glRectf(-size, -size, +size, +size);
  }
  else {
    const float tankAngle = player->getAngle();
    glPushMatrix();
    glRotatef(float(tankAngle * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);
    if (useTankModels) {
      drawFancyTank(player);
    } else {
      const float* dims = player->getDimensions();
      glRectf(-dims[0], -dims[1], +dims[0], +dims[1]);
    }
    glPopMatrix();

    // align to the screen axes
    glRotatef(float(myAngle * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);
  }

  // adjust with height box size
  const float boxHeight = BZDB.eval(StateDatabase::BZDB_BOXHEIGHT);
  const float hbSize = size * (1.0f + (0.5f * (pos[2] / boxHeight)));

  // draw the height box
  glBegin(GL_LINE_STRIP); {
    glVertex2f(-hbSize, 0.0f);
    glVertex2f(0.0f, -hbSize);
    glVertex2f(+hbSize, 0.0f);
    glVertex2f(0.0f, +hbSize);
    glVertex2f(-hbSize, 0.0f);
  } glEnd();

  // draw the AutoHunt indicator
  if (!colorblind) {
    drawHuntLevel(player, size, hbSize);
  }

  glPopMatrix();
}


void RadarRenderer::drawFancyTank(const Player* player)
{
  if (smooth) {
    glDisable(GL_BLEND);
  }

  // we use the depth buffer so that the treads look ok
  if (BZDBCache::zbuffer) {
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
  }

  OpenGLGState::resetState();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  RENDERER.enableSun(true);

  player->renderRadar(); // draws at (0,0,0)

  RENDERER.enableSun(false);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // FIXME - height box color
  OpenGLGState::resetState();

  if (BZDBCache::zbuffer) {
    glDisable(GL_DEPTH_TEST);
  }

  if (smooth) {
    glEnable(GL_BLEND);
  }

  return;
}


void RadarRenderer::drawHuntLevel(const Player* player,
				  float tankSize, float heightBoxSize)
{
  const int huntLevel = player->getAutoHuntLevel();
  const int chevrons = AutoHunt::getChevronCount(huntLevel);
  if (chevrons < 1) {
    return;
  }

  // setup the color
  float color[4];
  const double diffTime = TimeKeeper::getTick() - TimeKeeper::getStartTime();
  const float period = AutoHunt::getBlinkPeriod(huntLevel);
  const float thresh = AutoHunt::getOuterBlinkThreshold(huntLevel);
  const bool blink = fmodf((float)diffTime, period) < (period * thresh);
  if (blink) {
    color[0] = 0.5f;
    color[1] = 0.5f;
    color[2] = 0.5f;
  } else {
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
  }
  // translucency range
  const float innerAlpha = AutoHunt::getChevronInnerAlpha(huntLevel);
  const float outerAlpha = AutoHunt::getChevronOuterAlpha(huntLevel);
  color[3] = innerAlpha;
  const float alphaRange = innerAlpha - outerAlpha;
  float alphaDelta = 0.0f;
  if (chevrons > 1) {
    alphaDelta = alphaRange / (float)(chevrons - 1);
  }

  // setup the geometry
  float s = (float)M_SQRT1_2 * heightBoxSize;
  const float spacing = ps * AutoHunt::getChevronSpace(huntLevel);
  const float check1 = spacing * 2.0f;
  if (s < check1) {
    s = check1;
  }
  const float check2 = tankSize + spacing;
  if (s < check2) {
    s = check2;
  }
  const float gap = 0.4f;
  float c = s * (1.0f - gap);

  const float cDelta = spacing * AutoHunt::getChevronDelta(huntLevel);

  if (!smooth) {
    glEnable(GL_BLEND);
  }

  // draw the chevrons
  for (int i = 0; i < chevrons; i++) {
    glColor4fv(color);
    glBegin(GL_LINE_STRIP);
    glVertex2f(+s, +c);
    glVertex2f(+s, +s);
    glVertex2f(+c, +s);
    glEnd();
    glBegin(GL_LINE_STRIP);
    glVertex2f(-s, -c);
    glVertex2f(-s, -s);
    glVertex2f(-c, -s);
    glEnd();
    glBegin(GL_LINE_STRIP);
    glVertex2f(-c, +s);
    glVertex2f(-s, +s);
    glVertex2f(-s, +c);
    glEnd();
    glBegin(GL_LINE_STRIP);
    glVertex2f(+c, -s);
    glVertex2f(+s, -s);
    glVertex2f(+s, -c);
    glEnd();
    // setup for the next pass
    s += spacing;
    c += cDelta;
    color[3] -= alphaDelta;
  }

  if (!smooth) {
    glDisable(GL_BLEND);
  }

  return;
}


void RadarRenderer::drawFlag(const float pos[3])
{
  GLfloat s = BZDBCache::flagRadius > 3.0f * ps ? BZDBCache::flagRadius : 3.0f * ps;
  glBegin(GL_LINES);
  glVertex2f(pos[0] - s, pos[1]);
  glVertex2f(pos[0] + s, pos[1]);
  glVertex2f(pos[0] + s, pos[1]);
  glVertex2f(pos[0] - s, pos[1]);
  glVertex2f(pos[0], pos[1] - s);
  glVertex2f(pos[0], pos[1] + s);
  glVertex2f(pos[0], pos[1] + s);
  glVertex2f(pos[0], pos[1] - s);
  glEnd();
}

void RadarRenderer::drawFlagOnTank(const float pos[3])
{
  glPushMatrix();

  // align it to the screen axes
  const float angle = LocalPlayer::getMyTank()->getAngle();
  glTranslatef(pos[0], pos[1], 0.0f);
  glRotatef(float(angle * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);

  float tankRadius = BZDBCache::tankRadius;
  GLfloat s = 2.5f * tankRadius > 4.0f * ps ? 2.5f * tankRadius : 4.0f * ps;
  glBegin(GL_LINES);
  glVertex2f(-s, 0.0f);
  glVertex2f(+s, 0.0f);
  glVertex2f(+s, 0.0f);
  glVertex2f(-s, 0.0f);
  glVertex2f(0.0f, -s);
  glVertex2f(0.0f, +s);
  glVertex2f(0.0f, +s);
  glVertex2f(0.0f, -s);
  glEnd();

  glPopMatrix();
}


void RadarRenderer::renderFrame(SceneRenderer& renderer)
{
  const MainWindow& window = renderer.getWindow();

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, window.getWidth(), 0.0, window.getHeight(), -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  OpenGLGState::resetState();

  const int ox = window.getOriginX();
  const int oy = window.getOriginY();

  glScissor(ox + x - 1, oy + y - 1, w + 2, h + 2);

  const float left = float(ox + x) - 1;
  const float right = float(ox + x + w) + 1;
  const float top = float(oy + y) - 1;
  const float bottom = float(oy + y + h) + 1;

  float outlineOpacity = RENDERER.getPanelOpacity();
  float fudgeFactor = BZDBCache::hudGUIBorderOpacityFactor;	// bzdb cache this manybe?
  if ( outlineOpacity < 1.0f )
    outlineOpacity = (outlineOpacity*fudgeFactor) + (1.0f - fudgeFactor);

  if (BZDBCache::blend)
    glEnable(GL_BLEND);

  glColor4f(teamColor[0],teamColor[1],teamColor[2],outlineOpacity);
  glOutlineBoxHV(10,left,top,right,bottom);
  if (BZDBCache::blend)
    glDisable(GL_BLEND);

  glColor4f(teamColor[0],teamColor[1],teamColor[2],1.0f);

  const float opacity = renderer.getPanelOpacity();
  if ((opacity < 1.0f) && (opacity > 0.0f)) {
    glScissor(ox + x - 2, oy + y - 2, w + 4, h + 4);
    // draw nice blended background
    if (BZDBCache::blend && opacity < 1.0f)
      glEnable(GL_BLEND);
    glColor4f(0.0f, 0.0f, 0.0f, opacity);
    glRectf((float) x, (float) y, (float)(x + w), (float)(y + h));
    if (BZDBCache::blend && opacity < 1.0f)
      glDisable(GL_BLEND);
  }

  // note that this scissor setup is used for the reset of the rendering
  glScissor(ox + x, oy + y, w, h);

  if (opacity == 1.0f) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();

  return;
}


static bool checkDrawFlags()
{
  if (!BZDB.isTrue("autoFlagDisplay")) {
    return BZDB.isTrue("displayRadarFlags");
  }
  const LocalPlayer *myTank = LocalPlayer::getMyTank();
  if (!myTank) {
    return BZDB.isTrue("displayRadarFlags");
  }
  // pick the automatic mode
  const FlagType* ft = myTank->getFlag();
  if ((ft == Flags::Null)     ||
      (ft == Flags::Identify) ||
      (ft->flagQuality == FlagBad)) {
    return true;
  }
  return false;
}

bool RadarRenderer::renderNoise ( SceneRenderer& renderer )
{
  const float radarLimit = BZDBCache::radarLimit;
  float radarRange = BZDB.eval("displayRadarRange") * radarLimit;
  float maxRange = radarLimit;

  if (jammed && (bzfrand() > decay))// if jammed then draw white noise.  occasionally draw a good frame.
  { 
    TextureManager &tm = TextureManager::instance();
    int noiseTexture = tm.getTextureID( "noise" );

    glColor3f(1.0f, 1.0f, 1.0f);
    if ((noiseTexture >= 0) && (renderer.useQuality() > _LOW_QUALITY)) 
    {
      const int sequences = 10;

      static float np[] =
      { 0, 0, 1, 1,
      1, 1, 0, 0,
      0.5f, 0.5f, 1.5f, 1.5f,
      1.5f, 1.5f, 0.5f, 0.5f,
      0.25f, 0.25f, 1.25f, 1.25f,
      1.25f, 1.25f, 0.25f, 0.25f,
      0, 0.5f, 1, 1.5f,
      1, 1.5f, 0, 0.5f,
      0.5f, 0, 1.5f, 1,
      1.4f, 1, 0.5f, 0,
      0.75f, 0.75f, 1.75f, 1.75f,
      1.75f, 1.75f, 0.75f, 0.75f,
      };

      int noisePattern = 4 * int(floor(sequences * bzfrand()));

      glEnable(GL_TEXTURE_2D);
      tm.bind(noiseTexture);

      glBegin(GL_QUADS); 
      {
	glTexCoord2f(np[noisePattern+0],np[noisePattern+1]);
	glVertex2f(-radarRange,-radarRange);
	glTexCoord2f(np[noisePattern+2],np[noisePattern+1]);
	glVertex2f( radarRange,-radarRange);
	glTexCoord2f(np[noisePattern+2],np[noisePattern+3]);
	glVertex2f( radarRange, radarRange);
	glTexCoord2f(np[noisePattern+0],np[noisePattern+3]);
	glVertex2f(-radarRange, radarRange);
      } 
      glEnd();

      glDisable(GL_TEXTURE_2D);
    }
    else if ((noiseTexture >= 0) && BZDBCache::texture && (renderer.useQuality() == _LOW_QUALITY))
    {
      glEnable(GL_TEXTURE_2D);
      tm.bind(noiseTexture);

      glBegin(GL_QUADS);
      {
	glTexCoord2f(0,0);
	glVertex2f(-radarRange,-radarRange);
	glTexCoord2f(1,0);
	glVertex2f( radarRange,-radarRange);
	glTexCoord2f(1,1);
	glVertex2f( radarRange, radarRange);
	glTexCoord2f(0,1);
	glVertex2f(-radarRange, radarRange);
      }
      glEnd();

      glDisable(GL_TEXTURE_2D);
    }
    if (decay > 0.015f)
      decay *= 0.5f;
  }
  else
  {

    // if decay is sufficiently small then boost it so it's more
    // likely a jammed radar will get a few good frames closely
    // spaced in time.  value of 1 guarantees at least two good
    // frames in a row.
    if (decay <= 0.015f)
      decay = 1.0f;
    else
      decay *= 0.5f;

    // return false so we draw a real frame this time
    return false;
  }

  return true;
}


void RadarRenderer::render(SceneRenderer& renderer, bool blank, bool observer)
{
  RenderNode::resetTriangleCount();

  const float radarLimit = BZDBCache::radarLimit;
  if (!BZDB.isTrue("displayRadar") || (radarLimit <= 0.0f))
  {
    triangleCount = 0;
    return;
  }

  // render the frame
  renderFrame(renderer);

  if (blank || !world)
    return;

  smooth = !multiSampled && BZDBCache::smooth;
  const bool fastRadar = ((BZDBCache::radarStyle == 1) || (BZDBCache::radarStyle == 2)) && BZDBCache::zbuffer;
  const LocalPlayer *myTank = LocalPlayer::getMyTank();

  // setup the radar range
  float radarRange = BZDB.eval("displayRadarRange") * radarLimit;
  float maxRange = radarLimit;

  // when burrowed, limit radar range
  if (myTank && (myTank->getFlag() == Flags::Burrow) &(myTank->getPosition()[2] < 0.0f))
    maxRange = radarLimit / 4.0f;

  if (radarRange > maxRange)
  {
    radarRange = maxRange;
    // only clamp the user's desired range if it's actually
    // greater then 1. otherwise, we may be resetting it due
    // to burrow radar limiting.
    if (BZDB.eval("displayRadarRange") > 1.0f) 
      BZDB.set("displayRadarRange", "1.0");
  }

  // prepare projection matrix
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  const MainWindow& window = renderer.getWindow();
  const int xSize = window.getWidth();
  const int ySize = window.getHeight();
  const double xCenter = double(x) + 0.5 * double(w);
  const double yCenter = double(y) + 0.5 * double(h);
  const double xUnit = 2.0 * radarRange / double(w);
  const double yUnit = 2.0 * radarRange / double(h);
  // NOTE: the visual extents include passable objects
  double maxHeight = 0.0;
  const Extents* visExts = renderer.getVisualExtents();
  if (visExts) 
    maxHeight = (double)visExts->maxs[2];

  glOrtho(-xCenter * xUnit, (xSize - xCenter) * xUnit,
	  -yCenter * yUnit, (ySize - yCenter) * yUnit,
	  -(maxHeight + 10.0), (maxHeight + 10.0));

  // prepare modelview matrix
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  OpenGLGState::resetState();

  if (!renderNoise(renderer) && myTank && world)   // if we are drawing a radar, do so
  {
    colorblind = (myTank->getFlag() == Flags::Colorblindness);

    // get size of pixel in model space (assumes radar is square)
    ps = 2.0f * (radarRange / GLfloat(w));
    MeshSceneNode::setRadarLodScale(ps);

    float tankWidth = BZDBCache::tankWidth;
    float tankLength = BZDBCache::tankLength;
    const float testMin = 8.0f * ps;
    // maintain the aspect ratio if it isn't square
    if ((tankWidth > testMin) &&  (tankLength > testMin))
      useTankDimensions = true;
    else 
      useTankDimensions = false;

    if (useTankDimensions && (renderer.useQuality() >= _HIGH_QUALITY))
      useTankModels = true;
    else
      useTankModels = false;

    // relative to my tank
    const float* myPos = myTank->getPosition();
    const float myAngle = myTank->getAngle();

    // draw the view angle below stuff
    // view frustum edges
    if (!BZDB.isTrue("hideRadarViewLines"))
    {
      glColor3f(1.0f, 0.625f, 0.125f);
      const float fovx = renderer.getViewFrustum().getFOVx();
      const float viewWidth = radarRange * tanf(0.5f * fovx);
      if (BZDB.isTrue("showShotGuide"))
      {
	glBegin(GL_LINES);
	glVertex2f(-viewWidth, radarRange);
	glVertex2f(0.0f, 0.0f);
	glVertex2f(viewWidth, radarRange);
	glVertex2f(0.0f, 0.0f);
	glColor3f(0.5f, 0.3125f, 0.0625f);
	glVertex2f(0.0f, radarRange);
	glVertex2f(0.0f, 0.0f);
      }
      else 
      {
	glBegin(GL_LINE_STRIP);
	glVertex2f(-viewWidth, radarRange);
	glVertex2f(0.0f, 0.0f);
	glVertex2f(viewWidth, radarRange);
      }
      glEnd();
    }

    // transform to the observer's viewpoint
    glPushMatrix();
    glRotatef((float)(90.0 - myAngle * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);
    glPushMatrix();
    glTranslatef(-myPos[0], -myPos[1], 0.0f);

    if (useTankModels)
      renderer.setupSun();      // new modelview transform requires repositioning

    // setup the blending function
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // draw the buildings
    renderObstacles(fastRadar, radarRange);

    // antialiasing on for lines and points unless we're multisampling,
    // in which case it's automatic and smoothing makes them look worse.
    if (smooth)
    {
      glEnable(GL_BLEND);
      glEnable(GL_LINE_SMOOTH);
      glEnable(GL_POINT_SMOOTH);
    }

    std::vector<ShotPath*> shots = ShotList::instance().getShotList();

    bool coloredShot = BZDB.isTrue("coloredradarshots");

    const float *shotcolor;
    const float white[4] = {1,1,1,1};
    float muzzleHeight = BZDB.eval(StateDatabase::BZDB_MUZZLEHEIGHT);
    bool iSeeAll = myTank && (myTank->getFlag() == Flags::Seer);

    // draw all the shots
    for ( size_t s = 0; s < shots.size(); s++ )
    {
      ShotPath* shot = shots[s];

      shotcolor = white; // default to white;
     
      const float cs = colorScale(shot->getPosition()[2], muzzleHeight);  // compensate for height for all shots

      if ( !shot->isLocal() && coloredShot ) // if we want to use colored shots, if we want colorbind to be extra mean, put the !colorblind in this if
	shotcolor = Team::getRadarColor(colorblind ? RogueTeam : shot->getTeam());
      glColor3f(shotcolor[0] * cs, shotcolor[1] * cs, shotcolor[2] * cs);

      if (shot->isLocal() || (shot->getShotType() != InvisibleShot || iSeeAll))	// if it's local always draw it, else only draw shots we can see
	shot->radarRender();
    }

    // used for blinking tanks
    const double diffTime = TimeKeeper::getTick() - TimeKeeper::getStartTime();

    // draw other tanks (and any flags on them)
    // note about flag drawing.  each line segment is drawn twice
    // (once in each direction);  this degrades the antialiasing
    // but on systems that don't do correct filtering of endpoints
    // not doing it makes (half) the endpoints jump wildly.
    const int curMaxPlayers = world->getCurMaxPlayers();
    for (int i = 0; i < curMaxPlayers; i++)
    {
      RemotePlayer* player = world->getPlayer(i);
      if (!player)
	continue;
      
      if (!player->isAlive() && (!useTankModels || !observer || !player->isExploding()))
	continue;
  
      if ((player->getFlag() == Flags::Stealth) && (myTank->getFlag() != Flags::Seer)) 
	continue;

      const float* position = player->getPosition();

      if (player->getFlag() != Flags::Null)
      {
	glColor3fv(player->getFlag()->getColor());
	drawFlagOnTank(position);
      }

      if (player->isPaused() || player->isNotResponding())
      {
	const float dimfactor = 0.4f;

	const float *color;
	if (colorblind)
	  color = Team::getRadarColor(RogueTeam);
	else 
	  color = Team::getRadarColor(player->getTeam());

	float dimmedcolor[3];
	dimmedcolor[0] = color[0] * dimfactor;
	dimmedcolor[1] = color[1] * dimfactor;
	dimmedcolor[2] = color[2] * dimfactor;
	glColor3fv(dimmedcolor);
      } 
      else 
      {
	const TeamColor team = colorblind ? RogueTeam : player->getTeam();
	glColor3fv(Team::getRadarColor(team));
      }

      // If this tank is hunted flash it on the radar
      int huntLevel = player->isHunted() ? 9 : player->getAutoHuntLevel();
      if ((huntLevel > 0) && !colorblind)
      {
	float period = 0.40f;
	float thresh = 0.25f;
	if (!player->isHunted())
	{
	  period = AutoHunt::getBlinkPeriod(huntLevel);
	  thresh = AutoHunt::getInnerBlinkThreshold(huntLevel);
	}
	const bool blink = fmodf((float)diffTime, period) < (period * thresh);
	if (blink)
	{
	  const float greenBlinkColor[3] = {1.0f, 0.8f, 1.0f};
	  const float normalBlinkColor[3] = {0.0f, 0.8f, 0.9f};
	  if (player->getTeam() == GreenTeam)
	    glColor3fv(greenBlinkColor);
	  else
	    glColor3fv(normalBlinkColor);
	} 
	else
	  glColor3fv(Team::getRadarColor(player->getTeam()));
      }

      drawTank(player, observer);
    }

    // draw flags not on tanks.
    // draw them in reverse order so that the team flags
    // (which come first), are drawn on top of the normal flags.
    const int maxFlags = world->getMaxFlags();
    const bool drawNormalFlags = checkDrawFlags();
    for (int i = (maxFlags - 1); i >= 0; i--)
    {
      const Flag& flag = world->getFlag(i);
      // don't draw flags that don't exist or are on a tank
      if (flag.status == FlagNoExist || flag.status == FlagOnTank)
	continue;

      // don't draw normal flags if we aren't supposed to
      if (flag.type->flagTeam == NoTeam && !drawNormalFlags)
	continue;

      // Flags change color by height
      const float cs = colorScale(flag.position[2], muzzleHeight);
      const float *flagcolor = flag.type->getColor();
      glColor3f(flagcolor[0] * cs, flagcolor[1] * cs, flagcolor[2] * cs);
      drawFlag(flag.position);
    }

    // draw antidote flag
    const float* antidotePos =
      LocalPlayer::getMyTank()->getAntidoteLocation();
    if (antidotePos)
    {
      glColor3f(1.0f, 1.0f, 0.0f);
      drawFlag(antidotePos);
    }

    // draw these markers above all others always centered
    glPopMatrix();

    // north marker
    GLfloat ns = 0.05f * radarRange, ny = 0.9f * radarRange;
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_LINE_STRIP);
    glVertex2f(-ns, ny - ns);
    glVertex2f(-ns, ny + ns);
    glVertex2f(ns, ny - ns);
    glVertex2f(ns, ny + ns);
    glEnd();

    // always up
    glPopMatrix();

    // forward tick
    glBegin(GL_LINES);
    glVertex2f(0.0f, radarRange - ps);
    glVertex2f(0.0f, radarRange - 4.0f * ps);
    glEnd();

    if (!observer) 
    {
      // revert to the centered transformation
      glRotatef((float)(90.0 - myAngle * 180.0 / M_PI), 0.0f, 0.0f, 1.0f);
      glTranslatef(-myPos[0], -myPos[1], 0.0f);

      // my flag
      if (myTank->getFlag() != Flags::Null) 
      {
	glColor3fv(myTank->getFlag()->getColor());
	drawFlagOnTank(myPos);
      }

      // my tank
      glColor3f(1.0f, 1.0f, 1.0f);
      drawTank(myTank, true);
    }

    if (dimming > 0.0f)
    {
      if (!smooth)
	glEnable(GL_BLEND);

      // darken the entire radar if we're dimmed
      // we're drawing positively, so dimming is actually an opacity
      glColor4f(0.0f, 0.0f, 0.0f, 1.0f - dimming);
      glRectf(-radarRange, -radarRange, +radarRange, +radarRange);
    }
    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POINT_SMOOTH);
  }

  // restore GL state
  glPopMatrix();

  triangleCount = RenderNode::getTriangleCount();
}


float RadarRenderer::colorScale(const float z, const float h)
{
  float scaleColor;
  if (BZDBCache::radarStyle > 0) {
    const LocalPlayer* myTank = LocalPlayer::getMyTank();

    // Scale color so that objects that are close to tank's level are opaque
    const float zTank = myTank->getPosition()[2];

    if (zTank > (z + h))
      scaleColor = 1.0f - (zTank - (z + h)) / colorFactor;
    else if (zTank < z)
      scaleColor = 1.0f - (z - zTank) / colorFactor;
    else
      scaleColor = 1.0f;

    // Don't fade all the way
    if (scaleColor < 0.35f)
      scaleColor = 0.35f;
  } else {
    scaleColor = 1.0f;
  }

  return scaleColor;
}


float RadarRenderer::transScale(const float z, const float h)
{
  float scaleColor;
  const LocalPlayer* myTank = LocalPlayer::getMyTank();

  // Scale color so that objects that are close to tank's level are opaque
  const float zTank = myTank->getPosition()[2];
  if (zTank > (z + h))
    scaleColor = 1.0f - (zTank - (z + h)) / colorFactor;
  else if (zTank < z)
    scaleColor = 1.0f - (z - zTank) / colorFactor;
  else
    scaleColor = 1.0f;

  if (scaleColor < 0.5f)
    scaleColor = 0.5f;

  return scaleColor;
}


void RadarRenderer::renderObstacles(bool fastRadar, float _range)
{
  if (smooth) {
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);
  }

  // draw the walls
  renderWalls();

  if (lastFast != fastRadar)
    clearRadarObjects();

  // draw the boxes, pyramids, and meshes
  if (!fastRadar) {
    renderBoxPyrMesh();
  } else {
    renderBoxPyrMeshFast(_range);
  }

  lastFast = fastRadar;

  // draw the team bases and teleporters
  renderBasesAndTeles();

  if (smooth) {
    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);
  }

  return;
}


void RadarRenderer::renderWalls()
{
  const ObstacleList& walls = OBSTACLEMGR.getWalls();
  int count = walls.size();
  glColor3f(0.25f, 0.5f, 0.5f);
  glBegin(GL_LINES);
  for (int i = 0; i < count; i++) {
    const WallObstacle& wall = *((const WallObstacle*) walls[i]);
    const float wid = wall.getBreadth();
    const float c   = wid * cosf(wall.getRotation());
    const float s   = wid * sinf(wall.getRotation());
    const float* pos = wall.getPosition();
    glVertex2f(pos[0] - s, pos[1] + c);
    glVertex2f(pos[0] + s, pos[1] - c);
  }
  glEnd();

  return;
}


void RadarRenderer::renderBoxPyrMeshFast(float _range)
{
  // FIXME - This is hack code at the moment, but even when
  //	 rendering the full world, it draws the aztec map
  //	 3X faster (the culling algo is actually slows us
  //	 down in that case)
  //	   - need a better default gradient texture
  //	     (better colors, and tied in to show max jump height?)
  //	   - build a procedural texture if default is missing
  //	   - use a GL_TEXTURE_1D
  //	   - setup the octree to return Z sorted elements (partially done)
  //	   - add a renderClass() member to SceneNode (also for coloring)
  //	   - also add a renderShadow() member (they don't need sorting,
  //	     and if you don't have double-buffering, you shouldn't be
  //	     using shadows)
  //	   - vertex shaders would be faster
  //	   - it would probably be a better approach to attach a radar
  //	     rendering object to each obstacle... no time

  // get the texture
  int gradientTexId = -1;
  TextureManager &tm = TextureManager::instance();
  gradientTexId = tm.getTextureID("radar", false);

  // safety: no texture, no service
  if (gradientTexId < 0) {
    renderBoxPyrMesh();
    return;
  }

  // GL state
  OpenGLGStateBuilder gb;
  gb.setTexture(gradientTexId);
  gb.setShading(GL_FLAT);
  gb.setCulling(GL_BACK);
  gb.setBlending(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  OpenGLGState gs = gb.getState();
  gs.setState();

  // disable the unused arrays
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);

  // now that the texture is bound, setup the clamp mode
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);

  // do this after the GState setting
  if (smooth) {
    glEnable(GL_POLYGON_SMOOTH);
  }

  // setup the texturing mapping
  const float hf = 128.0f; // height factor, goes from 0.0 to 1.0 in texcoords
  const float vfz = RENDERER.getViewFrustum().getEye()[2];
  const GLfloat plane[4] = { 0.0f, 0.0f, (1.0f / hf), (((hf * 0.5f) - vfz) / hf) };
  glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
  glTexGenfv(GL_S, GL_EYE_PLANE, plane);

  // setup texture generation
  glEnable(GL_TEXTURE_GEN_S);

  // set the color
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

  //  if (!BZDB.isTrue("visualRadar")) {
  ViewFrustum radarClipper;
  radarClipper.setOrthoPlanes(RENDERER.getViewFrustum(), _range, _range);
  RENDERER.getSceneDatabase()->renderRadarNodes(radarClipper);
  //  } else {
  //    RENDERER.getSceneDatabase()->renderRadarNodes(RENDERER.getViewFrustum());
  //  }

  // restore texture generation
  glDisable(GL_TEXTURE_GEN_S);

  OpenGLGState::resetState();

  // re-enable the arrays
  glEnableClientState(GL_NORMAL_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  // do this after the GState setting
  if (smooth) {
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);
    glDisable(GL_POLYGON_SMOOTH);
  }

  return;
}

void RadarRenderer::buildGeometry ( GLDisplayList displayList )
{
  // we need to make the geometry for one of our lists
  // see if the list is one of our objects
  // if it is build the geometry
  // don't do any color calls in here
  // as the color is set outside the list
  RadarObjectMap::iterator itr = radarObjectLists.find(displayList);
  if ( itr == radarObjectLists.end() )
    return;

  switch ( itr->second.first) {
    case eBoxPyr:
      buildBoxPyr(itr->second.second);
      break;
        
    case eMesh:
    case eMeshDeathFaces:
      buildMeshGeo((MeshObstacle*)itr->second.second,itr->second.first == eMeshDeathFaces);
      break;

    case eBoxPyrOutline:
      buildOutline(itr->second.second);
      break;

    default:
      break;
  }
}

// boxes and pyramids can share the same code, since they render just a box
// and optimisation for super fast radar may be to just use this radar box
// for meshes as well
void RadarRenderer::buildBoxPyr ( Obstacle* object )
{
  glBegin(GL_QUADS);
  //  const float z = object->getPosition()[2];
  //  const float bh = object->getHeight();
  const float c = cosf(object->getRotation());
  const float s = sinf(object->getRotation());
  const float wx = c * object->getWidth(), wy = s * object->getWidth();
  const float hx = -s * object->getBreadth(), hy = c * object->getBreadth();
  const float* pos = object->getPosition();

  glVertex2f(pos[0] - wx - hx, pos[1] - wy - hy);
  glVertex2f(pos[0] + wx - hx, pos[1] + wy - hy);
  glVertex2f(pos[0] + wx + hx, pos[1] + wy + hy);
  glVertex2f(pos[0] - wx + hx, pos[1] - wy + hy);
  glEnd();
}

void RadarRenderer::buildMeshGeo ( MeshObstacle* mesh, bool deathFaces )
{
  int faces = mesh->getFaceCount();

  for (int f = 0; f < faces; f++) {
    const MeshFace* face = mesh->getFace(f);
    if (face->getPlane()[2] <= 0.0f)
      continue;

    const BzMaterial* bzmat = face->getMaterial();
    if ((bzmat != NULL) && bzmat->getNoRadar())
      continue;

    //      const float z = face->getPosition()[2];
    //      const float bh = face->getHeight();

    // draw death faces with a soupcon of red
    const PhysicsDriver* phydrv = PHYDRVMGR.getDriver(face->getPhysicsDriver());
    bool isDeath = (phydrv != NULL) && phydrv->getIsDeath();
      
    if (isDeath != deathFaces)
      continue;
      
    // draw the face as a triangle fan
    int vertexCount = face->getVertexCount();
    glBegin(GL_TRIANGLE_FAN);
    for (int v = 0; v < vertexCount; v++) {
      const float* pos = face->getVertex(v);
      glVertex2f(pos[0], pos[1]);
    }
    glEnd();
  }
}

void RadarRenderer::buildOutline ( Obstacle* object )
{
  //  const float z = object->getPosition()[2];
  //  const float bh = object->getHeight();

  const float c = cosf(object->getRotation());
  const float s = sinf(object->getRotation());
  const float wx = c * object->getWidth(), wy = s * object->getWidth();
  const float hx = -s * object->getBreadth(), hy = c * object->getBreadth();
  const float* pos = object->getPosition();

  glBegin(GL_LINE_LOOP);
  glVertex2f(pos[0] - wx - hx, pos[1] - wy - hy);
  glVertex2f(pos[0] + wx - hx, pos[1] + wy - hy);
  glVertex2f(pos[0] + wx + hx, pos[1] + wy + hy);
  glVertex2f(pos[0] - wx + hx, pos[1] - wy + hy);
  glEnd();
}

void RadarRenderer::renderBoxPyrMesh()
{
  const bool enhanced = (BZDBCache::radarStyle > 0);

  DisplayListSystem &ds = DisplayListSystem::Instance();

  // if the object list is empty, we need to add the objects to it
  // this will generate the object geometry once and store
  // them in a display list, hopefully on the card
  // the display list manager will regenerate the list
  // data if needed ( context invalidation ).
  if ( !radarObjectLists.size()) {
    // add box buildings.
    const ObstacleList& boxes = OBSTACLEMGR.getBoxes();
    for (unsigned int i = 0; i < (unsigned int )boxes.size(); i++) {
      if (((BoxBuilding*)boxes[i])->isInvisible())
	continue;
      radarObjectLists[ds.newList(this)] = RadarObject(eBoxPyr,(BoxBuilding*)boxes[i]);
    }
    
    // add pyramid buildings
    const ObstacleList& pyramids = OBSTACLEMGR.getPyrs();
    for (unsigned int i = 0; i < (unsigned int )pyramids.size(); i++)
      radarObjectLists[ds.newList(this)] = RadarObject(eBoxPyr,(PyramidBuilding*)pyramids[i]);
    
    // add meshes
    const ObstacleList& meshes = OBSTACLEMGR.getMeshes();
    for (unsigned int i = 0; i < (unsigned int)meshes.size(); i++) {
      // add a list for the mesh regular faces, and one for the death faces.
      // if the mesh has no death faces, it'll be an empty list, no big loss.
      // we could flag each object if it has any death faces on load and keep
      // that as part of obstacle, to clean this up, but it may not be too bad.
      radarObjectLists[ds.newList(this)] = RadarObject(eMesh,(MeshObstacle*)meshes[i]);
      radarObjectLists[ds.newList(this)] = RadarObject(eMeshDeathFaces,(MeshObstacle*)meshes[i]);
    }
    
    // add the outlines for boxes and pyramids
    for (unsigned int i = 0; i < (unsigned int )boxes.size(); i++) {
      if (((BoxBuilding*)boxes[i])->isInvisible())
	continue;
      radarObjectLists[ds.newList(this)] = RadarObject(eBoxPyrOutline,(BoxBuilding*)boxes[i]);
    }
    for (unsigned int i = 0; i < (unsigned int )pyramids.size(); i++)
      radarObjectLists[ds.newList(this)] = RadarObject(eBoxPyrOutline,(PyramidBuilding*)pyramids[i]);
  }
  
  // if we have lists to render, call them,
  if (radarObjectLists.size()) {
    RadarObjectMap::iterator itr = radarObjectLists.begin();

    RadarObjectType lastType = eNone;

    while ( itr != radarObjectLists.end() ) {
      // all objcts use the same color scale
      const float z = itr->second.second->getPosition()[2];
      const float bh = itr->second.second->getHeight();
      const float cs = colorScale(z, bh);

      // cus lookups deep in STL are fugly
      RadarObjectType thisType = itr->second.first;
      GLDisplayList list = itr->first;

      // if the last type is difrent then this type
      // we have to change some of the blend modes
      // we want to minimise these changes to the state
      // so don't do them every object.
      if ( thisType != lastType ) {
	if ( thisType == eBoxPyr ) {
	  if (!smooth) {
	    // smoothing has blending disabled
	    if (enhanced)
	      glEnable(GL_BLEND); // always blend the polygons if we're enhanced
	  } else {
	    // smoothing has blending enabled
	    if (!enhanced)
	      glDisable(GL_BLEND); // don't blend the polygons if we're not enhanced
	  }
	} else if ( thisType == eBoxPyrOutline ) {
	  if (!enhanced)
	    glEnable(GL_CULL_FACE);

	  if (smooth) {
	    glDisable(GL_POLYGON_SMOOTH);
	    glEnable(GL_BLEND); // NOTE: revert from the enhanced setting
	  } else if (enhanced)
	    glDisable(GL_BLEND);
	}
	else {
	  // draw mesh obstacles
	  if (smooth)
	    glEnable(GL_POLYGON_SMOOTH);

	  if (!enhanced)
	    glDisable(GL_CULL_FACE);
	}
      }

      // the only thing that gets difrent colors is the death faces
      if ( thisType == eMeshDeathFaces)
	glColor4f(0.75f * cs, 0.25f * cs, 0.25f * cs, transScale(z, bh));
      else
	glColor4f(0.25f * cs, 0.5f * cs, 0.5f * cs, transScale(z, bh));

      // draw all lists, except outlines when we arn't smoothing
      if ( thisType != eBoxPyrOutline || ( thisType == eBoxPyrOutline && smooth ) )
	ds.callList(list);
      
      itr++;
    }
  }
  return;
}


void RadarRenderer::renderBasesAndTeles()
{
  int i;

  // draw team bases
  if(world->allowTeamFlags()) {
    for(i = 1; i < NumTeams; i++) {
      for (int j = 0;;j++) {
	const float *base = world->getBase(i, j);
	if (base == NULL)
	  break;
	glColor3fv(Team::getRadarColor(TeamColor(i)));
	glBegin(GL_LINE_LOOP);
	const float beta = atan2f(base[5], base[4]);
	const float r = hypotf(base[4], base[5]);
	glVertex2f(base[0] + r * cosf(base[3] + beta),
		   base[1] + r * sinf(base[3] + beta));
	glVertex2f(base[0] + r * cosf((float)(base[3] - beta + M_PI)),
		   base[1] + r * sinf((float)(base[3] - beta + M_PI)));
	glVertex2f(base[0] + r * cosf((float)(base[3] + beta + M_PI)),
		   base[1] + r * sinf((float)(base[3] + beta + M_PI)));
	glVertex2f(base[0] + r * cosf(base[3] - beta),
		   base[1] + r * sinf(base[3] - beta));
	glEnd();
      }
    }
  }

  // draw teleporters.  teleporters are pretty thin so use lines
  // (which, if longer than a pixel, are guaranteed to draw something;
  // not so for a polygon).  just in case the system doesn't correctly
  // filter the ends of line segments, we'll draw the line in each
  // direction (which degrades the antialiasing).  Newport graphics
  // is one system that doesn't do correct filtering.
  const ObstacleList& teleporters = OBSTACLEMGR.getTeles();
  int count = teleporters.size();
  glColor3f(1.0f, 1.0f, 0.25f);
  glBegin(GL_LINES);
  for (i = 0; i < count; i++) {
    const Teleporter & tele = *((const Teleporter *) teleporters[i]);
    if (tele.isHorizontal ()) {
      const float z = tele.getPosition ()[2];
      const float bh = tele.getHeight ();
      const float cs = colorScale (z, bh);
      glColor4f (1.0f * cs, 1.0f * cs, 0.25f * cs, transScale (z, bh));
      const float c = cosf (tele.getRotation ());
      const float s = sinf (tele.getRotation ());
      const float wx = c * tele.getWidth (), wy = s * tele.getWidth ();
      const float hx = -s * tele.getBreadth (), hy = c * tele.getBreadth ();
      const float *pos = tele.getPosition ();
      glVertex2f (pos[0] - wx - hx, pos[1] - wy - hy);
      glVertex2f (pos[0] + wx - hx, pos[1] + wy - hy);

      glVertex2f (pos[0] + wx - hx, pos[1] + wy - hy);
      glVertex2f (pos[0] + wx + hx, pos[1] + wy + hy);

      glVertex2f (pos[0] + wx + hx, pos[1] + wy + hy);
      glVertex2f (pos[0] - wx + hx, pos[1] - wy + hy);

      glVertex2f (pos[0] - wx + hx, pos[1] - wy + hy);
      glVertex2f (pos[0] - wx - hx, pos[1] - wy - hy);

      glVertex2f (pos[0] - wx - hx, pos[1] - wy - hy);
      glVertex2f (pos[0] - wx - hx, pos[1] - wy - hy);
    }
    else {
      const float z = tele.getPosition ()[2];
      const float bh = tele.getHeight ();
      const float cs = colorScale (z, bh);
      glColor4f (1.0f * cs, 1.0f * cs, 0.25f * cs, transScale (z, bh));
      const float tw = tele.getBreadth ();
      const float c = tw * cosf (tele.getRotation ());
      const float s = tw * sinf (tele.getRotation ());
      const float *pos = tele.getPosition ();
      glVertex2f (pos[0] - s, pos[1] + c);
      glVertex2f (pos[0] + s, pos[1] - c);
      glVertex2f (pos[0] + s, pos[1] - c);
      glVertex2f (pos[0] - s, pos[1] + c);
    }
  }
  glEnd();

  return;
}


int RadarRenderer::getFrameTriangleCount() const
{
  return triangleCount;
}


// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8