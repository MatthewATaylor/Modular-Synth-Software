#include "../include/DAC.h"

DAC::DAC(int i2cFile) : I2CDevice(i2cFile) {}
DAC::DAC() {}

bool DAC::writeData(uint16_t value, uint8_t command, uint8_t channel) {
	uint8_t msdb = value >> 4;
	uint8_t lsdb = (value & 0b1111) << 4;
	uint8_t ca = (command << 4) + channel;
	uint8_t buffer[3] = {ca, msdb, lsdb};
	if (write(i2cFile, buffer, 3) != 3) {
		printf("Error: Failed to write data to DAC\n");
		return false;
	}
	return true;
}

