#ifndef DEBOUNCED_BUTTON_H
#define DEBOUNCED_BUTTON_H

#include <stdint.h>

#include "Timer.h"
#include "DigitalInputPin.h"

class DebouncedButton {
	public:
		DebouncedButton(uint8_t pin);
		bool wasClicked();

	private:
		DigitalInputPin buttonPin;
		bool prevValue = 0;
		Timer debounceTimer;
		bool timerWasSet = false;
};

#endif

