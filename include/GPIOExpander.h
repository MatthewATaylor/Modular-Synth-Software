#ifndef GPIO_EXPANDER_H
#define GPIO_EXPANDER_H

#include <unistd.h>
#include <stdio.h>

#include "I2CDevice.h"

class GPIOExpander : public I2CDevice {
	public:
		enum class Port {A, B};

		GPIOExpander(int i2cFile);
		GPIOExpander();

		/*
		 * Set direction of GPIO pins on specified port, where 1 = input and 0 = output
		 */
		bool pinMode(Port port, uint8_t configuration);

		/*
		 * Write state to specified pin
		 */
		bool writePin(Port port, uint8_t pinNum, bool state);

		/*
		 * Write states to all pins on specified port
		 */
		bool writePins(Port port, uint8_t states);

		bool readPin(Port port, uint8_t pinNum, bool *state);
		bool readPins(Port port, uint8_t *states);

	protected:
		static const uint8_t NUM_PORTS = 2;
		static const uint8_t PINS_PER_PORT = 8;

		uint8_t pinValues[NUM_PORTS] = {0b00000000, 0b00000000};
};

#endif

