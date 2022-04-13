#include "../include/GPIOPin.h"

GPIOPin::~GPIOPin() {
	closePin();
}

bool GPIOPin::closePin() {
	return unexportPin(true);
}

GPIOPin::GPIOPin(uint8_t pinNum) : pinNum(pinNum) {
	sprintf(pinNumStr, "%d", pinNum);
	unexportPin(false);
}

GPIOPin::GPIOPin() {}

bool GPIOPin::exportPin() {
	int exportDesc = open("/sys/class/gpio/export", O_WRONLY);
	if (exportDesc == -1) {
		printf("Error: Failed to open /sys/class/gpio/export\n");
		return false;
	}

	uint8_t pinNumBytes = strlen(pinNumStr);
	if (write(exportDesc, pinNumStr, pinNumBytes) != pinNumBytes) {
		printf("Error: Failed to export pin\n");
		return false;
	}

	close(exportDesc);
	return true;
}

bool GPIOPin::setDirection(char direction[]) {
	char directionPath[36] = "/sys/class/gpio/gpio";
	strcat(directionPath, pinNumStr);
	strcat(directionPath, "/direction");
	
	int directionDesc = open(directionPath, O_WRONLY);
	if (directionDesc == -1) {
		printf("Error: Failed to open %s\n", directionPath);
		return false;
	}

	uint8_t directionBytes = strlen(direction);
	if (write(directionDesc, direction, directionBytes) != directionBytes) {
		printf("Error: Failed to write pin direction\n");
		return false;
	}

	close(directionDesc);
	return true;
}

bool GPIOPin::openPin(int *valueDesc, uint8_t accessType) {
	char valuePath[32] = "/sys/class/gpio/gpio";
	strcat(valuePath, pinNumStr);
	strcat(valuePath, "/value");
	
	*valueDesc = open(valuePath, accessType);
	if (*valueDesc == -1) {
		printf("Error: Failed to open %s\n", valuePath);
		return false;
	}
	
	return true;
}

bool GPIOPin::unexportPin(bool displayErrors) {
	int unexportDesc = open("/sys/class/gpio/unexport", O_WRONLY);
	if (unexportDesc == -1) {
		if (displayErrors) {
			printf("Error: Failed to open /sys/class/gpio/unexport\n");
		}
		return false;
	}

	uint8_t pinNumBytes = strlen(pinNumStr);
	if (write(unexportDesc, pinNumStr, pinNumBytes) != pinNumBytes) {
		if (displayErrors) {
			printf("Error: Failed to unexport pin\n");
		}
		return false;
	}
	
	close(unexportDesc);
	return true;
}

