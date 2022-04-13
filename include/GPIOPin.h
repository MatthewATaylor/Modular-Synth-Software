#ifndef GPIO_PIN_H
#define GPIO_PIN_H

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>

class GPIOPin {
	public:
		virtual bool setup() = 0;

		~GPIOPin();

		virtual bool closePin();

	protected:
		uint8_t pinNum;
		char pinNumStr[3] = {0};

		GPIOPin(uint8_t pinNum);
		GPIOPin();
		
		bool exportPin();
		bool setDirection(char direction[]);
		bool openPin(int *valueDesc, uint8_t accessType);
		bool unexportPin(bool displayErrors);
};

#endif

