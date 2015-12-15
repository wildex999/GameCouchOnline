#ifndef HOSTPLAYER_H_
#define HOSTPLAYER_H_

extern "C" {
#include <enet\enet.h>
}

class HostPlayer {
	ENetPeer* peer;

public:
	HostPlayer(ENetPeer* peer);

	ENetPeer* getPeer();
};

#endif
