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
#include "SegmentedShotStrategy.h"

/* system implementation headers */
#include <assert.h>

/* common implementation headers */
#include "TextureManager.h"
#include "Intersect.h"
#include "BZDBCache.h"
#include "WallObstacle.h"

SegmentedShotStrategy::SegmentedShotStrategy(ShotPath* _path) : ShotStrategy(_path)
{
  // initialize times
  prevTime = getPath().getStartTime();
  lastTime = currentTime = prevTime;

  // start at first segment
  lastSegment = segment = 0;

  // get team
  if (_path->getPlayer() == ServerPlayer) {
    TeamColor tmpTeam = _path->getFiringInfo().shot.team;
    team = (tmpTeam < RogueTeam) ? RogueTeam :
	   (tmpTeam > HunterTeam) ? RogueTeam : tmpTeam;
  } else {
      team = _path->getTeam();
  }
}

SegmentedShotStrategy::~SegmentedShotStrategy()
{
}

void			SegmentedShotStrategy::update(float dt)
{
  prevTime = currentTime;
  currentTime += dt;

  // see if we've moved to another segment
  const int numSegments = (const int)segments.size();
  if (segment < numSegments && segments[segment].end <= currentTime) {
    lastSegment = segment;
    while (segment < numSegments && segments[segment].end <= currentTime) {
      if (++segment < numSegments) {
	switch (segments[segment].reason) {
	  case ShotPathSegment::Ricochet:
	    {
//CLIENTEDIT
//	      // play ricochet sound.  ricochet of local player's shots
//	      // are important, others are not.
//	      const PlayerId myTankId = LocalPlayer::getMyTank()->getId();
//	      const bool important = (getPath().getPlayer() == myTankId);
//	      const float* pos = segments[segment].ray.getOrigin();
//	      playWorldSound(SFX_RICOCHET, pos, important);
//
//ENDCLIENTEDIT
	      // this is fugly but it's what we do
	      float dir[3];
	      const float* newDir = segments[segment].ray.getDirection();
	      const float* oldDir = segments[segment - 1].ray.getDirection();
	      dir[0] = newDir[0] - oldDir[0];
	      dir[1] = newDir[1] - oldDir[1];
	      dir[2] = newDir[2] - oldDir[2];

	      float rots[2];
	      const float horiz = sqrtf((dir[0]*dir[0]) + (dir[1]*dir[1]));
	      rots[0] = atan2f(dir[1], dir[0]);
	      rots[1] = atan2f(dir[2], horiz);
//CLIENTEDIT
//
//	      EFFECTS.addRicoEffect( pos, rots);
//ENDCLIENTEDIT
	      break;
	    }
	  case ShotPathSegment::Boundary:
	    break;
	  default:
	    {
	      // this is fugly but it's what we do
	      float dir[3];
	      dir[0] = segments[segment].ray.getDirection()[0];
	      dir[1] = segments[segment].ray.getDirection()[1];
	      dir[2] = segments[segment].ray.getDirection()[2];

	      float rots[2];
	      const float horiz = sqrtf((dir[0]*dir[0]) + (dir[1]*dir[1]));
	      rots[0] = atan2f(dir[1], dir[0]);
	      rots[1] = atan2f(dir[2], horiz);
//CLIENTEDIT
//
//	      const float* pos = segments[segment].ray.getOrigin();
//	      EFFECTS.addShotTeleportEffect( pos, rots);
//ENDCLIENTEDIT
	    }
	    break;
	}
      }
    }
  }

  // if ran out of segments then expire shot on next update
  if (segment == numSegments) {
    setExpiring();

    if (numSegments > 0) {
      ShotPathSegment &segm = segments[numSegments - 1];
      const float     *dir  = segm.ray.getDirection();
      const float speed = hypotf(dir[0], hypotf(dir[1], dir[2]));
      float pos[3];
      segm.ray.getPoint(float(segm.end - segm.start - 1.0 / speed), pos);
//CLIENTEDIT
//      /* NOTE -- comment out to not explode when shot expires */
//      addShotExplosion(pos);
//ENDCLIENTEDIT
    }
  }

  // otherwise update position and velocity
  else {
    float p[3];
    segments[segment].ray.getPoint(float(currentTime - segments[segment].start), p);
    setPosition(p);
    setVelocity(segments[segment].ray.getDirection());
  }
}

