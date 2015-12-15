Project for allowing people to play local multiplayer games over a network. 
Server shares screen and audio to clients, and clients playback video & audio, and can send input to the server.

Currently only works for DirectX9 games(Or apps).
Can crash for certain events(Unhooking, Window Resize, Refresh)
Most values are hardcoded(Video size, video/audio bitrate, settings) and are tuned for LAN.

It works by injecting a custom DirectX9 DLL into the chosen app. It will capture and encode the video frames for every screen refresh, going for speed over quality to decrease delay to client.
It will also capture the computer audio on the server side, and send that to the client.
The client will receive the data, decode the audio/video and show it as soon as it get it. There will most likely be either some delay on the audio, or it will be stuttering(Not balanced for delay and buffering yet).
Client input will be captured and sent back to the server, which will inject it as it's own keyboard input(Mouse not yet implemented).

Compiling the DLL requires:
- libx264 for Video Encoding
- libswscale(ffmpeg library) for scaling video

Compiling Main app requires:
- libavcodec, libswscale and libswresample(ffmpeg library) for video decoding and scaling
- SDL2 for playing video frames
- enet for networking
- libfaac, bass and bass_fx for audio capture and playback


Developed with Visual Studio 2013 on Windows 7.

Example use:
Client connecting to server at 127.0.0.1, no audio playback(Or decoding): GCO_Main.exe -client -addr 127.0.0.1 -noaudio
Server, hooking into Shovel Knight: GCO_Main.exe -server -exename "ShovelKnight.exe"

