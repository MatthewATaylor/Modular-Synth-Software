#include "../include/GPIOExpander.h"

GPIOExpander::GPIOExpander(int i2cFile) : I2CDevice(i2cFile) {}
GPIOExpander::GPIOExpander() {}

bool GPIOExpander::pinMode(Port port, uint8_t configuration) {
	uint8_t addr = (port == GPIOExpander::Port::A ? 0 : 1);
	uint8_t buffer[2] = {addr, configuration};
	if (write(i2cFile, buffer, 2) != 2) {
		printf("Error: Failed to write to GPIO expander pin\n");
		return false;
	}

	// Turn off all outputs
	for (uint8_t i = 0; i < 8; ++i) {
		uint8_t pinIsInput = configuration & 1;
		if (!pinIsInput) {
			writePin(port, i, 0);
		}
		configuration >>= 1;
	}

	return true;
}

bool GPIOExpander::writePin(Port port, uint8_t pinNum, bool state) {
	uint8_t addr = (port == GPIOExpander::Port::A ? 0x12 : 0x13);

	// Set bit at position pinNum to value of state
	uint8_t newPinValues = 
		pinValues[(uint8_t) port] ^ 
		(((-(uint8_t) state) ^ pinValues[(uint8_t) port]) & (1u << pinNum));
	
	uint8_t buffer[2] = {addr, newPinValues};
	if (write(i2cFile, buffer, 2) != 2) {
		printf("Error: Failed to write to GPIO expander pin\n");
		return false;
	}
	pinValues[(uint8_t) port] = newPinValues;
	return true;
}

bool GPIOExpander::writePins(Port port, uint8_t states) {
	uint8_t addr = (port == GPIOExpander::Port::A ? 0x12 : 0x13);
	uint8_t buffer[2] = {addr, states};
	if (write(i2cFile, buffer, 2) != 2) {
		printf("Error: Failed to write to GPIO expander pin\n");
		return false;
	}
	pinValues[(uint8_t) port] = states;
	return true;
}

bool GPIOExpander::readPin(Port port, uint8_t pinNum, bool *state) {
	uint8_t states;
	if (!readPins(port, &states)) {
		printf("Error: Failed to read from GPIO expander pin\n");
		return false;
	}
	*state = (states >> pinNum) & 1;
	return true;
}

bool GPIOExpander::readPins(Port port, uint8_t *states) {
	uint8_t addr = (port == GPIOExpander::Port::A ? 0x12 : 0x13);
	if (write(i2cFile, &addr, 1) != 1) {
		printf("Error: Failed to write to GPIO expander pin\n");
		return false;
	}
	if (read(i2cFile, states, 1) != 1) {
		printf("Error: Failed to read from GPIO expander pin\n");
		return false;
	}
	return true;
}

