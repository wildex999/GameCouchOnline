#ifndef HOSTFRAMEPACKET_H_
#define HOSTFRAMEPACKET_H_

extern "C" {
#include <enet\enet.h>
}

class HostFramePacket {
public:
	int width;
	int height;
	int videoByteCount;
	char* videoData;

	short channelCount;
	short bitsPerSample;
	int samplesPerSecond;
	int audioByteCount;
	char* audioData;

	int frameSize; //Size of frame + video + audio

	//Initialize FramePacket struct from existing data
	static HostFramePacket* initFrame(char* data) {
		HostFramePacket* newPacket = (HostFramePacket*)data;

		if (newPacket->videoByteCount == 0)
			newPacket->videoData = NULL;
		else
			newPacket->videoData = ((char*)newPacket) + sizeof(HostFramePacket);

		if (newPacket->audioByteCount == 0)
			newPacket->audioData = NULL;
		else
			newPacket->audioData = ((char*)newPacket) + sizeof(HostFramePacket) + newPacket->videoByteCount;

		newPacket->frameSize = sizeof(HostFramePacket) + newPacket->videoByteCount + newPacket->audioByteCount;

		return newPacket;
	}

	//Initialized FramePacket with video and audio pointers.
	//Assumes All data is in same memory blob, with HostFramePacket first, then video then audio.
	static HostFramePacket* initFrame(HostFramePacket* newPacket, int videoBytes, int audioBytes) {
		newPacket->videoByteCount = videoBytes;
		newPacket->audioByteCount = audioBytes;

		if (videoBytes == 0)
			newPacket->videoData = NULL;
		else
			newPacket->videoData = ((char*)newPacket) + sizeof(HostFramePacket);

		if (audioBytes == 0)
			newPacket->audioData = NULL;
		else
			newPacket->audioData = ((char*)newPacket) + sizeof(HostFramePacket) + videoBytes;

		newPacket->frameSize = sizeof(HostFramePacket) + videoBytes + audioBytes;

		return newPacket;
	}

	//Allocate new frame with space for video and audio and then initializes it.
	static HostFramePacket* allocateFrame(int videoBytes, int audioBytes) {
		HostFramePacket* newPacket = (HostFramePacket*)new char*[sizeof(HostFramePacket)+videoBytes+audioBytes];
		return initFrame(newPacket, videoBytes, audioBytes);
	}

	static void freeCallback(struct _ENetPacket * packet) {
		delete packet->data;
	}
};

#endif
