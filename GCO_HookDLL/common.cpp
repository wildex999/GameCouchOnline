#include "stdafx.h"
#include "common.h"
#include <time.h>

std::wfstream logOutput;
const wchar_t* classNameDummyWindow = TEXT("GCO_DummyWindow");

//Events
HANDLE eventEndCapture;
HANDLE eventFrameReady;
HANDLE eventFrameRead;


void CurrentTimeString(char* out_string, int bufferSize) {
	time_t     now = time(0);
	struct tm  tstruct;
	localtime_s(&tstruct, &now);
	strftime(out_string, bufferSize, "%X: ", &tstruct);
}

void LogCurrentTime() {
	char buf[128];
	CurrentTimeString(buf, 128);
	logOutput << buf;
}