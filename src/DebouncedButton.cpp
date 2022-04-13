#include "../include/DebouncedButton.h"

DebouncedButton::DebouncedButton(uint8_t pin) : buttonPin(pin) {
	buttonPin.setup();
}

bool DebouncedButton::wasClicked() {
	bool currentValue = 0;
	buttonPin.readValue(&currentValue);
	if (currentValue && !prevValue && !timerWasSet) {
		debounceTimer.set();
		timerWasSet = true;
	}
	if (timerWasSet && debounceTimer.get_ms() >= 20.0) {
		timerWasSet = false;
		if (currentValue) {
			return true;
		}
	}
	prevValue = currentValue;
	return false;
}

