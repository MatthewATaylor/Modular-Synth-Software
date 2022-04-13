#include "../include/DigitalInputPin.h"

DigitalInputPin::DigitalInputPin(uint8_t pinNum) : GPIOPin(pinNum) {}

DigitalInputPin::DigitalInputPin() {}

bool DigitalInputPin::setup() {
	if (!exportPin()) {
		return false;
	}
	return setDirection((char*) "in");
}

bool DigitalInputPin::readValue(bool *out) {
	int valueDesc;
	if (!openPin(&valueDesc, O_RDONLY)) {
		return false;
	}
	char valueStr[3];
	if (read(valueDesc, valueStr, 3) == -1) {
		printf("Error: Failed to read pin value\n");
		return false;
	}
	*out = (bool) atoi(valueStr);
	close(valueDesc);
	return true;
}

