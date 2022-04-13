#ifndef DIGITAL_OUTPUT_PIN_H
#define DIGITAL_OUTPUT_PIN_H

#include "GPIOPin.h"

class DigitalOutputPin : public GPIOPin {
	public:
		DigitalOutputPin(uint8_t pinNum);
		DigitalOutputPin();

		bool setup() override;
		bool writeValue(bool value);
		bool closePin() override;
};

#endif

