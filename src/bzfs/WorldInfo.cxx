/* bzflag
 * Copyright (c) 1993 - 2004 Tim Riker
 *
 * This package is free software;  you can redistribute it and/or
 * modify it under the terms of the license found in the file
 * named COPYING that should have accompanied this file.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* interface header */
#include "WorldInfo.h"

/* system headers */
#include <ctype.h>

/* common implementation headers */
#include "global.h"
#include "Pack.h"
#include "Protocol.h"
#include "Intersect.h"
#include "CollisionManager.h"
#include "DynamicColor.h"
#include "TextureMatrix.h"
#include "BzMaterial.h"
#include "PhysicsDriver.h"
#include "MeshTransform.h"

#include "ObstacleMgr.h"
#include "Obstacle.h"
#include "BoxBuilding.h"
#include "PyramidBuilding.h"
#include "BaseBuilding.h"
#include "TetraBuilding.h"
#include "Teleporter.h"
#include "WallObstacle.h"
#include "MeshObstacle.h"
#include "ArcObstacle.h"
#include "ConeObstacle.h"
#include "SphereObstacle.h"


/* compression library header */
#include "../zlib/zlib.h"


WorldInfo::WorldInfo() :
  maxHeight(0.0f),
  database(NULL)
{
  size[0] = 400.0f;
  size[1] = 400.0f;
  gravity = -9.81f;
  waterLevel = -1.0f;
  waterMatRef = NULL;
}

WorldInfo::~WorldInfo()
{
  delete[] database;
  OBSTACLEMGR.clear();
}


void WorldInfo::addWall(float x, float y, float z, float r, float w, float h)
{
  const float pos[3] = {x, y, z};
  WallObstacle* wall = new WallObstacle(pos, r, w, h);
  OBSTACLEMGR.addWorldObstacle(wall);
}

void WorldInfo::addLink(int from, int to)
{
  // discard links to/from teleporters that don't exist
  // note that -1 "to" means the client picks one at random
  const ObstacleList& teles = OBSTACLEMGR.getTeles();
  if ((unsigned(from) >= (teles.size() * 2)) ||
     ((unsigned(to) >= (teles.size() * 2)) && (to != -1))) {
    DEBUG1("Warning: bad teleporter link dropped from=%d to=%d\n", from, to);
  } else {
    setTeleporterTarget (from, to);
  }
}


int WorldInfo::findTeleFaceByName(const std::string& name)
{
  if ((name == "random") || (name == "-1")) {
    return -1;
  }
  
  if ((name[0] >= '0') && (name[0] <= '9')) {
    // old-style numeric value
    return atoi(name.c_str());
  }

  if ((name.size() < 3) || (name[name.size() - 2] != ':')) {
    return -2;
  }

  char lastChar = tolower(name[name.size() - 1]);
  int face;
  if (lastChar == 'f') {
    face = 0;
  } else if (lastChar == 'b') {
    face = 1;
  } else {
    return -2;
  }
    
  std::string shortName = name;
  shortName.resize(shortName.size() - 2);
  const ObstacleList& teles = OBSTACLEMGR.getTeles();
  for (unsigned int i = 0; i < teles.size(); i++) {
    const Teleporter* tele = (const Teleporter*) teles[i];
    if (shortName == tele->getName()) {
      return ((int)(i * 2) + face);
    }
  }

  return -2;
}

void WorldInfo::addLink(const std::string& from, const std::string& to)
{
  int fromFace, toFace;
  fromFace = findTeleFaceByName(from);
  toFace = findTeleFaceByName(to);
  // discard links to/from teleporters that don't exist
  // note that -1 "to" means the client picks one at random
  const ObstacleList& teles = OBSTACLEMGR.getTeles();
  if ((unsigned(fromFace) >= (teles.size() * 2)) ||
     ((unsigned(toFace) >= (teles.size() * 2)) && (toFace != -1))) {
    DEBUG1("Warning: bad teleporter link dropped:  "
           "from = \"%s\", to = \"%s\"\n", from.c_str(), to.c_str());
  } else {
    setTeleporterTarget (fromFace, toFace);
    DEBUG4("linking %i to %i  (%i,%i  to  %i,%i)\n", fromFace, toFace,
           (fromFace / 2), (fromFace % 2), (toFace / 2), (toFace % 2));
  }
}


void WorldInfo::addZone(const CustomZone *zone)
{
  entryZones.addZone( zone );
}

void WorldInfo::addWeapon(const FlagType *type, const float *origin, float direction,
			  float initdelay, const std::vector<float> &delay, TimeKeeper &sync)
{
  worldWeapons.add(type, origin, direction, initdelay, delay, sync);
}

