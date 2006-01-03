/* bzflag
 * Copyright (c) 1993 - 2006 Tim Riker
 *
 * This package is free software;  you can redistribute it and/or
 * modify it under the terms of the license found in the file
 * named LICENSE that should have accompanied this file.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* interface header */
#include "NetHandler.h"

// system headers
#include <errno.h>

const int udpBufSize = 128000;

bool NetHandler::pendingUDP = false;
TimeKeeper NetHandler::now = TimeKeeper::getCurrent();
std::list<NetHandler*> NetHandler::netConnections;

bool NetHandler::initHandlers(struct sockaddr_in addr) {
  // udp socket
  int n;
  // we open a udp socket on the same port if alsoUDP
  if ((udpSocket = (int) socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      nerror("couldn't make udp connect socket");
      return false;
  }

  // increase send/rcv buffer size
  n = setsockopt(udpSocket, SOL_SOCKET, SO_SNDBUF, (SSOType) &udpBufSize,
		 sizeof(int));
  if (n < 0) {
    nerror("couldn't increase udp send buffer size");
    close(udpSocket);
    return false;
  }
  n = setsockopt(udpSocket, SOL_SOCKET, SO_RCVBUF, (SSOType) &udpBufSize,
		 sizeof(int));
  if (n < 0) {
      nerror("couldn't increase udp receive buffer size");
      close(udpSocket);
      return false;
  }
  if (bind(udpSocket, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
      nerror("couldn't bind udp listen port");
      close(udpSocket);
      return false;
  }
  // don't buffer info, send it immediately
  BzfNetwork::setNonBlocking(udpSocket);

  return true;
}

void NetHandler::setFd(fd_set *read_set, fd_set *write_set, int &maxFile) {
  std::list<NetHandler*>::const_iterator it;
  for (it = netConnections.begin(); it != netConnections.end(); it++) {
    NetHandler *player = *it;
    if (!player->closed) {
#if !defined(USE_THREADS) || !defined(HAVE_SDL)
      FD_SET((unsigned int)player->fd, read_set);
#endif
      if (player->outmsgSize > 0) {
	FD_SET((unsigned int)player->fd, write_set);
      }
      if (player->fd > maxFile) {
	maxFile = player->fd;
      }
    }
  }

  FD_SET((unsigned int)udpSocket, read_set);
  if (udpSocket > maxFile) {
    maxFile = udpSocket;
  }

  for (it = netConnections.begin(); it != netConnections.end(); it++) {
    NetHandler *player = *it;
    player->ares.setFd(read_set, write_set, maxFile);
  }
}

int NetHandler::getUdpSocket() {
  return udpSocket;
}

char NetHandler::udpmsg[MaxPacketLen];
int  NetHandler::udpLen  = 0;
int  NetHandler::udpRead = 0;

struct sockaddr_in NetHandler::lastUDPRxaddr;

int NetHandler::udpReceive(char *buffer, struct sockaddr_in *uaddr,
			   NetHandler **netHandler) {
  AddrLen recvlen = sizeof(*uaddr);
  uint16_t len;
  uint16_t code;

  *netHandler = NULL;

  if (udpLen == udpRead) {
    udpRead = 0;
    udpLen  = recvfrom(udpSocket, udpmsg, MaxPacketLen, 0,
		       (struct sockaddr *) &lastUDPRxaddr, &recvlen);
    // Error receiving data (or no data)
    if (udpLen < 0)
      return -1;
    DEBUG4("uread() len %d from %s:%d on %i\n",
	   udpLen,
	   inet_ntoa(lastUDPRxaddr.sin_addr),
	   ntohs(lastUDPRxaddr.sin_port),
	   udpSocket);
  }
  if ((udpLen - udpRead) < 4) {
    // No space for header :-( 
    udpLen  = 0;
    udpRead = 0;
    return -1;
  }

  // read head
  void *buf = udpmsg + udpRead;
  buf = nboUnpackUShort(buf, len);
  buf = nboUnpackUShort(buf, code);
  if ((udpLen - udpRead) < len + 4) {
    // No space for data :-( 
    udpLen  = 0;
    udpRead = 0;
    return -1;
  }
  // copy the whole bunch of data
  memcpy(buffer, udpmsg + udpRead, len + 4);
  udpRead += len + 4;
  // copy the source identification
  memcpy(uaddr, &lastUDPRxaddr, recvlen);

  if (len == 2 && code == MsgPingCodeRequest)
    // Ping code request
    return 0;

  std::list<NetHandler*>::const_iterator it;

  for (it = netConnections.begin(); it != netConnections.end(); it++)
    if ((*it)->isMyUdpAddrPort(*uaddr, true)) {
      *netHandler = *it;
      break;
    }

  if (!*netHandler) {
    if ((len == 1) && (code == MsgUDPLinkRequest)) {
      return 0;
    } 
    // no match, discard packet
    DEBUG2("uread() discard packet! %s:%d choices p(l) h:p",
	   inet_ntoa(uaddr->sin_addr), ntohs(uaddr->sin_port));
    for (it = netConnections.begin(); it != netConnections.end(); it++)
      if (!(*it)->closed) {
	DEBUG3("(%d-%d) %s:%d", (*it)->udpin,
	       (*it)->udpout,
	       inet_ntoa((*it)->uaddr.sin_addr),
	       ntohs((*it)->uaddr.sin_port));
    }
    DEBUG2("\n");
    return -1;
  }
#ifdef NETWORK_STATS
  (*netHandler)->countMessage(code, len, 0);
#endif
  if (code == MsgUDPLinkEstablished) {
    (*netHandler)->udpout = true;
  }
  return 0;
}

bool NetHandler::isUdpFdSet(fd_set *read_set) {
  if (FD_ISSET(udpSocket, read_set)) {
    return true;
  }
  return false;
}

void NetHandler::checkDNS(fd_set *read_set, fd_set *write_set) {
  std::list<NetHandler*>::const_iterator it;
  for (it = netConnections.begin(); it != netConnections.end(); it++)
    (*it)->ares.process(read_set, write_set);
}

int NetHandler::udpSocket = -1;

NetHandler::NetHandler(const struct sockaddr_in &clientAddr, int _fd)
  : fd(_fd), clientType(clientNone), tcplen(0), closed(false),
    outmsgOffset(0), outmsgSize(0), outmsgCapacity(0), outmsg(NULL),
    udpOutputLen(0), udpin(false), udpout(false), toBeKicked(false) {
  // store address information for player
  AddrLen addr_len = sizeof(clientAddr);
  memcpy(&uaddr, &clientAddr, addr_len);
  peer = Address(uaddr);

#ifdef NETWORK_STATS

  messageExchanged = false;

  // update player state
  time = now;

  // initialize the inbound/outbound counters to zero
  msgBytes[0] = 0;
  perSecondTime[0] = time;
  perSecondCurrentMsg[0] = 0;
  perSecondMaxMsg[0] = 0;
  perSecondCurrentBytes[0] = 0;
  perSecondMaxBytes[0] = 0;

  msgBytes[1] = 0;
  perSecondTime[1] = time;
  perSecondCurrentMsg[1] = 0;
  perSecondMaxMsg[1] = 0;
  perSecondCurrentBytes[1] = 0;
  perSecondMaxBytes[1] = 0;

#endif
  netConnections.push_back(this);
  ares.queryHostname((struct sockaddr *) &clientAddr);
}

NetHandler::~NetHandler() {
#ifdef NETWORK_STATS
  dumpMessageStats();
#endif
  // shutdown TCP socket
  shutdown(fd, 2);
  close(fd);

  delete[] outmsg;

  netConnections.remove(this);
}

bool NetHandler::isFdSet(fd_set *set) {
  if (FD_ISSET(fd, set)) {
    return true;
  }
  return false;
}

int NetHandler::send(const void *buffer, size_t length) {

  int n = ::send(fd, (const char *)buffer, (int)length, 0);
  if (n >= 0)
    return n;

  // get error code
  const int err = getErrno();

  // if socket is closed then give up
  if (err == ECONNRESET || err == EPIPE) {
    return -1;
  }

  // just try again later if it's one of these errors
  if (err != EAGAIN && err != EINTR) {
    // dump other errors and remove the player
    toBeKicked = true;
    if (err != EAGAIN ) {
      nerror("error on write EAGAIN");
      toBeKickedReason = "Write error EAGAIN";
    }
    else {
      nerror("error on write EINTR");
      toBeKickedReason = "Write error EINTR";
    }
  }
  return 0;
}

int NetHandler::bufferedSend(const void *buffer, size_t length) {
  // try flushing buffered data
  if (outmsgSize != 0) {
    const int n = send(outmsg + outmsgOffset, outmsgSize);
    if (n == -1) {
      netConnections.remove(this);
      return -1;
    }
    if (n > 0) {
      outmsgOffset += n;
      outmsgSize   -= n;
    }
  }
  // if the buffer is empty try writing the data immediately
  if ((outmsgSize == 0) && length > 0) {
    const int n = send(buffer, length);
    if (n == -1) {
      netConnections.remove(this);
      return -1;
    }
    if (n > 0) {
      buffer  = (void*)(((const char*)buffer) + n);
      length -= n;
    }
  }
  // write leftover data to the buffer
  if (length > 0) {
    // is there enough room in buffer?
    if (outmsgCapacity < outmsgSize + (int)length) {
      // double capacity until it's big enough
      int newCapacity = (outmsgCapacity == 0) ? 512 : outmsgCapacity;
      while (newCapacity < outmsgSize + (int)length)
	newCapacity <<= 1;

      // if the buffer is getting too big then drop the player.  chances
      // are the network is down or too unreliable to that player.
      // FIXME -- is 20kB too big?  too small?
      if (newCapacity >= 20 * 1024) {
	netConnections.remove(this);
	return -2;
      }

      // allocate memory
      char *newbuf = new char[newCapacity];

      // copy old data over
      memmove(newbuf, outmsg + outmsgOffset, outmsgSize);

      // cutover
      delete[] outmsg;
      outmsg	       = newbuf;
      outmsgOffset   = 0;
      outmsgCapacity = newCapacity;
    }

    // if we can't fit new data at the end of the buffer then move existing
    // data to head of buffer
    // FIXME -- use a ring buffer to avoid moving memory
    if (outmsgOffset + outmsgSize + (int)length > outmsgCapacity) {
      memmove(outmsg, outmsg + outmsgOffset, outmsgSize);
      outmsgOffset = 0;
    }

    // append data
    memmove(outmsg + outmsgOffset + outmsgSize, buffer, length);
    outmsgSize += (int)length;
  }
  return 0;
}

void NetHandler::closing()
{
  closed = true;
}

int NetHandler::pwrite(const void *b, int l) {

  if (l == 0) {
    return 0;
  }

  if (closed)
    return 0;

  void *buf = (void *)b;
  uint16_t len, code;
  buf = nboUnpackUShort(buf, len);
  buf = nboUnpackUShort(buf, code);
#ifdef NETWORK_STATS
  countMessage(code, len, 1);
#endif

  // Check if UDP Link is used instead of TCP, if so jump into udpSend
  if (udpout) {
    // only send bulk messages by UDP
    switch (code) {
    case MsgShotBegin:
    case MsgShotEnd:
    case MsgPlayerUpdate:
    case MsgPlayerUpdateSmall:
    case MsgGMUpdate:
    case MsgLagPing:
    case MsgGameTime:
      udpSend(b, l);
      return 0;
    }
  }

  // always sent MsgUDPLinkRequest over udp with udpSend
  if (code == MsgUDPLinkRequest) {
    udpSend(b, l);
    return 0;
  }

  return bufferedSend(b, l);
}

int NetHandler::pflush(fd_set *set) {
  if (FD_ISSET(fd, set))
    return bufferedSend(NULL, 0);
  else
    return 0;
}

RxStatus NetHandler::tcpReceive() {
  // read more data into player's message buffer
#if defined(USE_THREADS) && defined(HAVE_SDL)
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET((unsigned int)fd, &read_set);
  select(fd + 1, (fd_set*)&read_set, 0, 0, 0);
#endif

  // read header if we don't have it yet
  RxStatus e = receive(4);
  if (e != ReadAll)
    // if header not ready yet then skip the read of the body
    return e;

  // read body if we don't have it yet
  uint16_t len, code;
  void *buf = tcpmsg;
  buf = nboUnpackUShort(buf, len);
  buf = nboUnpackUShort(buf, code);
  if (len > MaxPacketLen) {
    netConnections.remove(this);
    return ReadHuge;
  }
  e = receive(4 + (int) len);
  if (e != ReadAll)
    // if body not ready yet then skip the command handling
    return e;

  // clear out message
  tcplen = 0;
#ifdef NETWORK_STATS
  countMessage(code, len, 0);
#endif
  if (code == MsgUDPLinkEstablished) {
    udpout = true;
  }
  return ReadAll;
}

RxStatus NetHandler::receive(size_t length) {
  RxStatus returnValue;
  if ((int)length <= tcplen)
    return ReadAll;
  int size = recv(fd, tcpmsg + tcplen, (int)length - tcplen, 0);
  if (size > 0) {
    tcplen += size;
    if (tcplen == (int)length)
      returnValue = ReadAll;
    else
      returnValue = ReadPart;
  } else if (size < 0) {
    // handle errors
    // get error code
    const int err = getErrno();

    // ignore if it's one of these errors
    if (err == EAGAIN || err == EINTR)
      returnValue = ReadPart;
    else if (err == ECONNRESET || err == EPIPE) {
      // if socket is closed then give up
      netConnections.remove(this);
      returnValue = ReadReset;
    } else {
      netConnections.remove(this);
      returnValue = ReadError;
    }
  } else { // if (size == 0)
    netConnections.remove(this);
    returnValue = ReadDiscon;
  }
  return returnValue;
}

void *NetHandler::getTcpBuffer() {
  return tcpmsg;
}

void NetHandler::flushUDP()
{
  if (udpOutputLen) {
    sendto(udpSocket, udpOutputBuffer, udpOutputLen, 0,
	   (struct sockaddr*)&uaddr, sizeof(uaddr));
    udpOutputLen = 0;
  }
}

void NetHandler::flushAllUDP() {
  std::list<NetHandler*>::const_iterator it;
  for (it = netConnections.begin(); it != netConnections.end(); it++)
    if (!(*it)->closed)
      (*it)->flushUDP();
  pendingUDP = false;
}

std::string NetHandler::reasonToKick() {
  std::string reason;
  if (toBeKicked) {
    reason = toBeKickedReason;
  }
  toBeKicked = false;
  return reason;
}

#ifdef NETWORK_STATS
void NetHandler::countMessage(uint16_t code, int len, int direction) {

  messageExchanged = true;

  // add length of type and length
  len += 4;

  msgBytes[direction] += len;

  // see if we've received a message of this type yet for this direction
  MessageCountMap::iterator i = msg[direction].find(code);

  if (i == msg[direction].end()) {
    // if not found, initialize stats
    struct MessageCount message;

    message.count = 1;
    message.maxSize = len;
    msg[direction][code] = message;    // struct copy

  } else {

    i->second.count++;
    if (i->second.maxSize < len) {
      i->second.maxSize = len;
    }
  }

  if (now - perSecondTime[direction] < 1.0f) {
    perSecondCurrentMsg[direction]++;
    perSecondCurrentBytes[direction] += len;
  } else {
    perSecondTime[direction] = now;
    if (perSecondMaxMsg[direction] < perSecondCurrentMsg[direction])
      perSecondMaxMsg[direction] = perSecondCurrentMsg[direction];
    if (perSecondMaxBytes[direction] < perSecondCurrentBytes[direction])
      perSecondMaxBytes[direction] = perSecondCurrentBytes[direction];
    perSecondCurrentMsg[direction] = 0;
    perSecondCurrentBytes[direction] = 0;
  }
}

void NetHandler::dumpMessageStats() {
  int total;
  int direction;

  if (!messageExchanged)
    return;

  DEBUG1("Player connect time: %f\n", now - time);

  for (direction = 0; direction <= 1; direction++) {
    total = 0;
    DEBUG1("Player messages %s:", direction ? "out" : "in");

    for (MessageCountMap::iterator i = msg[direction].begin();
	 i != msg[direction].end(); i++) {
      DEBUG1(" %c%c:%u(%u)", i->first >> 8, i->first & 0xff,
	     i->second.count, i->second.maxSize);
      total += i->second.count;
    }

    DEBUG1(" total:%u(%u) ", total, msgBytes[direction]);
    DEBUG1("max msgs/bytes per second: %u/%u\n",
	perSecondMaxMsg[direction],
	perSecondMaxBytes[direction]);
  }
  fflush(stdout);
}
#endif

void NetHandler::udpSend(const void *b, size_t l) {
#ifdef TESTLINK
  if ((random()%LINKQUALITY) == 0) {
    DEBUG1("Drop Packet due to Test\n");
    return;
  }
#endif

  // setting sizeLimit to -1 will disable udp-buffering
  const int sizeLimit = (int)MaxPacketLen - 4;

  // If the new data does not fit into the buffer, send the buffer
  if (udpOutputLen && (udpOutputLen + l > (int)MaxPacketLen)) {
    sendto(udpSocket, udpOutputBuffer, udpOutputLen, 0,
	   (struct sockaddr*)&uaddr, sizeof(uaddr));
    udpOutputLen = 0;
  }
  // If nothing is buffered and new data will mostly fill it, send
  // without copying
  if (!udpOutputLen && ((int)l > sizeLimit)) {
    sendto(udpSocket, (const char *)b, (int)l, 0, (struct sockaddr*)&uaddr,
	   sizeof(uaddr));
  } else {
    // Buffer new data
    memcpy(&udpOutputBuffer[udpOutputLen], (const char *)b, l);
    udpOutputLen += (int)l;
    // Send buffer if is almost full
    if (udpOutputLen > sizeLimit) {
      sendto(udpSocket, udpOutputBuffer, udpOutputLen, 0,
	     (struct sockaddr*)&uaddr, sizeof(uaddr));
      udpOutputLen = 0;
    }
  }
  if (udpOutputLen)
    pendingUDP = true;
}

bool NetHandler::isMyUdpAddrPort(struct sockaddr_in _uaddr,
				 bool checkPort) {
  if (closed)
    return false;

  if (checkPort != udpin)
    return false;

  if (memcmp(&uaddr.sin_addr, &_uaddr.sin_addr, sizeof(uaddr.sin_addr)))
    return false;

  if (!checkPort)
    return true;

  if (uaddr.sin_port == _uaddr.sin_port)
    return true;

  return false;
}

void NetHandler::getPlayerList(char *list) {
  sprintf(list, "%s%s%s%s%s%s",
	  peer.getDotNotation().c_str(),
	  getHostname() ? " (" : "",
	  getHostname() ? getHostname() : "",
	  getHostname() ? ")" : "",
	  udpin ? " udp" : "",
	  udpout ? "+" : "");
}

const char* NetHandler::getTargetIP() {
  /* peer->getDotNotation returns a temp variable that is not safe
   * to pass around.  we keep a copy in allocated memory for safety.
   */
  if (dotNotation.size() == 0)
    dotNotation = peer.getDotNotation();
  return dotNotation.c_str();
}

int NetHandler::sizeOfIP() {
  // IPv4 is 1 byte for type and 4 bytes for IP = 5
  // IPv6 is 1 byte for type and 16 bytes for IP = 17
  return peer.getIPVersion() == 4 ? 5 : 17;
}

void *NetHandler::packAdminInfo(void *buf) {
  buf = peer.pack(buf);
  return buf;
}

NetHandler *NetHandler::whoIsAtIP(const std::string& IP) {
  NetHandler *player = NULL;
  std::list<NetHandler*>::const_iterator it;

  for (it = netConnections.begin(); it != netConnections.end(); it++)
    // FIXME: this is broken for IPv6
    // there can be multiple ascii formats for the same address
    if ((*it)->closed
	&& !strcmp((*it)->peer.getDotNotation().c_str(), IP.c_str())) {
      player = *it;
      break;
    }
  return player;
}

in_addr NetHandler::getIPAddress() {
  return uaddr.sin_addr;
}

const char *NetHandler::getHostname() {
  return ares.getHostname();
}

bool NetHandler::reverseDNSDone()
{
  AresHandler::ResolutionStatus status = ares.getStatus();
  return (status == AresHandler::Failed)
    || (status == AresHandler::HbASucceeded);
}

void NetHandler::setClientKind(int kind)
{
  if (clientType == clientNone)
    clientType = kind;
}

int NetHandler::getClientKind()
{
  return clientType;
}

void NetHandler::setCurrentTime(TimeKeeper tm)
{
  now = tm;
}

void NetHandler::setUDPin(struct sockaddr_in *_uaddr)
{
  if (_uaddr->sin_port)
    uaddr.sin_port = _uaddr->sin_port;
  udpin = true;
}

// Local Variables: ***
// mode:C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: t ***
// End: ***
// ex: shiftwidth=2 tabstop=8
