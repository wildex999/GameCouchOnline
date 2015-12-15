#ifndef EVENTS_H_
#define EVENTS_H_

#define GCOEVENT_END_CAPTURE TEXT("GCOEV_END_CAPTURE")
#define GCOEVENT_GOT_FRAME TEXT("GCOEV_GOT_FRAME") //Frame ready for copying to shared memory
#define GCOEVENT_FRAME_READY TEXT("GCOEV_FRAME_READY") //Frame ready for copying from shared memory
#define GCOEVENT_FRAME_READ TEXT("GCOEV_FRAME_READ") //Done reading shared memory

#define GCOMEMORY_FRAMEDATA TEXT("GCOMEM_FRAMEDATA")

#endif