void WorldInfo::addWaterLevel (float level, const BzMaterial* matref)
{
  waterLevel = level;
  waterMatRef = matref;
}

void WorldInfo::addBox(float x, float y, float z, float r,
		       float w, float d, float h, bool drive, bool shoot)
{
  const float pos[3] = {x, y, z};
  BoxBuilding* box = new BoxBuilding(pos, r, w, d, h, drive, shoot, false);
  OBSTACLEMGR.addWorldObstacle(box);
}

void WorldInfo::addPyramid(float x, float y, float z, float r,
			   float w, float d, float h,
			   bool drive, bool shoot, bool flipZ)
{
  const float pos[3] = {x, y, z};
  PyramidBuilding* pyr = new PyramidBuilding(pos, r, w, d, h, drive, shoot);
  if (flipZ) {
    pyr->setZFlip();
  }
  OBSTACLEMGR.addWorldObstacle(pyr);
}

void WorldInfo::addTeleporter(float x, float y, float z, float r,
			      float w, float d, float h, float b,
			      bool horizontal, bool drive, bool shoot)
{
  const float pos[3] = {x, y, z};
  Teleporter* tele = new Teleporter(pos, r, w, d, h, b, horizontal, drive, shoot);
  OBSTACLEMGR.addWorldObstacle(tele);

  // default to passthru linkage
  const ObstacleList& teles = OBSTACLEMGR.getTeles();
  int index = teles.size() - 1;
  setTeleporterTarget ((index * 2) + 1, (index * 2));
  setTeleporterTarget ((index * 2), (index * 2) + 1);
}

void WorldInfo::addBase(float x, float y, float z, float r,
			float w, float d, float h, int color,
			bool /* drive */, bool /* shoot */)
{
  const float pos[3] = {x, y, z};
  const float size[3] = {w, d, h};
  BaseBuilding* base = new BaseBuilding(pos, r, size, color);
  OBSTACLEMGR.addWorldObstacle(base);
  //bases.push_back (base);
}


void WorldInfo::makeWaterMaterial()
{
  // the texture matrix
  TextureMatrix* texmat = new TextureMatrix;
  texmat->setName("defaultWaterLevel");
  texmat->setShiftParams(0.05f, 0.0f);
  int texmatIndex = TEXMATRIXMGR.addMatrix(texmat);

  // the material
  BzMaterial material;
  const float diffuse[4] = {0.65f, 1.0f, 0.5f, 0.9f};
  material.reset();
  material.setName("defaultWaterLevel");
  material.setTexture("water");
  material.setTextureMatrix(texmatIndex); // generate a default later
  material.setDiffuse(diffuse);
  material.setUseTextureAlpha(true); // make sure that alpha is enabled
  material.setUseColorOnTexture(false); // only use the color as a backup
  material.setUseSphereMap(false);
  waterMatRef = MATERIALMGR.addMaterial(&material);

  return;
}

float WorldInfo::getWaterLevel() const
{
  return waterLevel;
}

float WorldInfo::getMaxWorldHeight() const
{
  return maxHeight;
}

void WorldInfo::setTeleporterTarget(int src, int tgt)
{
  if ((int)teleportTargets.size() < (src + 1)) {
    teleportTargets.resize(((src / 2) + 1) * 2);
  }

  // record target in source entry
  teleportTargets[src] = tgt;
}

WorldWeapons& WorldInfo::getWorldWeapons()
{
  return worldWeapons;
}

void		    WorldInfo::loadCollisionManager()
{
  COLLISIONMGR.load();
  return;
}

void		    WorldInfo::checkCollisionManager()
{
  if (COLLISIONMGR.needReload()) {
    // reload the collision grid
    COLLISIONMGR.load();
  }
  return;
}

bool WorldInfo::rectHitCirc(float dx, float dy, const float *p, float r) const
{
  // Algorithm from Graphics Gems, pp51-53.
  const float rr = r * r, rx = -p[0], ry = -p[1];
  if (rx + dx < 0.0f) // west of rect
    if (ry + dy < 0.0f) //  sw corner
      return (rx + dx) * (rx + dx) + (ry + dy) * (ry + dy) < rr;
    else if (ry - dy > 0.0f) //  nw corner
      return (rx + dx) * (rx + dx) + (ry - dy) * (ry - dy) < rr;
    else //  due west
      return rx + dx > -r;

  else if (rx - dx > 0.0f) // east of rect
    if (ry + dy < 0.0f) //  se corner
      return (rx - dx) * (rx - dx) + (ry + dy) * (ry + dy) < rr;
    else if (ry - dy > 0.0f) //  ne corner
      return (rx - dx) * (rx - dx) + (ry - dy) * (ry - dy) < rr;
    else //  due east
      return rx - dx < r;

  else if (ry + dy < 0.0f) // due south
    return ry + dy > -r;

  else if (ry - dy > 0.0f) // due north
    return ry - dy < r;

  // circle origin in rect
  return true;
}

