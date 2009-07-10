/* bzflag
 * Copyright (c) 1993 - 2009 Tim Riker
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
#include "PointShotStrategy.h"

/* system implementation headers */
#include <assert.h>

/* common implementation headers */
#include "BZDBCache.h"
#include "WallObstacle.h"
#include "Intersect.h"
/* local implementation headers */
#include "LocalPlayer.h"
#include "playing.h"


PointShotStrategy::PointShotStrategy(ShotPath* _path)
: ShotStrategy(_path)
{
	doBoxTest = false;
}


PointShotStrategy::~PointShotStrategy()
{
}

float PointShotStrategy::checkShotHit(const ShotCollider& tank, fvec3& position, float radius) const
{
  float minTime = Infinity;
  // expired shot can't hit anything
  if (getPath().isExpired()) 
    return minTime;

  // tank is positioned from it's bottom so shift position up by
  // half a tank height.
  fvec3 lastTankPositionRaw = tank.motion.getOrigin();
  lastTankPositionRaw.z += 0.5f * tank.size.z;
  Ray tankLastMotion(lastTankPositionRaw, tank.motion.getDirection());
  
  const Extents& tankBBox = tank.bbox;

  // if bounding box of tank and entire shot doesn't overlap then no hit
  // we only do this for shots that keep the bbox updated
  if (doBoxTest)
  {
	  if (!bbox.touches(tankBBox))
		  return minTime;
  }

  float shotRadius = radius;

  // check each segment in interval (prevTime,currentTime]
  const float dt = float(currentTime - prevTime);
  const int numSegments = (const int)segments.size();
  for (int i = lastSegment; i <= segment && i < numSegments; i++)
  {
    // can never hit your own first laser segment
    if ((i == 0) && tank.testLastSegment && (getPath().getShotType() == LaserShot))
      continue;
/*
    // skip segments that don't overlap in time with current interval
    if (segments[i].end <= prevTime) continue;
    if (currentTime <= segments[i].start) break;
*/

	const ShotPathSegment& s = segments[i];
	if (doBoxTest)
	{
		// if shot segment and tank bboxes don't overlap then no hit,
		// or if it's a shot that is out of the world boundary
		if (!s.bbox.touches(tankBBox) || (s.reason == ShotPathSegment::Boundary))
		  continue;
	}

    // construct relative shot ray:  origin and velocity relative to
    // my tank as a function of time (t=0 is start of the interval).
    Ray relativeRay(Intersect::rayMinusRay(s.ray, float(prevTime - s.start), tankLastMotion, 0.0f));
	
	static fvec3 tankBase(0.0f, 0.0f, -0.5f * tank.size.z);

    // get hit time
    // find closest approach to narrow box around tank.  width of box is small if we have narrow
	float t = Intersect::timeRayHitsBlock(relativeRay, tankBase, tank.angle, tank.size.x, tank.test2D ? shotRadius : tank.size.y, tank.size.z);

    if (t > minTime)
		continue;

    // make sure time falls within segment
    if ((t < 0.0f) || (t > dt))
		continue;
    if (t > (s.end - prevTime))
		continue;

    // check if shot hits tank -- get position at time t, see if in radius
    fvec3 closestPos;
    relativeRay.getPoint(t, closestPos);
	// save best time so far
	minTime = t;

	// compute location of tank at time of hit
	fvec3 tankPos;
	tank.motion.getPoint(t, tankPos);

	// compute position of intersection
	position = tankPos + closestPos + fvec3(0,0,0.5f * tank.size.z);
	//printf("%u:%u %u:%u\n", tank->getId().port, tank->getId().number, getPath().getPlayer().port, getPath().getPlayer().number);
  }

  return minTime;
}


// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8