bool			SegmentedShotStrategy::predictPosition(float dt, float p[3]) const
{
  float ctime = (float)currentTime + dt;
  int cur=0;
  // see if we've moved to another segment
  const int numSegments = (const int)segments.size();
  while (cur < numSegments && segments[cur].end < ctime) cur++;
  if (cur >= numSegments) return false;

  segments[segment].ray.getPoint(float(ctime - segments[segment].start), p);

  return true;
}


bool			SegmentedShotStrategy::predictVelocity(float dt, float p[3]) const
{
  float ctime = (float)currentTime + dt;
  int cur=0;
  // see if we've moved to another segment
  const int numSegments = (const int)segments.size();
  while (cur < numSegments && segments[cur].end < ctime) cur++;
  if (cur >= numSegments) return false;

  const float *pos;
  pos = segments[segment].ray.getDirection();

  p[0] = pos[0]; p[1] = pos[1]; p[2] = pos[2];

  return true;
}


void			SegmentedShotStrategy::setCurrentTime(const
						double _currentTime)
{
  currentTime = _currentTime;
}

double	SegmentedShotStrategy::getLastTime() const
{
  return lastTime;
}

double SegmentedShotStrategy::getPreviousTime() const
{
  return prevTime;
}

bool			SegmentedShotStrategy::isOverlapping(
				const float (*bbox1)[3],
				const float (*bbox2)[3]) const
{
  if (bbox1[1][0] < bbox2[0][0]) return false;
  if (bbox1[0][0] > bbox2[1][0]) return false;
  if (bbox1[1][1] < bbox2[0][1]) return false;
  if (bbox1[0][1] > bbox2[1][1]) return false;
  if (bbox1[1][2] < bbox2[0][2]) return false;
  if (bbox1[0][2] > bbox2[1][2]) return false;
  return true;
}

void			SegmentedShotStrategy::setCurrentSegment(int _segment)
{
  segment = _segment;
}

