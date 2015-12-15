#include "HostPlayer.h"

HostPlayer::HostPlayer(ENetPeer* peer) {
	this->peer = peer;
}

ENetPeer* HostPlayer::getPeer() {
	return peer;
}