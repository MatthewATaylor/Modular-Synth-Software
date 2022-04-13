#include "../include/DigitalOutputPin.h"

DigitalOutputPin::DigitalOutputPin(uint8_t pinNum) : GPIOPin(pinNum) {}

DigitalOutputPin::DigitalOutputPin() {}

bool DigitalOutputPin::setup() {
	if (!exportPin()) {
		return false;
	}
	if (!setDirection((char*) "out")) {
		return false;
	}
	return writeValue(0);
}

bool DigitalOutputPin::writeValue(bool value) {
	int valueDesc;
	if (!openPin(&valueDesc, O_WRONLY)) {
		return false;
	}
	if (write(valueDesc, value ? "1" : "0", 1) != 1) {
		printf("Error: Failed to write pin value\n");
		return false;
	}
	close(valueDesc);
	return true;
}

bool DigitalOutputPin::closePin() {
	int valueDesc;
	if (!openPin(&valueDesc, O_WRONLY)) {
		return false;
	}
	if (write(valueDesc, "0", 1) != 1) {
		printf("Error: Failed to turn off pin\n");
		return false;
	}
	close(valueDesc);
	return unexportPin(true);
}