void			SegmentedShotStrategy::makeSegments(ObstacleEffect e)
{
  // compute segments of shot until total length of segments exceeds the
  // lifetime of the shot.
  const ShotPath  &shotPath  = getPath();
  const float	  *v	 = shotPath.getVelocity();
  double	  startTime = shotPath.getStartTime();
  float		  timeLeft	    = shotPath.getLifetime();
  float		  minTime = BZDB.eval(StateDatabase::BZDB_MUZZLEFRONT) / hypotf(v[0], hypotf(v[1], v[2]));
//CHANGEEDIT
//  World		  *world = World::getWorld();
//  if (!world)
//    return; /* no world, no shots */
//ENDCHANGEEDIT

  // if all shots ricochet and obstacle effect is stop, then make it ricochet
//CHANGEEDIT
//  if (e == Stop && world->allShotsRicochet())
//CHANGED
//  if (e == Stop)
//    e = Reflect;
//
//ENDCHANGEEDIT
  // prepare first segment
  float o[3], d[3];
  d[0] = v[0];
  d[1] = v[1];
  d[2] = v[2];		// use v[2] to have jumping affect shot velocity
  o[0] = shotPath.getPosition()[0];
  o[1] = shotPath.getPosition()[1];
  o[2] = shotPath.getPosition()[2];

  segments.clear();
  ShotPathSegment::Reason reason = ShotPathSegment::Initial;
  int i;
  const int maxSegment = 100;
  float worldSize = BZDBCache::worldSize / 2.0f - 0.01f;
  for (i = 0; (i < maxSegment) && (timeLeft > Epsilon); i++) {
    // construct ray and find the first building, teleporter, or outer wall
    float o2[3];
    o2[0] = o[0] - minTime * d[0];
    o2[1] = o[1] - minTime * d[1];
    o2[2] = o[2] - minTime * d[2];

    // Sometime shot start outside world
    if (o2[0] <= -worldSize)
      o2[0] = -worldSize;
    if (o2[0] >= worldSize)
      o2[0] = worldSize;
    if (o2[1] <= -worldSize)
      o2[1] = -worldSize;
    if (o2[1] >= worldSize)
      o2[1] = worldSize;

    Ray r(o2, d);
    Ray rs(o, d);
    float t = timeLeft + minTime;
    int face;
    bool hitGround = getGround(r, Epsilon, t);
    Obstacle* building = (Obstacle*)((e != Through) ? getFirstBuilding(r, Epsilon, t) : NULL);
    const Teleporter* teleporter = getFirstTeleporter(r, Epsilon, t, face);
    t -= minTime;
    minTime = 0.0f;
    bool ignoreHit = false;

    // if hit outer wall with ricochet and hit is above top of wall
    // then ignore hit.
    if (!teleporter && building && (e == Reflect) &&
	(building->getType() == WallObstacle::getClassName()) &&
	((o[2] + t * d[2]) > building->getHeight())) {
      ignoreHit = true;
    }

    // construct next shot segment and add it to list
    double endTime(startTime);

    if (t < 0.0f)
      endTime += Epsilon;
     else
      endTime += t;

    ShotPathSegment segm(startTime, endTime, rs, reason);
    segments.push_back(segm);
    startTime = endTime;

    // used up this much time in segment
    if (t < 0.0f) {
      timeLeft -= Epsilon;
    } else {
      timeLeft -= t;
    }

    // check in reverse order to see what we hit first
    reason = ShotPathSegment::Through;
    if (ignoreHit) {
      // uh...ignore this.  usually used if you shoot over the boundary wall.
      // just move the point of origin and build the next segment
      o[0] += t * d[0];
      o[1] += t * d[1];
      o[2] += t * d[2];
      reason = ShotPathSegment::Boundary;
    } else if (teleporter) {
      // entered teleporter -- teleport it
//CHANGEEDIT
//      unsigned int seed = shotPath.getShotId() + i;
//      int source = world->getTeleporter(teleporter, face);
//      int target = world->getTeleportTarget(source, seed);
//
//      int outFace;
//      const Teleporter* outTeleporter = world->getTeleporter(target, outFace);
//      o[0] += t * d[0];
//      o[1] += t * d[1];
//      o[2] += t * d[2];
//      teleporter->getPointWRT(*outTeleporter, face, outFace,
//					    o, d, 0.0f, o, d, NULL);
//ENDCHANGEEDIT
      reason = ShotPathSegment::Teleport;
    }
    else if (building) {
      // hit building -- can bounce off or stop, buildings ignored for Through
      switch (e) {
	case Stop:
	  timeLeft = 0.0f;
	break;

      case Reflect: {
	// move origin to point of reflection
	o[0] += t * d[0];
	o[1] += t * d[1];
	o[2] += t * d[2];

	// reflect direction about normal to building
	float normal[3];
	building->get3DNormal(o, normal);
	reflect(d, normal);
	reason = ShotPathSegment::Ricochet;
	}
	break;

      case Through:
	assert(0);
      }
    }
    else if (hitGround)	// we hit the ground
    {
      switch (e) {
	case Stop:
	case Through:
	  timeLeft = 0.0f;
	  break;

	case Reflect: {
	  // move origin to point of reflection
	  o[0] += t * d[0];
	  o[1] += t * d[1];
	  o[2] += t * d[2];

	  // reflect direction about normal to ground
	  float normal[3];
	  normal[0] = 0.0f;
	  normal[1] = 0.0f;
	  normal[2] = 1.0f;
	  reflect(d, normal);
	  reason = ShotPathSegment::Ricochet;
	  break;
	}
      }
    }
  }
  lastTime = startTime;

  // make bounding box for entire path
  const size_t numSegments = segments.size();
  if (numSegments > 0) {
    const ShotPathSegment& firstSeg = segments[0];
    bbox[0][0] = firstSeg.bbox[0][0];
    bbox[0][1] = firstSeg.bbox[0][1];
    bbox[0][2] = firstSeg.bbox[0][2];
    bbox[1][0] = firstSeg.bbox[1][0];
    bbox[1][1] = firstSeg.bbox[1][1];
    bbox[1][2] = firstSeg.bbox[1][2];
    for (size_t j = 1; j < numSegments; ++j) {
      const ShotPathSegment& segm = segments[j];
      if (bbox[0][0] > segm.bbox[0][0]) bbox[0][0] = segm.bbox[0][0];
      if (bbox[1][0] < segm.bbox[1][0]) bbox[1][0] = segm.bbox[1][0];
      if (bbox[0][1] > segm.bbox[0][1]) bbox[0][1] = segm.bbox[0][1];
      if (bbox[1][1] < segm.bbox[1][1]) bbox[1][1] = segm.bbox[1][1];
      if (bbox[0][2] > segm.bbox[0][2]) bbox[0][2] = segm.bbox[0][2];
      if (bbox[1][2] < segm.bbox[1][2]) bbox[1][2] = segm.bbox[1][2];
    }
  } else {
    bbox[0][0] = bbox[1][0] = 0.0f;
    bbox[0][1] = bbox[1][1] = 0.0f;
    bbox[0][2] = bbox[1][2] = 0.0f;
  }
}

