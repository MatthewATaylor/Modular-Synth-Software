#ifndef LCD_H
#define LCD_H

#include <unistd.h>
#include <stdint.h>

#include "DigitalOutputPin.h"
#include "Timer.h"

class LCD {
	public:
		LCD(uint8_t ePin, uint8_t rsPin, uint8_t db4Pin, uint8_t db5Pin, uint8_t db6Pin, uint8_t db7Pin);

		void setup();
		void setDisplayOn(bool on);
		void clear();
		void returnHome();
		void setCursorPos(uint8_t row, uint8_t col);
		void writeChar(char character);
		void writeStr(const char *str);

	private:
		DigitalOutputPin ePin, rsPin, db4Pin, db5Pin, db6Pin, db7Pin;

		void pulseEnable();
		void writeData(bool db7, bool db6, bool db5, bool db4);
};

#endif