bool WorldInfo::inRect(const float *p1, float angle, const float *size,
		       float x, float y, float r) const
{
  // translate origin
  float pa[2];
  pa[0] = x - p1[0];
  pa[1] = y - p1[1];

  // rotate
  float pb[2];
  const float c = cosf(-angle), s = sinf(-angle);
  pb[0] = c * pa[0] - s * pa[1];
  pb[1] = c * pa[1] + s * pa[0];

  // do test
  return rectHitCirc(size[0], size[1], pb, r);
}


InBuildingType WorldInfo::inCylinderNoOctree(Obstacle **location,
					     float x, float y, float z,
					     float radius, float height) const
{
  if (height < Epsilon) {
    height = Epsilon;
  }

  float pos[3] = {x, y, z};

  for (int type = 0; type < GroupDefinition::ObstacleTypeCount; type++) {
    const ObstacleList& list = OBSTACLEMGR.getWorld()->getList(type);
    for (unsigned int i = 0; i < list.size(); i++) {
      Obstacle* obs = list[i];
      if (obs->inCylinder(pos, radius, height)) {
	if (location != NULL) {
	  *location = obs;
	}
	return classifyHit(obs);
      }
    }
  }

  if (location != NULL) {
    *location = (Obstacle *)NULL;
  }

  return NOT_IN_BUILDING;
}


InBuildingType WorldInfo::cylinderInBuilding(const Obstacle **location,
					     const float* pos, float radius,
					     float height) const
{
  if (height < Epsilon) {
    height = Epsilon;
  }

  *location = NULL;

  // check everything but walls
  const ObsList* olist = COLLISIONMGR.cylinderTest (pos, radius, height);
  for (int i = 0; i < olist->count; i++) {
    const Obstacle* obs = olist->list[i];
    if (obs->inCylinder(pos, radius, height)) {
      *location = obs;
      break;
    }
  }

  return classifyHit (*location);
}


InBuildingType WorldInfo::cylinderInBuilding(const Obstacle **location,
					     float x, float y, float z, float radius,
					     float height) const
{
  const float pos[3] = {x, y, z};
  return cylinderInBuilding (location, pos, radius, height);
}


InBuildingType WorldInfo::boxInBuilding(const Obstacle **location,
					const float* pos, float angle,
					float width, float breadth, float height) const
{
  if (height < Epsilon) {
    height = Epsilon;
  }

  *location = NULL;

  // check everything but walls
  const ObsList* olist =
    COLLISIONMGR.boxTest (pos, angle, width, breadth, height);
  for (int i = 0; i < olist->count; i++) {
    const Obstacle* obs = olist->list[i];
    if (obs->inBox(pos, angle, width, breadth, height)) {
      *location = obs;
      break;
    }
  }

  return classifyHit (*location);
}


InBuildingType WorldInfo::classifyHit (const Obstacle* obstacle) const
{
  if (obstacle == NULL) {
    return NOT_IN_BUILDING;
  }
  else if (obstacle->getType() == BoxBuilding::getClassName()) {
    if (obstacle->isDriveThrough()) {
      return IN_BOX_DRIVETHROUGH;
    }
    else {
      return IN_BOX_NOTDRIVETHROUGH;
    }
  }
  else if (obstacle->getType() == PyramidBuilding::getClassName()) {
    return IN_PYRAMID;
  }
  else if (obstacle->getType() == TetraBuilding::getClassName()) {
    return IN_TETRA;
  }
  else if (obstacle->getType() == MeshObstacle::getClassName()) {
    return IN_MESH;
  }
  else if (obstacle->getType() == MeshFace::getClassName()) {
    return IN_MESHFACE;
  }
  else if (obstacle->getType() == BaseBuilding::getClassName()) {
    return IN_BASE;
  }
  else if (obstacle->getType() == Teleporter::getClassName()) {
    return IN_TELEPORTER;
  }
  else {
    // FIXME - choke here?
    printf ("*** Unknown obstacle type in WorldInfo::boxInBuilding()\n");
    return IN_BASE;
  }
}


bool WorldInfo::getZonePoint(const std::string &qualifier, float *pt) const
{
  const Obstacle* loc;
  InBuildingType type;

  if (!entryZones.getZonePoint(qualifier, pt))
    return false;

  type = cylinderInBuilding(&loc, pt[0], pt[1], 0.0f, 1.0f, pt[2]);
  if (type == NOT_IN_BUILDING)
    pt[2] = 0.0f;
  else
    pt[2] = loc->getPosition()[2] + loc->getSize()[2];
  return true;
}