const std::vector<ShotPathSegment>&	SegmentedShotStrategy::getSegments() const
{
  return segments;
}

//
// NormalShotStrategy
//

NormalShotStrategy::NormalShotStrategy(ShotPath* _path) :
  SegmentedShotStrategy(_path)
{
  // make segments
  makeSegments(Stop);
}

NormalShotStrategy::~NormalShotStrategy()
{
  // do nothing
}

//
// RapidFireStrategy
//

RapidFireStrategy::RapidFireStrategy(ShotPath* _path) :
  SegmentedShotStrategy(_path)
{
  // speed up shell and decrease lifetime
  FiringInfo& f = getFiringInfo(_path);
  f.lifetime *= BZDB.eval(StateDatabase::BZDB_RFIREADLIFE);
  float fireAdVel = BZDB.eval(StateDatabase::BZDB_RFIREADVEL);
  f.shot.vel[0] *= fireAdVel;
  f.shot.vel[1] *= fireAdVel;
  f.shot.vel[2] *= fireAdVel;
  setReloadTime(_path->getReloadTime()
		/ BZDB.eval(StateDatabase::BZDB_RFIREADRATE));

  // make segments
  makeSegments(Stop);
}

RapidFireStrategy::~RapidFireStrategy()
{
  // do nothing
}

//
// ThiefStrategy
//

ThiefStrategy::ThiefStrategy(ShotPath *_path) :
  SegmentedShotStrategy(_path),cumTime(0.0f)
{
  // speed up shell and decrease lifetime
  FiringInfo& f = getFiringInfo(_path);
  f.lifetime *= BZDB.eval(StateDatabase::BZDB_THIEFADLIFE);
  float thiefAdVel = BZDB.eval(StateDatabase::BZDB_THIEFADSHOTVEL);
  f.shot.vel[0] *= thiefAdVel;
  f.shot.vel[1] *= thiefAdVel;
  f.shot.vel[2] *= thiefAdVel;
  setReloadTime(_path->getReloadTime()
		/ BZDB.eval(StateDatabase::BZDB_THIEFADRATE));

  // make segments
  makeSegments(Stop);
  setCurrentTime(getLastTime());
  endTime = f.lifetime;

  // make thief scene nodes
  const int numSegments = (const int)(getSegments().size());
  thiefNodes = new LaserSceneNode*[numSegments];

  TextureManager &tm = TextureManager::instance();
  int texture = tm.getTextureID("thief");

  for (int i = 0; i < numSegments; i++) {
    const ShotPathSegment& segm = getSegments()[i];
    const float t = float(segm.end - segm.start);
    const Ray& ray = segm.ray;
    const float* rawdir = ray.getDirection();
    float dir[3];
    dir[0] = t * rawdir[0];
    dir[1] = t * rawdir[1];
    dir[2] = t * rawdir[2];
    thiefNodes[i] = new LaserSceneNode(ray.getOrigin(), dir);
    if (texture >= 0)
      thiefNodes[i]->setTexture(texture);

	if (i == 0)
		thiefNodes[i]->setFirst();

	thiefNodes[i]->setColor(0,1,1);
	thiefNodes[i]->setCenterColor(0,0,0);
  }
  setCurrentSegment(numSegments - 1);
}

ThiefStrategy::~ThiefStrategy()
{
  const size_t numSegments = (getSegments().size());
  for (size_t i = 0; i < numSegments; i++)
    delete thiefNodes[i];
  delete[] thiefNodes;
}

void			ThiefStrategy::update(float dt)
{
  cumTime += dt;
  if (cumTime >= endTime) setExpired();
}

void			ThiefStrategy::addShot(SceneDatabase* scene, bool)
{
  // thief is so fast we always show every segment
  const size_t numSegments = (getSegments().size());
  for (size_t i = 0; i < numSegments; i++)
    scene->addDynamicNode(thiefNodes[i]);
}

bool			ThiefStrategy::isStoppedByHit() const
{
  return false;
}


//
// MachineGunStrategy
//

MachineGunStrategy::MachineGunStrategy(ShotPath* _path) :
  SegmentedShotStrategy(_path)
{
  // speed up shell and decrease lifetime
  FiringInfo& f = getFiringInfo(_path);
  f.lifetime *= BZDB.eval(StateDatabase::BZDB_MGUNADLIFE);
  float mgunAdVel = BZDB.eval(StateDatabase::BZDB_MGUNADVEL);
  f.shot.vel[0] *= mgunAdVel;
  f.shot.vel[1] *= mgunAdVel;
  f.shot.vel[2] *= mgunAdVel;
  setReloadTime(_path->getReloadTime()
		/ BZDB.eval(StateDatabase::BZDB_MGUNADRATE));

  // make segments
  makeSegments(Stop);
}

