#ifndef FRAMEDATA_H_
#define FRAMEDATA_H_

class FrameData {
public:
	static const int maxDataSize = 64 * 1024 * 1024; //TODO: Set this according to actual frame size

	int width;
	int height;
	int pitch;
	int dataSize;

	//Ask Capture DLL for new capture size
	int newWidth, newHeight;
	
	//Return a pointer to the data
	char* getData() {
		//We can't depend on member ordering and use a char as jumping point, so we just return a pointer
		//starting after the FrameData instance.
		return (char*)(this+sizeof(FrameData));
	};
};

#endif
