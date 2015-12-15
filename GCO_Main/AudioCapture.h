#ifndef AUDIOCAPTURE_H_
#define AUDIOCAPTURE_H_

#include <mmdeviceapi.h>
#include <Audioclient.h>

class AudioCapture {
	static bool comInitialized;

	IMMDeviceEnumerator *devEnumerator;
	IMMDevice *dev;
	IAudioClient *audioClient;
	IAudioCaptureClient *captureClient;
	WAVEFORMATEX *mixFormat;
	REFERENCE_TIME requestedDuration;
	REFERENCE_TIME bufferDuration;
	UINT32 bufferFrameCount;
	UINT32 frameSize; //Size of a frame in bytes
	bool initialized;

	char* data;
	int dataBufferSize;

public:
	AudioCapture();

	bool startCapture();
	bool getData(char** out_data, int* out_sizeBytes);
	void endCapture();

	short getChannelCount();
	short getBitsPerSample();
	int getSamplesPerSecond();

	bool isInitialized() { return initialized; };
};

#endif
