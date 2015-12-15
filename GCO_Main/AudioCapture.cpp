#include "AudioCapture.h"
#include "Output.h"

//Modified from tutorial at: https://msdn.microsoft.com/en-us/library/windows/desktop/dd370800(v=vs.85).aspx

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

bool AudioCapture::comInitialized = false;

AudioCapture::AudioCapture() {
	initialized = false;

	captureClient = NULL;
	audioClient = NULL;
	dev = NULL;
	devEnumerator = NULL;
}

bool AudioCapture::startCapture() {
	if (isInitialized())
		return false;

	HRESULT result;
	devEnumerator = NULL;
	dev = NULL;
	audioClient = NULL;
	captureClient = NULL;
	mixFormat = NULL;
	requestedDuration = REFTIMES_PER_SEC;

	if (!comInitialized)
	{
		CoInitialize(NULL);
		comInitialized = true;
	}

	const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
	const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
	const IID IID_IAudioClient = __uuidof(IAudioClient);
	const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

	result = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&devEnumerator);
	if (FAILED(result))
	{
		Output("Failed to get IMMDeviceEnumerator instance, error: %x\n", result);
		return false;
	}

	result = devEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
	if (FAILED(result))
	{
		Output("Failed to get IMMDevice, error: %x notfound: %x\n", result, E_NOTFOUND);
		endCapture();
		return false;
	}

	result = dev->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&audioClient);
	if (FAILED(result))
	{
		Output("Failed to activate/get AudioClient, error: %x\n", result);
		endCapture();
		return false;
	}

	result = audioClient->GetMixFormat(&mixFormat);
	if (FAILED(result))
	{
		Output("Failed to get mix format, error: %x\n", result);
		endCapture();
		return false;
	}

	frameSize = mixFormat->nChannels * (mixFormat->wBitsPerSample / 8);
	Debug("Channels: %d, BitsPerSample: %d, SamplesPerSecond: %d, Format: %d, FrameSize: %d\n", mixFormat->nChannels, mixFormat->wBitsPerSample, mixFormat->nSamplesPerSec, mixFormat->wFormatTag, frameSize);

	result = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, requestedDuration, 0, mixFormat, NULL);
	if (FAILED(result))
	{
		Output("Failed to initialize AudioClient, error: %x\n", result);
		endCapture();
		return false;
	}

	result = audioClient->GetBufferSize(&bufferFrameCount);
	if (FAILED(result))
	{
		Output("Failed to get buffer size, error: %x\n", result);
		endCapture();
		return false;
	}

	result = audioClient->GetService(IID_IAudioCaptureClient, (void**)&captureClient);
	if (FAILED(result))
	{
		Output("Failed to get Audio Capture Client, error: %x\n", result);
		endCapture();
		return false;
	}

	bufferDuration = (REFERENCE_TIME)REFTIMES_PER_SEC * (bufferFrameCount / mixFormat->nSamplesPerSec);
	Debug("Buffer duration: %d\n", bufferDuration);

	result = audioClient->Start();
	if (FAILED(result))
	{
		Output("Failed to start AudioClient, error: %x\n", result);
		endCapture();
		return false;
	}

	//For now we just create a buffer large enough to contain 1 second of frames
	dataBufferSize = mixFormat->nSamplesPerSec*frameSize;
	data = new char[dataBufferSize];

	initialized = true;

	return true;
}

bool AudioCapture::getData(char** out_data, int* out_sizeBytes) {
	if (!initialized)
		return false;
	HRESULT result;

	int dataOffset = 0;

	BYTE* buffer;
	UINT32 framesAvailable;
	DWORD flags;

	UINT32 frameCount = 0;
	while (true)
	{
		result = captureClient->GetNextPacketSize(&frameCount);
		if (FAILED(result))
		{
			Output("Failed to get number of frames, error: %x\n", result);
			return false;
		}
		if (frameCount == 0)
			break;	

		result = captureClient->GetBuffer(&buffer, &framesAvailable, &flags, NULL, NULL);
		if (FAILED(result))
		{
			Output("Failed to get audio buffer, error: %x\n", result);
			return false;
		}

		//Copy audio data if they are not silent
		int dataSize = framesAvailable*frameSize;
		if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
		{
			//Write frames to our buffer
			if (dataOffset + dataSize >= dataBufferSize)
			{
				captureClient->ReleaseBuffer(0);
				break;
			}

			memcpy_s(&data[dataOffset], dataBufferSize, buffer, dataSize);
		}
		else
			memset(&data[dataOffset], 0, dataSize); //Even silence needs to be indicated

		dataOffset += dataSize;
		captureClient->ReleaseBuffer(framesAvailable);
	}

	*out_data = data;
	*out_sizeBytes = dataOffset;

	return true;
}


void AudioCapture::endCapture() {
	initialized = false;

	delete data;

	if (captureClient)
		captureClient->Release();
	captureClient = NULL;

	if (audioClient)
		audioClient->Release();
	audioClient = NULL;

	if (dev)
		dev->Release();
	dev = NULL;

	if (devEnumerator)
		devEnumerator->Release();
	devEnumerator = NULL;

}

short AudioCapture::getChannelCount() {
	if (!initialized)
		return 0;
	return mixFormat->nChannels;
}

short AudioCapture::getBitsPerSample() {
	if (!initialized)
		return 0;
	return mixFormat->wBitsPerSample;
}

int AudioCapture::getSamplesPerSecond() {
	return mixFormat->nSamplesPerSec;
}