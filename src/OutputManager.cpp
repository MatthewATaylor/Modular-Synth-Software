#include "../include/OutputManager.h"

OutputManager::OutputManager(int i2cFile, LCD *lcd, DebouncedButton *outputButton, DebouncedButton *channelButton) : i2cFile(i2cFile), dac(i2cFile), gpio(i2cFile), lcd(lcd), outputButton(outputButton), channelButton(channelButton) {
	gpio.open(GPIO_ADDR);
	gpio.pinMode(GPIOExpander::Port::A, 0);
	gpio.pinMode(GPIOExpander::Port::B, 0);

	lcd->clear();
	lcd->returnHome();
	lcd->writeStr("Output  -2345678");
	lcd->setCursorPos(1, 0);
	lcd->writeStr("Channel 11111111");
}

void OutputManager::pressKey(uint8_t noteId, uint8_t channel) {
	bool channelSet = 0;
	bool outputFound = 0;
	uint8_t outputIndex = 0;
	
	// First: look for free output with correct channel
	for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) {
		if (outputs[i].channel == channel) {
			// One of the outputs is set to this MIDI channel
			channelSet = 1;
			if (!outputs[i].gateIsOn) {
				// Current output is not being used
				outputFound = 1;
				outputIndex = i;
				break;
			}
		}
	}

	if (!channelSet) {
		// No outputs set to correct channel, MIDI event ignored
		return;
	}

	// No free outputs but an output is set to correct channel
	if (!outputFound) {
		bool longestGateOnTimeSet = 0;
		double longestGateOnTime = 0;

		// Overwrite output that has been on the longest
		for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) {
			if (outputs[i].channel != channel) {
				continue;
			}
			if (!longestGateOnTimeSet || outputs[i].gateOnTimer.get_s() > longestGateOnTime) {
				longestGateOnTimeSet = 1;
				longestGateOnTime = outputs[i].gateOnTimer.get_s();
				outputIndex = i;
			}
		}
	}

	outputs[outputIndex].noteId = noteId;
	outputs[outputIndex].gateIsOn = 1;
	outputs[outputIndex].gateOnTimer.set();
	outputs[outputIndex].triggerIsOn = 1;
	outputs[outputIndex].triggerOnTimer.set();

	gpio.open(GPIO_ADDR);
	gpio.writePin(GPIOExpander::Port::A, outputIndex, 1);  // Gate
	gpio.writePin(GPIOExpander::Port::B, outputIndex, 1);  // Trigger

	double outVoltage = noteId / 12.0;
	uint16_t dacVal = (uint16_t) (outVoltage / 5.0 * 4095);

	dac.open(DAC_ADDR);
	dac.writeData(dacVal, DAC::Command::WRITE_UPDATE, outputIndex);
}

void OutputManager::releaseKey(uint8_t noteId, uint8_t channel) {
	for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) {
		if (outputs[i].noteId == noteId && outputs[i].channel == channel) {
			outputs[i].gateIsOn = 0;
			
			gpio.open(GPIO_ADDR);
			gpio.writePin(GPIOExpander::Port::A, i, 0);
		}
	}
}

void OutputManager::turnOffChannel(uint8_t channel) {
	for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) {
		if (outputs[i].channel == channel) {
			outputs[i].gateIsOn = 0;
			outputs[i].triggerIsOn = 0;

			gpio.open(GPIO_ADDR);
			gpio.writePin(GPIOExpander::Port::A, i, 0);
			gpio.writePin(GPIOExpander::Port::B, i, 0);
		}
	}
}

void OutputManager::updateTriggers() {
	for (uint8_t i = 0; i < NUM_OUTPUTS; ++i) {
		if (outputs[i].triggerIsOn && outputs[i].triggerOnTimer.get_ms() >= 1) {
			outputs[i].triggerIsOn = 0;

			gpio.open(GPIO_ADDR);
	 		gpio.writePin(GPIOExpander::Port::B, i, 0);
		}
	}		
}

void OutputManager::updateSelectedOutput() {
	if (outputButton->wasClicked()) {
		lcdDeselectOutput();
		++selectedOutput;
		if (selectedOutput >= NUM_OUTPUTS) {
			selectedOutput = 0;
		}
		lcdSelectOutput();
	}
}

void OutputManager::updateChannelAssignments() {
	if (channelButton->wasClicked()) {
		++outputs[selectedOutput].channel;
		if (outputs[selectedOutput].channel >= NUM_OUTPUTS) {
			outputs[selectedOutput].channel = 0;
		}
		lcdSetChannel();

		outputs[selectedOutput].gateIsOn = 0;
		outputs[selectedOutput].triggerIsOn = 0;
		
		gpio.open(GPIO_ADDR);
		gpio.writePin(GPIOExpander::Port::A, selectedOutput, 0);
		gpio.writePin(GPIOExpander::Port::B, selectedOutput, 0);
	}
}

void OutputManager::lcdDeselectOutput() {
	lcd->setCursorPos(0, selectedOutput + 8);
	lcd->writeChar('0' + selectedOutput + 1);
}

void OutputManager::lcdSelectOutput() {
	lcd->setCursorPos(0, selectedOutput + 8);
	lcd->writeChar('-');
}

void OutputManager::lcdSetChannel() {
	lcd->setCursorPos(1, selectedOutput + 8);
	lcd->writeChar('0' + outputs[selectedOutput].channel + 1);
}