bool WorldInfo::getSafetyPoint(const std::string &qualifier,
			       const float *pos, float *pt) const
{
  const Obstacle *loc;
  InBuildingType type;

  if (!entryZones.getSafetyPoint(qualifier, pos, pt))
    return false;

  type = cylinderInBuilding(&loc, pt[0], pt[1], 0.0f, 1.0f, pt[2]);
  if (type == NOT_IN_BUILDING)
    pt[2] = 0.0f;
  else
    pt[2] = loc->getPosition()[2] + loc->getSize()[2];
  return true;
}


void WorldInfo::finishWorld()
{
  entryZones.calculateQualifierLists();

  loadCollisionManager();

  maxHeight = COLLISIONMGR.getMaxWorldHeight();
  const float wallHeight = BZDB.eval(StateDatabase::BZDB_WALLHEIGHT);
  if (maxHeight < wallHeight) {
    maxHeight = wallHeight;
  }
  if (maxHeight < 0.0f) {
    maxHeight = 0.0f;
  }

  return;
}


int WorldInfo::packDatabase()
{
  unsigned int i;
  const ObstacleList& teles = OBSTACLEMGR.getTeles();

  // make default water material. we wait to make the default material
  // to avoid messing up any user indexing. this has to be done before
  // the texture matrices and materials are packed.
  if ((waterLevel >= 0.0f) && (waterMatRef == NULL)) {
    makeWaterMaterial();
  }

  // compute the database size
  databaseSize =
    DYNCOLORMGR.packSize() + TEXMATRIXMGR.packSize() +
    MATERIALMGR.packSize() + PHYDRVMGR.packSize() +
    TRANSFORMMGR.packSize() + OBSTACLEMGR.packSize() +
    worldWeapons.packSize() + entryZones.packSize() +
    (sizeof(uint32_t) + (WorldCodeLinkSize * 2 * teles.size()));
  // add water level size
  databaseSize += sizeof(float);
  if (waterLevel >= 0.0f) {
    databaseSize += sizeof(int);
  }


  // allocate the buffer
  database = new char[databaseSize];
  void *databasePtr = database;


  // pack dynamic colors
  databasePtr = DYNCOLORMGR.pack(databasePtr);

  // pack texture matrices
  databasePtr = TEXMATRIXMGR.pack(databasePtr);

  // pack materials
  databasePtr = MATERIALMGR.pack(databasePtr);

  // pack physics drivers
  databasePtr = PHYDRVMGR.pack(databasePtr);

  // pack obstacle transforms
  databasePtr = TRANSFORMMGR.pack(databasePtr);

  // pack obstacles
  databasePtr = OBSTACLEMGR.pack(databasePtr);

  // pack water level
  databasePtr = nboPackFloat(databasePtr, waterLevel);
  if (waterLevel >= 0.0f) {
    int matindex = MATERIALMGR.getIndex(waterMatRef);
    databasePtr = nboPackInt(databasePtr, matindex);
  }

  // pack weapons
  databasePtr = worldWeapons.pack(databasePtr);

  // pack entry zones
  databasePtr = entryZones.pack(databasePtr);

  // pack teleporter links
  uint32_t linkCount = 2 * teles.size();
  databasePtr = nboPackUInt(databasePtr, linkCount);
  for (i = 0; i < teles.size(); i++) {
    // pack even link
    databasePtr = nboPackUShort(databasePtr, uint16_t(i * 2));
    databasePtr = nboPackUShort(databasePtr, uint16_t(teleportTargets[i * 2]));
    // pack odd link
    databasePtr = nboPackUShort(databasePtr, uint16_t(i * 2 + 1));
    databasePtr = nboPackUShort(databasePtr, uint16_t(teleportTargets[i * 2 + 1]));
  }


  // compress the map database
  uLongf gzDBlen = databaseSize + (databaseSize/512) + 12;
  char* gzDB = new char[gzDBlen];
  int code = compress2 ((Bytef*)gzDB, &gzDBlen,
			(Bytef*)database, databaseSize, 9);
  if (code != Z_OK) {
    printf ("Could not create compressed world database: %i\n", code);
    exit (1);
  }

  // switch to the compressed map database
  uncompressedSize = databaseSize;
  databaseSize = gzDBlen;
  char *oldDB = database;
  database = gzDB;
  delete[] oldDB;

  DEBUG1 ("Map size: uncompressed = %i, compressed = %i\n",
	   uncompressedSize, databaseSize);

  return 1;
}

void *WorldInfo::getDatabase() const
{
  return database;
}

int WorldInfo::getDatabaseSize() const
{
  return databaseSize;
}

int WorldInfo::getUncompressedSize() const
{
  return uncompressedSize;
}

// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8