MachineGunStrategy::~MachineGunStrategy()
{
  // do nothing
}

//
// RicochetStrategy
//

RicochetStrategy::RicochetStrategy(ShotPath* _path) :
  SegmentedShotStrategy(_path)
{
  // make segments that bounce
  makeSegments(Reflect);
}

RicochetStrategy::~RicochetStrategy()
{
  // do nothing
}

//
// SuperBulletStrategy
//

SuperBulletStrategy::SuperBulletStrategy(ShotPath* _path) :
  SegmentedShotStrategy(_path)
{
  // make segments that go through buildings
  makeSegments(Through);
}

SuperBulletStrategy::~SuperBulletStrategy()
{
  // do nothing
}


PhantomBulletStrategy::PhantomBulletStrategy(ShotPath* _path) :
  SegmentedShotStrategy(_path)
{
  // make segments that go through buildings
  makeSegments(Through);
}

PhantomBulletStrategy::~PhantomBulletStrategy()
{
  // do nothing
}

//
// LaserStrategy
//

LaserStrategy::LaserStrategy(ShotPath* _path) :
  SegmentedShotStrategy(_path), cumTime(0.0f)
{
  // speed up shell and decrease lifetime
  FiringInfo& f = getFiringInfo(_path);
  f.lifetime *= BZDB.eval(StateDatabase::BZDB_LASERADLIFE);
  float laserAdVel = BZDB.eval(StateDatabase::BZDB_LASERADVEL);
  f.shot.vel[0] *= laserAdVel;
  f.shot.vel[1] *= laserAdVel;
  f.shot.vel[2] *= laserAdVel;
  setReloadTime(_path->getReloadTime()
		/ BZDB.eval(StateDatabase::BZDB_LASERADRATE));

  // make segments
  makeSegments(Stop);
  setCurrentTime(getLastTime());
  endTime = f.lifetime;

//CLIENTEDIT
//  // make laser scene nodes
//  const int numSegments = (const int)(getSegments().size());
//  laserNodes = new LaserSceneNode*[numSegments];
//  const LocalPlayer* myTank = LocalPlayer::getMyTank();
//  TeamColor tmpTeam = (myTank->getFlag() == Flags::Colorblindness) ? RogueTeam : team;
//
//  TextureManager &tm = TextureManager::instance();
//  std::string imageName = Team::getImagePrefix(tmpTeam);
//  imageName += BZDB.get("laserTexture");
//  int texture = tm.getTextureID(imageName.c_str());
//
//  for (int i = 0; i < numSegments; i++) {
//    const ShotPathSegment& segm = getSegments()[i];
//    const float t = float(segm.end - segm.start);
//    const Ray& ray = segm.ray;
//    const float* rawdir = ray.getDirection();
//    float dir[3];
//    dir[0] = t * rawdir[0];
//    dir[1] = t * rawdir[1];
//    dir[2] = t * rawdir[2];
//    laserNodes[i] = new LaserSceneNode(ray.getOrigin(), dir);
//    if (texture >= 0)
//      laserNodes[i]->setTexture(texture);
//
//	const float *color = Team::getRadarColor(tmpTeam);
//	laserNodes[i]->setColor(color[0],color[1],color[2]);
//
//	if (i == 0)
//		laserNodes[i]->setFirst();
//  }
//  setCurrentSegment(numSegments - 1);
//ENDCLIENTEDIT
}

LaserStrategy::~LaserStrategy()
{
  const size_t numSegments = getSegments().size();
  for (size_t i = 0; i < numSegments; i++)
    delete laserNodes[i];
  delete[] laserNodes;
}

void			LaserStrategy::update(float dt)
{
  cumTime += dt;
  if (cumTime >= endTime) setExpired();
}

void			LaserStrategy::addShot(SceneDatabase* scene, bool)
{
//CLIENTEDIT
//  // laser is so fast we always show every segment
//  const size_t numSegments = getSegments().size();
//  for (size_t i = 0; i < numSegments; i++)
//    scene->addDynamicNode(laserNodes[i]);
//ENDCLIENTEDIT
}

bool			LaserStrategy::isStoppedByHit() const
{
  return false;
}

// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8
