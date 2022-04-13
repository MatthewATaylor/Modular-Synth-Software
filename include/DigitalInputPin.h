#ifndef DIGITAL_INPUT_PIN_H
#define DIGITAL_INPUT_PIN_H

#include <stdlib.h>

#include "GPIOPin.h"

class DigitalInputPin : public GPIOPin {
	public:
		DigitalInputPin(uint8_t pinNum);
		DigitalInputPin();

		bool setup() override;
		bool readValue(bool *out);
};

#endif

