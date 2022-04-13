#include "../include/I2CDevice.h"

I2CDevice::I2CDevice(int i2cFile) : i2cFile(i2cFile) {}
I2CDevice::I2CDevice() {}

bool I2CDevice::open(uint8_t addr) {
	if (ioctl(i2cFile, I2C_SLAVE, addr) < 0) {
		printf("Error: Failed to communicate with I2C device\n");
		return false;
	}
	return true;
}

