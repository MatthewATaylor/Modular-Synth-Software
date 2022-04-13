#include "../include/LCD.h"

LCD::LCD(uint8_t ePin, uint8_t rsPin, uint8_t db4Pin, uint8_t db5Pin, uint8_t db6Pin, uint8_t db7Pin) :
	ePin(ePin), rsPin(rsPin), db4Pin(db4Pin), db5Pin(db5Pin), db6Pin(db6Pin), db7Pin(db7Pin) {}

void LCD::setup() {
	ePin.setup();
	rsPin.setup();
	db4Pin.setup();
	db5Pin.setup();
	db6Pin.setup();
	db7Pin.setup();

	usleep(50000);

	this->ePin.writeValue(0);
	this->rsPin.writeValue(0);
	this->db4Pin.writeValue(0);
	this->db5Pin.writeValue(0);
	this->db6Pin.writeValue(0);
	this->db7Pin.writeValue(0);

	writeData(0, 0, 1, 1);
	usleep(4500);

	writeData(0, 0, 1, 1);
	usleep(4500);

	writeData(0, 0, 1, 1);
	usleep(150);

	writeData(0, 0, 1, 0);  // 4-bit mode

	writeData(0, 0, 1, 0);
	writeData(1, 0, 0, 0);  // 2 lines, 5x8 dots

	setDisplayOn(false);
	clear();

	writeData(0, 0, 0, 0);
	writeData(0, 1, 1, 0);  // Entry mode set (increment on write, no shift)

	setDisplayOn(true);
	returnHome();
}

void LCD::setDisplayOn(bool on) {
	rsPin.writeValue(0);
	writeData(0, 0, 0, 0);
	writeData(1, on, 0, 0);
}

void LCD::clear() {
	rsPin.writeValue(0);
	writeData(0, 0, 0, 0);
	writeData(0, 0, 0, 1);
	usleep(2000);
}

void LCD::returnHome() {
	rsPin.writeValue(0);
	writeData(0, 0, 0, 0);
	writeData(0, 0, 1, 0);
	usleep(2000);
}

void LCD::setCursorPos(uint8_t row, uint8_t col) {
	rsPin.writeValue(0);
	uint8_t pos = row * 0x40 + col;

	writeData(1, (pos & 0b1000000) >> 6, (pos & 0b100000) >> 5, (pos & 0b10000) >> 4);
	writeData((pos & 0b1000) >> 3, (pos & 0b100) >> 2, (pos & 0b10) >> 1, pos & 1);
}

void LCD::writeChar(char character) {
	rsPin.writeValue(1);
	bool bits[8] = {0};  // Lower bits ordered first
	for (uint8_t i = 0; i < 8; ++i) {
		bits[i] = character & 1;
		character >>= 1;
	}
	writeData(bits[7], bits[6], bits[5], bits[4]);
	writeData(bits[3], bits[2], bits[1], bits[0]);
}

void LCD::writeStr(const char *str) {
	for (size_t i = 0; str[i] != '\0'; ++i) {
		writeChar(str[i]);
	}
}

void LCD::pulseEnable() {
	ePin.writeValue(0);
	usleep(1);
	ePin.writeValue(1);
	usleep(1);
	ePin.writeValue(0);
	usleep(100);
}

void LCD::writeData(bool db7, bool db6, bool db5, bool db4) {
	db7Pin.writeValue(db7);
	db6Pin.writeValue(db6);
	db5Pin.writeValue(db5);
	db4Pin.writeValue(db4);
	pulseEnable();
}

