#ifndef OUTPUT_MANAGER_H
#define OUTPUT_MANAGER_H

#include <stdint.h>

#include "Timer.h"
#include "DAC.h"
#include "GPIOExpander.h"
#include "LCD.h"
#include "DebouncedButton.h"

class OutputManager {
	public:
		OutputManager(int i2cFile, LCD *lcd, DebouncedButton *outputButton, DebouncedButton *channelButton);

		void pressKey(uint8_t noteId, uint8_t channel);
		void releaseKey(uint8_t noteId, uint8_t channel);
		void turnOffChannel(uint8_t channel);

		// Below must be called every iteration of main loop
		void updateTriggers();
		void updateSelectedOutput();
		void updateChannelAssignments();

	private:
		struct Output {
			uint8_t noteId = 0;
			uint8_t channel = 0;

			bool gateIsOn = 0;
			Timer gateOnTimer;

			bool triggerIsOn = 0;
			Timer triggerOnTimer;
		};

		static const uint8_t DAC_ADDR = 0b1001000;
		static const uint8_t GPIO_ADDR = 0b0100000;
		static const uint8_t NUM_OUTPUTS = 8;

		Output outputs[NUM_OUTPUTS];
		
		int i2cFile;
		DAC dac;
		GPIOExpander gpio;

		LCD *lcd;
		DebouncedButton *outputButton;
		DebouncedButton *channelButton;
		uint8_t selectedOutput = 0;

		void lcdDeselectOutput();
		void lcdSelectOutput();
		void lcdSetChannel();
};

#endif

