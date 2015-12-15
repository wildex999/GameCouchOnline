#ifndef INPUTDATA_H_
#define INPUTDATA_H_

//Data sent from clients to server with input

enum InputType {
	InputTypeUnknown = 0,
	InputTypeKeyboard,
	InputTypeMouseMove,
	InputTypeMouseButton,
	InputTypeMouseScroll
};

//Use union instead?

class InputData {
public:
	InputType inputType = InputTypeUnknown;
};

class InputKeyboard {
public:
	InputKeyboard() { inputType = InputTypeKeyboard; }

	InputType inputType;
	unsigned short key;
	bool keyUp; //If true, this is a key up event, if not it's a key down event
	bool scanCode; //If true, the given key value is a scan code
	bool extKey; //Extended key(Right ALT etc.)
};

#endif
