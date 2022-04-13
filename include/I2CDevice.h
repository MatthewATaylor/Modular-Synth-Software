#ifndef I2C_DEVICE_H
#define I2C_DEVICE_H

#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>

#include <sys/ioctl.h>

class I2CDevice {
	public:
		I2CDevice(int i2cFile);
		I2CDevice();

		/*
		 * Initialize communications with the I2C slave of given address
		 */
		bool open(uint8_t addr);

	protected:
		int i2cFile = -1;
};

#endif

