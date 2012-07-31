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

/* bzflag common header */
#include "common.h"

/* system headers */
#include <string>

/* common headers */
#include "Player.h"
#include "Singleton.h"
#include "vectors.h"
#include "game/Flag.h"

/* local headers */
#include "Roster.h"
#include "World.h"

#define ROAM (Roaming::instance())

class Roaming : public Singleton<Roaming> {
  public:
    Roaming(); // c'tor

    enum RoamingView {
      roamViewDisabled = 0,
      roamViewFree,
      roamViewTrack,
      roamViewFollow,
      roamViewFP,
      roamViewFlag,
      roamViewCount
    };
    bool isRoaming(void) const;
    RoamingView getMode(void) const;
    void setMode(RoamingView newView);

    RoamingView parseView(const std::string& view) const;
    const char* getViewName(RoamingView) const;

    enum RoamingTarget {
      next = 0,
      previous,
      explicitSet
    };
    void changeTarget(RoamingTarget target, int explicitIndex = 0);
    /* if view is in any mode in which they are not valid,
       getTargetTank and getTargetFlag will return NULL.  Otherwise
       they return the index of the object that you're
       tracking/following/driving with */
    Player* getTargetTank(void) const;
    Flag*   getTargetFlag(void) const;

    void buildRoamingLabel(void);
    std::string getRoamingLabel(void) const;

    struct RoamingCamera {
      fvec3 pos;
      float theta;
      float phi;
      float zoom;
    };
    void setCamera(const RoamingCamera* newCam);
    void resetCamera(void);
    /* note that dc is a camera structure of *changes* (thus dc)
       not new values */
    void updatePosition(RoamingCamera* dc, float dt);
    const RoamingCamera* getCamera(void) const;
    void setZoom(float newZoom);
    float getZoom(void) const;

  protected:
    friend class Singleton<Roaming>;

  private:
    bool changePlayer(RoamingTarget targetType); // used by changeTarget()

  private:
    RoamingView view;
    RoamingCamera camera;
    int targetManual;
    int targetWinner;
    int targetFlag;
    std::string roamingLabel;
};

inline bool Roaming::isRoaming(void) const {
  return (view > roamViewDisabled);
}

inline Roaming::RoamingView Roaming::getMode(void) const {
  return view;
}

inline float Roaming::getZoom() const {
  return camera.zoom;
}

inline void Roaming::setZoom(float newZoom) {
  camera.zoom = newZoom;
}

inline std::string Roaming::getRoamingLabel(void) const {
  return roamingLabel;
}

inline Player* Roaming::getTargetTank() const {
  if ((getMode() == roamViewFollow)
      || (getMode() == roamViewFP)
      || (getMode() == roamViewTrack)) {
    return getPlayerByIndex(targetWinner);
  }
  else {
    return NULL;
  }
}

inline Flag* Roaming::getTargetFlag() const {
  if (!(getMode() == roamViewFlag)) {
    return NULL;
  }
  World* world = World::getWorld();
  if (!world) {
    return NULL;
  }
  else {
    return &(world->getFlag(targetFlag));
  }
}

inline const Roaming::RoamingCamera* Roaming::getCamera() const {
  return &camera;
}


// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: nil ***
// End: ***
// ex: shiftwidth=2 tabstop=8 expandtab