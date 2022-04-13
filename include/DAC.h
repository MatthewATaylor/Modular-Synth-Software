#ifndef DAC_H
#define DAC_H

#include <unistd.h>
#include <stdint.h>

#include "I2CDevice.h"

class DAC : public I2CDevice {
	public:
		struct Command {
			static const uint8_t WRITE = 0b0000;
			static const uint8_t UPDATE = 0b0001;
			static const uint8_t WRITE_UPDATE_ALL = 0b0010;
			static const uint8_t WRITE_UPDATE = 0b0011;
		};

		DAC(int i2cFile);
		DAC();

		/*
		 * Set command and access byte and write a 12-bit (0 to 4095) value to the DAC
		 */
		bool writeData(uint16_t value, uint8_t command, uint8_t channel);
};

#endif

