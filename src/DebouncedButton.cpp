#include "../include/DebouncedButton.h"

DebouncedButton::DebouncedButton(uint8_t pin) : buttonPin(pin) {
	buttonPin.setup();
}

bool DebouncedButton::isPressed() {
	bool currentValue = 0;
	bool returnValue = 0;
	buttonPin.readValue(&currentValue);
	if (currentValue && !prevValue && !timerWasSet) {
		debounceTimer.set();
		timerWasSet = 1;
	}
	if (!currentValue && prevValue && timerWasSet) {
		timerWasSet = 0;
	}
	if (timerWasSet && debounceTimer.get_ms() >= 20.0) {
		if (currentValue) {
			returnValue = 1;
		}
	}
	prevValue = currentValue;
	return returnValue;
}

bool DebouncedButton::wasClicked() {
	bool pressed = isPressed();
	bool out = pressed && !wasPressed;
	wasPressed = pressed;
	return out;
}

double DebouncedButton::getHoldTime_s() {
	return debounceTimer.get_s();
}

