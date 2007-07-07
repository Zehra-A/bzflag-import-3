#include "RCRequest.h"
#include "Roster.h"

#include "RCLink.h"
#include "RCRequests.h"

#include "RCMessageFactory.h"

bool RCRequest::process(RCRobotPlayer *) { return true; }

int RCRequest::getRobotIndex() const
{
  return robotIndex;
}

int RCRequest::setRobotIndex(char *arg)
{
  char *endptr;
  robotIndex = strtol(arg, &endptr, 0);
  if (endptr == arg)
    robotIndex = -1;
  else if (robotIndex >= numRobots)
    robotIndex = -1;

  return robotIndex;
}
