#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <signal.h>

struct GlobalParams {
	static uint8_t midiVoices;
	static uint8_t currentLayer;
	static uint8_t maxLayer;
	static uint16_t bpm;
};
uint8_t GlobalParams::midiVoices = 8;
uint8_t GlobalParams::currentLayer = 0;
uint8_t GlobalParams::maxLayer = 0;
uint16_t GlobalParams::bpm = 0;

class Timer {
	public:
		Timer() {
			set();
		}

		void set() {
			clock_gettime(CLOCK_MONOTONIC, &setTime);
		}

		double get_s() {
			struct timespec currentTime;
			clock_gettime(CLOCK_MONOTONIC, &currentTime);
			return currentTime.tv_sec - setTime.tv_sec + (currentTime.tv_nsec - setTime.tv_nsec) / 1000000000.0;
		}

		double get_ms() {
			return get_s() * 1000.0;
		}

	private:
		struct timespec setTime;
};

class I2CDevice {
	public:
		I2CDevice(int i2cFile) : i2cFile(i2cFile) {}
		I2CDevice() {}

		/*
		 * Initialize communications with the I2C slave of given address
		 */
		bool open(uint8_t addr) {
			if (ioctl(i2cFile, I2C_SLAVE, addr) < 0) {
				printf("Error: Failed to communicate with I2C device\n");
				return false;
			}
			return true;
		}

	protected:
		int i2cFile = -1;
};

class DAC : public I2CDevice {
	public:
		struct Command {
			static const uint8_t WRITE = 0b0000;
			static const uint8_t UPDATE = 0b0001;
			static const uint8_t WRITE_UPDATE_ALL = 0b0010;
			static const uint8_t WRITE_UPDATE = 0b0011;
		};

		DAC(int i2cFile) : I2CDevice(i2cFile) {}
		DAC() {}

		/*
		 * Set command and access byte and write a 12-bit (0 to 4095) value to the DAC
		 */
		bool writeData(uint16_t value, uint8_t command, uint8_t channel) {
			uint8_t msdb = value >> 4;
			uint8_t lsdb = (value & 0b1111) << 4;
			uint8_t ca = (command << 4) + channel;
			uint8_t buffer[3] = {ca, msdb, lsdb};
			if (write(i2cFile, buffer, 3) != 3) {
				printf("Error: Failed to write data to DAC\n");
				return false;
			}
			return true;
		}
};

class ADC : public I2CDevice {
	public:
		ADC(int i2cFile) : I2CDevice(i2cFile) {}
		ADC() {}

		bool readData(uint16_t *out) {
			uint8_t buffer[2];
			if (read(i2cFile, buffer, 2) != 2) {
				printf("Error: Failed to read from ADC\n");
				return false;
			}
			*out = (buffer[0] << 8) + buffer[1];
			return true;
		}

		static double getVoltage(uint16_t value) {
			return value / (double) MAX_VALUE * 2.048;
		}

		// Fill valueToPotPosition array with precalculated values
		static void calculatePotPositions() {
			for (uint16_t i = 1; i < MAX_VALUE; ++i) {
				double v = getVoltage(i);
				valueToPotPosition[i] = (500 * v - 33 + sqrt(290000 * v * v - 33000 * v + 1089)) / (1000.0 * v);
			}
		}

		/*
		 * Get pot position (0 to 1) given ADC reading using precalculated values
		 */
		static double getPotPosition(uint16_t value) {
			return valueToPotPosition[value];
		}

	private:
		static const uint16_t MAX_VALUE = 2047;
		static double valueToPotPosition[MAX_VALUE];
};
double ADC::valueToPotPosition[ADC::MAX_VALUE] = {0.0};

class GPIOExpander : public I2CDevice {
	public:
		enum class Port {A, B};

		GPIOExpander(int i2cFile) : I2CDevice(i2cFile) {}
		GPIOExpander() {}

		/*
		 * Set direction of GPIO pins on specified port, where 1 = input and 0 = output
		 */
		bool pinMode(Port port, uint8_t configuration) {
			uint8_t addr = (port == GPIOExpander::Port::A ? 0 : 1);
			uint8_t buffer[2] = {addr, configuration};
			if (write(i2cFile, buffer, 2) != 2) {
				printf("Error: Failed to write to GPIO expander pin\n");
				return false;
			}

			// Turn off all outputs
			for (uint8_t i = 0; i < 8; ++i) {
				uint8_t pinIsInput = configuration & 1;
				if (!pinIsInput) {
					writePin(port, i, 0);
				}
				configuration >>= 1;
			}

			return true;
		}

		/*
		 * Write state to specified pin
		 */
		bool writePin(Port port, uint8_t pinNum, bool state) {
			uint8_t addr = (port == GPIOExpander::Port::A ? 0x12 : 0x13);

			// Set bit at position pinNum to value of state
			uint8_t newPinValues = 
				pinValues[(uint8_t) port] ^ 
				(-(uint8_t) state ^ pinValues[(uint8_t) port]) & (1u << pinNum);
			
			uint8_t buffer[2] = {addr, newPinValues};
			if (write(i2cFile, buffer, 2) != 2) {
				printf("Error: Failed to write to GPIO expander pin\n");
				return false;
			}
			pinValues[(uint8_t) port] = newPinValues;
			return true;
		}

		/*
		 * Write states to all pins on specified port
		 */
		bool writePins(Port port, uint8_t states) {
			uint8_t addr = (port == GPIOExpander::Port::A ? 0x12 : 0x13);
			uint8_t buffer[2] = {addr, states};
			if (write(i2cFile, buffer, 2) != 2) {
				printf("Error: Failed to write to GPIO expander pin\n");
				return false;
			}
			pinValues[(uint8_t) port] = states;
			return true;
		}

		bool readPin(Port port, uint8_t pinNum, bool *state) {
			uint8_t states;
			if (!readPins(port, &states)) {
				printf("Error: Failed to read from GPIO expander pin\n");
				return false;
			}
			*state = (states >> pinNum) & 1;
			return true;
		}

		bool readPins(Port port, uint8_t *states) {
			uint8_t addr = (port == GPIOExpander::Port::A ? 0x12 : 0x13);
			if (write(i2cFile, &addr, 1) != 1) {
				printf("Error: Failed to write to GPIO expander pin\n");
				return false;
			}
			if (read(i2cFile, states, 1) != 1) {
				printf("Error: Failed to read from GPIO expander pin\n");
				return false;
			}
			return true;
		}

	protected:
		static const uint8_t NUM_PORTS = 2;
		static const uint8_t PINS_PER_PORT = 8;

		uint8_t pinValues[NUM_PORTS] = {0b00000000, 0b00000000};
};

class DebouncedGPIOExpander : public GPIOExpander {
	public:
		DebouncedGPIOExpander(int i2cFile) : GPIOExpander(i2cFile) {}
		DebouncedGPIOExpander() {}

		bool pinWasClicked(Port port, uint8_t pinNum) {
			bool currentValue = 0;
			readPin(port, pinNum, &currentValue);
			if (currentValue && !prevValues[(int) port][pinNum] && !debounceTimerWasSet[(int) port][pinNum]) {
				debounceTimers[(int) port][pinNum].set();
				debounceTimerWasSet[(int) port][pinNum] = true;
			}
			if (debounceTimerWasSet[(int) port][pinNum] && debounceTimers[(int) port][pinNum].get_ms() >= 20.0) {
				debounceTimerWasSet[(int) port][pinNum] = false;
				if (currentValue) {
					return true;
				}
			}
			prevValues[(int) port][pinNum] = currentValue;
			return false;
		}

	private:
		bool prevValues[NUM_PORTS][PINS_PER_PORT] = {0};
		Timer debounceTimers[NUM_PORTS][PINS_PER_PORT];
		bool debounceTimerWasSet[NUM_PORTS][PINS_PER_PORT] = {0};
};

class GPIOPin {
	public:
		virtual bool setup() = 0;

		~GPIOPin() {
			closePin();
		}

		virtual bool closePin() {
			return unexportPin(true);
		}

	protected:
		uint8_t pinNum;
		char pinNumStr[3] = {0};

		GPIOPin(uint8_t pinNum) : pinNum(pinNum) {
			sprintf(pinNumStr, "%d", pinNum);
			unexportPin(false);
		}

		GPIOPin() {}
		
		bool exportPin() {
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

		bool setDirection(char direction[]) {
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

		bool openPin(int *valueDesc, uint8_t accessType) {
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

		bool unexportPin(bool displayErrors) {
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
};

class DigitalOutputPin : public GPIOPin {
	public:
		DigitalOutputPin(uint8_t pinNum) : GPIOPin(pinNum) {}

		DigitalOutputPin() {}

		bool setup() override {
			if (!exportPin()) {
				return false;
			}
			if (!setDirection((char*) "out")) {
				return false;
			}
			return writeValue(0);
		}

		bool writeValue(bool value) {
			int valueDesc;
			if (!openPin(&valueDesc, O_WRONLY)) {
				return false;
			}
			if (write(valueDesc, value ? "1" : "0", 1) != 1) {
				printf("Error: Failed to write pin value\n");
				return false;
			}
			close(valueDesc);
			return true;
		}

		bool closePin() override {
			int valueDesc;
			if (!openPin(&valueDesc, O_WRONLY)) {
				return false;
			}
			if (write(valueDesc, "0", 1) != 1) {
				printf("Error: Failed to turn off pin\n");
				return false;
			}
			close(valueDesc);
			return unexportPin(true);
		}
};

class DigitalInputPin : public GPIOPin {
	public:
		DigitalInputPin(uint8_t pinNum) : GPIOPin(pinNum) {}

		DigitalInputPin() {}

		bool setup() override {
			if (!exportPin()) {
				return false;
			}
			return setDirection((char*) "in");
		}

		bool readValue(bool *out) {
			int valueDesc;
			if (!openPin(&valueDesc, O_RDONLY)) {
				return false;
			}
			char valueStr[3];
			if (read(valueDesc, valueStr, 3) == -1) {
				printf("Error: Failed to read pin value\n");
				return false;
			}
			*out = (bool) atoi(valueStr);
			close(valueDesc);
			return true;
		}
};

class LCD {
	public:
		LCD(uint8_t ePin, uint8_t rsPin, uint8_t db4Pin, uint8_t db5Pin, uint8_t db6Pin, uint8_t db7Pin) :
			ePin(ePin), rsPin(rsPin), db4Pin(db4Pin), db5Pin(db5Pin), db6Pin(db6Pin), db7Pin(db7Pin) {}

		void setup() {
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

		void setDisplayOn(bool on) {
			rsPin.writeValue(0);
			writeData(0, 0, 0, 0);
			writeData(1, on, 0, 0);
		}

		void clear() {
			rsPin.writeValue(0);
			writeData(0, 0, 0, 0);
			writeData(0, 0, 0, 1);
			usleep(2000);
		}

		void returnHome() {
			rsPin.writeValue(0);
			writeData(0, 0, 0, 0);
			writeData(0, 0, 1, 0);
			usleep(2000);
		}

		void setCursorPos(uint8_t row, uint8_t col) {
			rsPin.writeValue(0);
			uint8_t pos = row * 0x40 + col;

			writeData(1, (pos & 0b1000000) >> 6, (pos & 0b100000) >> 5, (pos & 0b10000) >> 4);
			writeData((pos & 0b1000) >> 3, (pos & 0b100) >> 2, (pos & 0b10) >> 1, pos & 1);
		}

		void writeChar(char character) {
			rsPin.writeValue(1);
			bool bits[8] = {0};  // Lower bits ordered first
			for (uint8_t i = 0; i < 8; ++i) {
				bits[i] = character & 1;
				character >>= 1;
			}
			writeData(bits[7], bits[6], bits[5], bits[4]);
			writeData(bits[3], bits[2], bits[1], bits[0]);
		}

		void writeStr(const char *str) {
			for (size_t i = 0; str[i] != '\0'; ++i) {
				writeChar(str[i]);
			}
		}

		// Display current layer
		void writeDefault() {
			if (GlobalParams::currentLayer == 0) {
				writeStr("Layer: MIDI");
			}
			else {
				char layerStr[10] = {0};
				sprintf(layerStr, "Layer: %d", GlobalParams::currentLayer);
				writeStr(layerStr);
			}
			writeBPM();
		}

		void writeTimed(const char *str, double millis) {
			writeStr(str);
			isTimed = true;
			messageTimer.set();
			timerDelay_ms = millis;
		}

		void updateTiming() {
			// Timed message has been shown for long enough, go back to default text
			if (isTimed && messageTimer.get_ms() >= timerDelay_ms) {
				clear();
				writeDefault();
				isTimed = false;
			}
		}

		void updateBPM() {
			if (isTimed) {
				return;
			}

			writeBPM();
		}

	private:
		DigitalOutputPin ePin, rsPin, rwPin, db4Pin, db5Pin, db6Pin, db7Pin;
		bool isTimed = false;
		Timer messageTimer;
		double timerDelay_ms = 0.0;

		void pulseEnable() {
			ePin.writeValue(0);
			usleep(1);
			ePin.writeValue(1);
			usleep(1);
			ePin.writeValue(0);
			usleep(100);
		}

		void writeData(bool db7, bool db6, bool db5, bool db4) {
			db7Pin.writeValue(db7);
			db6Pin.writeValue(db6);
			db5Pin.writeValue(db5);
			db4Pin.writeValue(db4);
			pulseEnable();
		}

		void writeBPM() {
			setCursorPos(1, 0);
			char bpmStr[12] = {0};
			sprintf(bpmStr, "BPM: %3d", GlobalParams::bpm);
			writeStr(bpmStr);
		}
};


class MIDIPacketQueue {
	public:
		const static uint8_t PACKET_SIZE = 4;

		MIDIPacketQueue() {
			queue = new uint8_t*[BUFFER_SIZE];
			for (uint8_t i = 0; i < BUFFER_SIZE; ++i) {
				queue[i] = new uint8_t[PACKET_SIZE];
			}
			capacity = BUFFER_SIZE;
		}

		void push(uint8_t *packet) {
			if (capacity <= length) {
				uint8_t **newQueue = new uint8_t*[length + BUFFER_SIZE];
				
				// Copy old queue data
				for (uint8_t i = 0; i < length; ++i) {
					newQueue[i] = new uint8_t[PACKET_SIZE];
					memcpy(newQueue[i], queue[i], PACKET_SIZE);
				}
				
				// Expand to length + BUFFER_SIZE
				for (uint8_t i = length; i < length + BUFFER_SIZE; ++i) {
					newQueue[i] = new uint8_t[PACKET_SIZE];
				}

				deallocate();
				queue = newQueue;
				capacity += BUFFER_SIZE;
			}

			// Add new packet
			memcpy(queue[length], packet, PACKET_SIZE);

			++length;
		}

		void deallocate() {
			for (uint8_t i = 0; i < capacity; ++i) {
				delete[] queue[i];
			}
			delete[] queue;
		}

		void reset() {
			deallocate();
			capacity = 0;
			length = 0;
		}

		void clear() {
			// Only leave a capacity of BUFFER_SIZE remaining
			for (uint8_t i = BUFFER_SIZE; i < capacity; ++i) {
				delete[] queue[i];
			}
			capacity = BUFFER_SIZE;
			length = 0;
		}

		size_t getLength() const {
			return length;
		}

		const uint8_t *getPacket(size_t index) const {
			return queue[index];
		}

		~MIDIPacketQueue() {
			deallocate();
		}

	private:
		const static uint8_t BUFFER_SIZE = 8;

		uint8_t **queue = nullptr;
		size_t capacity = 0;
		size_t length = 0;
};

class OutputManager {
	public:	
		static const uint8_t NUM_CHANNELS = 8;

		OutputManager(int i2cFile) : dac(i2cFile), gpio(i2cFile) {
			gpio.open(GPIO_EXPANDER_ADDR);
			gpio.pinMode(GPIOExpander::Port::A, 0);  // Triggers
			gpio.pinMode(GPIOExpander::Port::B, 0);  // Gates
			pressedKeys = new uint8_t[PRESSED_KEYS_BUFFER_SIZE];
		}	

		void pressKey(uint8_t keyID) {
			if (!turnOnKey(keyID)) {
				return;
			}

			if (numPressedKeys >= pressedKeysCapacity) {
				uint8_t *newPressedKeys = new uint8_t[pressedKeysCapacity + PRESSED_KEYS_BUFFER_SIZE];
				memcpy(newPressedKeys, pressedKeys, pressedKeysCapacity);
				pressedKeysCapacity += PRESSED_KEYS_BUFFER_SIZE;
				delete[] pressedKeys;
				pressedKeys = newPressedKeys;
			}
			pressedKeys[numPressedKeys] = keyID;
			++numPressedKeys;
		}

		void releaseKey(uint8_t keyID) {
			turnOffKey(keyID);
			if (!removePressedKey(keyID)) {
				return;
			}

			// Add back in last key that was replaced
			if (numPressedKeys >= GlobalParams::midiVoices) {
				for (size_t i = numPressedKeys - 1; i != static_cast<unsigned>(-1); --i) {
					bool keyAlreadyOn = false;
					for (uint8_t j = 0; j < GlobalParams::midiVoices; ++j) {
						if (channels[j].keyID == pressedKeys[i] && channels[j].gateIsOn) {
							keyAlreadyOn = true;
							break;
						}
					}
					if (!keyAlreadyOn) {
						turnOnKey(pressedKeys[i]);
						break;
					}
				}
			}
		}

		void updateTriggers() {
			gpio.open(GPIO_EXPANDER_ADDR);
			for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
				if (channels[i].triggerIsOn && channels[i].triggerTimer.get_ms() > 5.0) {
					gpio.writePin(GPIOExpander::Port::A, i, 0);
					channels[i].triggerIsOn = false;
				}
			}
		}

		// For when midiVoices changes or when setting sequences, release unused channels
		void cleanChannels(uint8_t startChannel, uint8_t endChannel) {
			for (uint8_t i = startChannel; i < endChannel; ++i) {
				removePressedKey(channels[i].keyID);
				turnOffChannel(i);
			}
		}

		void turnOnChannel(uint8_t channel, uint8_t keyID) {
			writeToDAC(keyID, channel);

			gpio.open(GPIO_EXPANDER_ADDR);
			gpio.writePin(GPIOExpander::Port::A, channel, 1);
			gpio.writePin(GPIOExpander::Port::B, channel, 1);

			channels[channel].keyID = keyID;
			channels[channel].gateIsOn = true;
			channels[channel].triggerIsOn = true;
			channels[channel].triggerTimer.set();
		}

		void turnOffChannel(uint8_t channel) {
			gpio.open(GPIO_EXPANDER_ADDR);
			gpio.writePin(GPIOExpander::Port::B, channel, 0);
			channels[channel].gateIsOn = false;
		}

		void setPitchBend(double pitchBend) {
			dac.open(DAC_ADDR);
			for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
				channels[i].pitchBend = pitchBend;
				writeToDAC(channels[i].keyID, i, false);
			}
		}

	private:
		struct OutputChannel {
			uint8_t keyID = 0;  // 0-127
			double pitchBend = 0;
			bool gateIsOn = false;
			bool triggerIsOn = false;
			Timer triggerTimer;
		};

		static const uint8_t GPIO_EXPANDER_ADDR = 0b0100010;
		static const uint8_t DAC_ADDR = 0b1001000;
		static const uint8_t PRESSED_KEYS_BUFFER_SIZE = 4;

		uint8_t *pressedKeys = nullptr;
		size_t numPressedKeys = 0;
		size_t pressedKeysCapacity = PRESSED_KEYS_BUFFER_SIZE;
		OutputChannel channels[NUM_CHANNELS];
		DAC dac;
		GPIOExpander gpio;

		void writeToDAC(uint8_t keyID, uint8_t channel, bool shouldOpenDAC = true) {
			double outputVoltage = ((double) keyID - 53 + channels[channel].pitchBend) / 12.0;
			uint16_t dacValue = (uint16_t) (outputVoltage / 5.0 * 4095);
			
			if (shouldOpenDAC) {
				dac.open(DAC_ADDR);
			}

			dac.writeData(dacValue, DAC::Command::WRITE_UPDATE, channel);
		}

		bool turnOnKey(uint8_t keyID) {
			uint8_t channelToWrite = NUM_CHANNELS;
			bool keyIDTaken = false;
			for (uint8_t i = 0; i < GlobalParams::midiVoices; ++i) {
				if (!channels[i].gateIsOn && channelToWrite == NUM_CHANNELS) {
					channelToWrite = i;
				}
				if (channels[i].gateIsOn && channels[i].keyID == keyID) {
					keyIDTaken = true;
					break;
				}
			}

			// Prevent key double presses
			if (keyIDTaken) {
				return false;
			}

			// No open channel found, replace oldest key press
			if (channelToWrite == NUM_CHANNELS) {
				for (size_t i = 0; i < numPressedKeys; ++i) {
					for (uint8_t j = 0; j < GlobalParams::midiVoices; ++j) {
						if (channels[j].keyID == pressedKeys[i]) {
							turnOffKey(pressedKeys[i]);
							channelToWrite = j;
							goto keyReplacementFound;
						}
					}
				}
				return false;
			}	

// Exit above two for loops
keyReplacementFound:
			turnOnChannel(channelToWrite, keyID);
			return true;
		}

		void turnOffKey(uint8_t keyID) {
			gpio.open(GPIO_EXPANDER_ADDR);
			for (uint8_t i = 0; i < GlobalParams::midiVoices; ++i) {
				if (channels[i].gateIsOn && channels[i].keyID == keyID) {
					gpio.writePin(GPIOExpander::Port::B, i, 0);
					channels[i].gateIsOn = false;
				}
			}
		}

		bool removePressedKey(uint8_t keyID) {
			if (numPressedKeys == 0) {
				return false;
			}

			bool keyFound = false;
			for (size_t i = 0; i < numPressedKeys - 1; ++i) {
				if (pressedKeys[i] == keyID) {
					keyFound = true;
				}
				if (keyFound) {
					// Shift key IDs down to fill released key's slot
					pressedKeys[i] = pressedKeys[i + 1];
				}
			}
			if (!keyFound && pressedKeys[numPressedKeys - 1] != keyID) {
				return false;
			}

			--numPressedKeys;
			return true;
		}
};	

class DebouncedButton {
	public:
		DebouncedButton(uint8_t pin) : buttonPin(pin) {
			buttonPin.setup();
		}
		bool wasClicked() {
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

	private:
		DigitalInputPin buttonPin;
		bool prevValue = 0;
		Timer debounceTimer;
		bool timerWasSet = false;
};

class Sequencer {
	public:
		Sequencer(int i2cFile, OutputManager *outputManager) : ledGPIO(i2cFile), buttonGPIO(i2cFile), outputManager(outputManager) {
			ledGPIO.open(0b0100001);
			ledGPIO.pinMode(GPIOExpander::Port::A, 0);
			ledGPIO.pinMode(GPIOExpander::Port::B, 0);
		}

		// Update number of sequencer layers and channels used by those layers
		void updateLayerLayout() {
			uint8_t totalVoicesUsed = GlobalParams::midiVoices;
			GlobalParams::maxLayer = 0;
			for (uint8_t i = 0; i < MAX_LAYERS; ++i) {
				if (totalVoicesUsed >= OutputManager::NUM_CHANNELS) {
					break;
				}
				layers[i].startChannel = totalVoicesUsed;
				totalVoicesUsed += layers[i].voicesUsed;
				if (totalVoicesUsed > OutputManager::NUM_CHANNELS) {
					layers[i].voicesUsed -= (totalVoicesUsed - OutputManager::NUM_CHANNELS);
				}
				++GlobalParams::maxLayer;
			}
		}

		void updateLEDs() {
			ledGPIO.open(LED_GPIO_ADDR);
			if (GlobalParams::currentLayer == 0) {
				// LEDs off in MIDI layer
				ledGPIO.writePins(GPIOExpander::Port::A, 0);
				ledGPIO.writePins(GPIOExpander::Port::B, 0);
			}
			else {
				GPIOExpander::Port port = GPIOExpander::Port::B;
				uint8_t states = 0;
				for (uint8_t i = 0; i < NUM_STEPS; ++i) {
					bool state = 0;

					if (i == selectedStep) {
						if (selectBlinkTimer.get_ms() >= 100) {
							selectBlinkTimer.set();
							selectBlinkIsOn = !selectBlinkIsOn;
						}
						state = selectBlinkIsOn;
					}
					else {
						uint8_t *noteIDs = layers[GlobalParams::currentLayer - 1].steps[i].noteIDs;
						if (noteIDs[0] == Step::RESET_ID || i == layers[GlobalParams::currentLayer - 1].currentStep) {
							// Off for reset or current step
							state = 0;
						}
						else {
							state = 1;
						}
					}

					states += (state << (i % 8));

					if (i == 7 || i == NUM_STEPS - 1) {
						ledGPIO.writePins(port, states);
						port = GPIOExpander::Port::A;
						states = 0;
					}
				}
			}
		}
		
		void updateSelection() {
			buttonGPIO.open(BUTTON_GPIO_ADDR);
			
			uint8_t statesA;
			buttonGPIO.readPins(GPIOExpander::Port::A, &statesA);
			uint8_t statesB;
			buttonGPIO.readPins(GPIOExpander::Port::B, &statesB);

			uint8_t statesToRead = statesB;
			for (uint8_t i = 0; i < NUM_STEPS; ++i) {
				if (i == 8) {
					statesToRead = statesA;
				}
				bool state = (statesToRead >> (i % 8)) & 1;
				if (state) {
					if (selectedStep != i) {
						keyPressesInSelection = 0;
					}
					selectedStep = i;
					outputManager->cleanChannels(0, GlobalParams::midiVoices);
					return;
				}
			}
			selectedStep = NUM_STEPS;
		}

		bool isAcceptingInput() {
			return selectedStep != NUM_STEPS && GlobalParams::currentLayer != 0;
		}
		
		void pressKey(uint8_t keyID) {
			// First key pushed for selection: setReset() and set first channel to key
			// Following key presses: append noteIDs to step
			
			if (keyPressesInSelection == 0) {
				setReset();
				layers[GlobalParams::currentLayer - 1].steps[selectedStep].noteIDs[0] = keyID;
			}
			else {
				if (GlobalParams::currentLayer == GlobalParams::maxLayer) {
					uint8_t totalVoicesUsed = GlobalParams::midiVoices;
					for (uint8_t i = 0; i < GlobalParams::maxLayer; ++i) {
						totalVoicesUsed += layers[i].voicesUsed;
					}
					if (totalVoicesUsed >= OutputManager::NUM_CHANNELS) {
						return;
					}
				}
				//uint8_t currentLayerVoicesUsed = layers[GlobalParams::currentLayer - 1].voicesUsed;
				//uint8_t firstChannelID = layers[GlobalParams::currentLayer - 1].steps[selectedStep].noteIDs[0];
				uint8_t currentStepVoicesUsed = 0;
				for (uint8_t i = 0; i < OutputManager::NUM_CHANNELS; ++i) {
					uint8_t currentChannelID = layers[GlobalParams::currentLayer - 1].steps[selectedStep].noteIDs[i];
					if (currentChannelID == Step::REST_ID || currentChannelID == Step::RESET_ID) {
						break;
					}
					++currentStepVoicesUsed;
				}
				//uint8_t channelToUpdate = currentLayerVoicesUsed;

				layers[GlobalParams::currentLayer - 1].steps[selectedStep].noteIDs[currentStepVoicesUsed] = keyID;
				updateVoicesUsed(GlobalParams::currentLayer - 1);
				updateLayerLayout();
			}

			++keyPressesInSelection;
		}

		void setRest() {
			for (uint8_t i = 0; i < OutputManager::NUM_CHANNELS; ++i) {
				layers[GlobalParams::currentLayer - 1].steps[selectedStep].noteIDs[i] = Step::REST_ID;
			}
			updateVoicesUsed(GlobalParams::currentLayer - 1);
			updateLayerLayout();
		}

		void setReset() {
			for (uint8_t i = 0; i < OutputManager::NUM_CHANNELS; ++i) {
				layers[GlobalParams::currentLayer - 1].steps[selectedStep].noteIDs[i] = Step::RESET_ID;
			}
			updateVoicesUsed(GlobalParams::currentLayer - 1);
			updateLayerLayout();
		}

		void setRatchet(LCD *lcd) {
			uint8_t *ratchetDivisions = &(layers[GlobalParams::currentLayer - 1].steps[selectedStep].ratchetDivisions);
			++(*ratchetDivisions);
			if (*ratchetDivisions > Step::MAX_RATCHET_DIVISIONS) {
				*ratchetDivisions = 1;
			}
			char textToDisplay[16] = {0};
			sprintf(textToDisplay, "Divisions: %d", *ratchetDivisions);
			lcd->clear();
			lcd->writeTimed(textToDisplay, 1000);
		}

		void setTie(LCD *lcd) {
			bool *isTie = &layers[GlobalParams::currentLayer - 1].steps[selectedStep].isTie;
			*isTie = !(*isTie);
			char textToDisplay[12] = {0};
			sprintf(textToDisplay, "Tie: %s", *isTie ? "On" : "Off");
			lcd->clear();
			lcd->writeTimed(textToDisplay, 1000);
		}

		// Restart all layers to first beat
		void syncLayers() {
			for (uint8_t i = 0; i < MAX_LAYERS; ++i) {
				layers[i].currentStep = 0;
			}
		}

		void resetAll() {
			for (uint8_t i = 0; i < MAX_LAYERS; ++i) {
				layers[i].currentStep = 0;
				for (uint8_t j = 0; j < NUM_STEPS; ++j) {
					for (uint8_t k = 0; k < OutputManager::NUM_CHANNELS; ++k) {
						layers[i].steps[j].noteIDs[k] = Step::RESET_ID;
						layers[i].steps[j].ratchetDivisions = 1;
						layers[i].steps[j].numEnables = 1;
						layers[i].steps[j].numDisables = 0;
						layers[i].steps[j].isTie = false;
						outputManager->turnOffChannel(layers[i].startChannel + k); 
					}
				}
			}
		}

		void play() {
			bool shouldAdvanceStep = false;
			double secondsPerBeat = 60.0 / GlobalParams::bpm / 4.0;
			double elapsedSeconds = stepTimer.get_s();
					
			if (elapsedSeconds >= secondsPerBeat) {	
				shouldAdvanceStep = true;
				stepTimer.set();
			}

			for (uint8_t i = 0; i < GlobalParams::maxLayer; ++i) {
				double secondsPerDivision = secondsPerBeat / layers[i].steps[layers[i].currentStep].ratchetDivisions;
				bool isTie = layers[i].steps[layers[i].currentStep].isTie;
				bool shouldTurnOnStep = elapsedSeconds >= layers[i].steps[layers[i].currentStep].numEnables * secondsPerDivision;
				bool shouldTurnOffStep = elapsedSeconds >= (layers[i].steps[layers[i].currentStep].numDisables + 0.5) * secondsPerDivision;

				if (shouldAdvanceStep) {
					layers[i].steps[layers[i].currentStep].numEnables = 1;
					layers[i].steps[layers[i].currentStep].numDisables = 0;
					++layers[i].currentStep;
					if (layers[i].currentStep == NUM_STEPS) {
						layers[i].currentStep = 0;
					}
					else if (layers[i].steps[layers[i].currentStep].noteIDs[0] == Step::RESET_ID) {
						layers[i].currentStep = 0;
					}

					uint8_t *noteIDs = layers[i].steps[layers[i].currentStep].noteIDs;
					if (noteIDs[0] != Step::REST_ID) {
						for (uint8_t j = 0; j < layers[i].voicesUsed; ++j) {
							if (noteIDs[j] == Step::RESET_ID) {
							       break;
							}
							outputManager->turnOnChannel(layers[i].startChannel + j, noteIDs[j]);
						}
					}
				}
				else if (shouldTurnOnStep) {  // For ratchet divisions
					uint8_t *noteIDs = layers[i].steps[layers[i].currentStep].noteIDs;
					if (noteIDs[0] != Step::REST_ID) {
						for (uint8_t j = 0; j < layers[i].voicesUsed; ++j) {
							outputManager->turnOnChannel(layers[i].startChannel + j, noteIDs[j]);
						}
					}
					++layers[i].steps[layers[i].currentStep].numEnables;
				}
				else if (shouldTurnOffStep) {
					if (!(isTie && layers[i].steps[layers[i].currentStep].numEnables == layers[i].steps[layers[i].currentStep].ratchetDivisions)) {
						// Not a tie on the last subdivision
						for (uint8_t j = 0; j < layers[i].voicesUsed; ++j) {
							outputManager->turnOffChannel(layers[i].startChannel + j);
						}
						++layers[i].steps[layers[i].currentStep].numDisables;
					}
				}
			}
		}

	private:
		static const uint8_t MAX_LAYERS = 8;
		static const uint8_t NUM_STEPS = 16;
		static const uint8_t LED_GPIO_ADDR = 0b0100001;
		static const uint8_t BUTTON_GPIO_ADDR = 0b0100000;

		struct Step {
			static const uint8_t REST_ID = 128;
			static const uint8_t RESET_ID = 129;
			static const uint8_t MAX_RATCHET_DIVISIONS = 4;

			uint8_t noteIDs[OutputManager::NUM_CHANNELS];
			uint8_t ratchetDivisions = 1;
			uint8_t numEnables = 1;
			uint8_t numDisables = 0;
			bool isTie = false;

			Step() {
				for (uint8_t i = 0; i < OutputManager::NUM_CHANNELS; ++i) {
					noteIDs[i] = RESET_ID;
				}
			}
		};

		struct Layer {
			uint8_t voicesUsed = 1;
			uint8_t startChannel = 0;
			Step steps[NUM_STEPS];
			uint8_t currentStep = 0;
		};

		OutputManager *outputManager = nullptr;
		
		Layer layers[MAX_LAYERS];
		GPIOExpander ledGPIO;
		DebouncedGPIOExpander buttonGPIO;
		
		uint8_t selectedStep = NUM_STEPS;
		Timer selectBlinkTimer;
		bool selectBlinkIsOn = false;

		Timer stepTimer;

		size_t keyPressesInSelection = 0;

		void updateVoicesUsed(uint8_t layerIndex) {
			uint8_t maxVoicesUsed = 1;
			for (uint8_t i = 0; i < NUM_STEPS; ++i) {
				uint8_t voicesUsedInStep = 1;
				for (uint8_t j = 0; j < OutputManager::NUM_CHANNELS; ++j) {
					if (layers[layerIndex].steps[i].noteIDs[j] == Step::RESET_ID || layers[layerIndex].steps[i].noteIDs[j] == Step::REST_ID) {
						break;
					}
					if (j != 0) {
						++voicesUsedInStep;
					}
				}
				if (voicesUsedInStep > maxVoicesUsed) {
					maxVoicesUsed = voicesUsedInStep;
				}
			}
			layers[layerIndex].voicesUsed = maxVoicesUsed;
		}
};

MIDIPacketQueue midiQueue;
pthread_t midiThread;
pthread_mutex_t midiLock;
const uint8_t MIDI_PACKET_SIZE = 3;

void *midiRead(void *arg) {
	int descriptor = open("/dev/midi1", O_RDONLY);
	if (descriptor == -1) {
		printf("Error: Failed to open MIDI device\n");
		return nullptr;
	}
	while (true) {
		uint8_t packet[MIDI_PACKET_SIZE];
		if (read(descriptor, packet, MIDI_PACKET_SIZE) == -1) {
			printf("Error: Failed to read MIDI packet\n");
			return nullptr;
		}
		pthread_mutex_lock(&midiLock);
		midiQueue.push(packet);
		pthread_mutex_unlock(&midiLock);
	}
	return nullptr;
}

bool midiInit() {
	if (pthread_mutex_init(&midiLock, NULL) != 0) {
		printf("Error: Failed to set MIDI lock\n");
		return false;
	}
	if (pthread_create(&midiThread, NULL, &midiRead, NULL) != 0) {
		printf("Error: Failed to create MIDI input thread\n");
		return false;
	}
	pthread_detach(midiThread);
	return true;
}

int main() {
	const uint8_t ADC_ADDR = 0b1101010;

	int i2cFile = open("/dev/i2c-1", O_RDWR);
	if (i2cFile < 0) {
		printf("Error: Failed to open I2C bus\n");
		return 1;
	}

	DAC dac(i2cFile);
	dac.open(0b1001000);
/*
	while (true) {
		dac.writeData(1000, DAC::Command::WRITE_UPDATE, 0);
		dac.writeData(1200, DAC::Command::WRITE_UPDATE, 1);
		dac.writeData(1400, DAC::Command::WRITE_UPDATE, 2);
		dac.writeData(1600, DAC::Command::WRITE_UPDATE, 3);
		dac.writeData(1800, DAC::Command::WRITE_UPDATE, 4);
		dac.writeData(2000, DAC::Command::WRITE_UPDATE, 5);
		dac.writeData(2200, DAC::Command::WRITE_UPDATE, 6);
		dac.writeData(2400, DAC::Command::WRITE_UPDATE, 7);
	}
*/

/*
	OutputManager outputManager(i2cFile);
	Sequencer sequencer(i2cFile, &outputManager);

	ADC rateADC(i2cFile);
	ADC::calculatePotPositions();

	LCD lcd(12, 13, 4, 5, 6, 7);
	lcd.setup();
	lcd.writeStr("Welcome!");
	usleep(2000000);

	midiInit();

	DigitalInputPin restPin(14);
	restPin.setup();
	Timer restTimer;

	DigitalInputPin resetPin(21);
	resetPin.setup();
	Timer resetTimer;

	DebouncedButton modeButton(17);
	DebouncedButton layerDecreaseButton(27);
	DebouncedButton layerIncreaseButton(22);
	DebouncedButton ratchetButton(20);
	DebouncedButton tieButton(26);

	lcd.clear();
	lcd.writeDefault();

	uint16_t prevBPM = 0;
	while (true) {
		// Change mode (number of MIDI voices for MIDI layer or playing type for seq layer)
		if (modeButton.wasClicked()) {
			if (GlobalParams::currentLayer == 0) {
				++GlobalParams::midiVoices;
				if (GlobalParams::midiVoices > OutputManager::NUM_CHANNELS) {
					GlobalParams::midiVoices = 0;
					outputManager.cleanChannels(0, OutputManager::NUM_CHANNELS);
				}
				else {
					outputManager.cleanChannels(GlobalParams::midiVoices - 1, GlobalParams::midiVoices);
				}

				char textToDisplay[16] = {0};
				sprintf(textToDisplay, "MIDI Voices: %d", GlobalParams::midiVoices);

				lcd.clear();
				lcd.writeTimed(textToDisplay, 1000);

				sequencer.updateLayerLayout();
			}
		}

		// Change layer (where layer 0 means MIDI layer)
		if (layerDecreaseButton.wasClicked()) {
			if (GlobalParams::currentLayer == 0) {
				GlobalParams::currentLayer = GlobalParams::maxLayer;
			}
			else {
				--GlobalParams::currentLayer;
			}
			lcd.clear();
			lcd.writeDefault();
		}
		else if (layerIncreaseButton.wasClicked()) {
			if (GlobalParams::currentLayer >= GlobalParams::maxLayer) {
				GlobalParams::currentLayer = 0;
			}
			else {
				++GlobalParams::currentLayer;
			}
			lcd.clear();
			lcd.writeDefault();
		}

		// MIDI input
		pthread_mutex_lock(&midiLock);
		for (size_t i = 0; i < midiQueue.getLength(); ++i) {			
			const uint8_t *packet = midiQueue.getPacket(i);
			if (packet[0] == 144) {
				if (sequencer.isAcceptingInput()) {
					sequencer.pressKey(packet[1]);
				}
				else {
					outputManager.pressKey(packet[1]);
				}
			}
			else if (packet[0] == 128) {
				outputManager.releaseKey(packet[1]);
			}
			else if (packet[0] == 224) {
				outputManager.setPitchBend((((double) packet[2] - 64) / 64.0) * 3);
			}
		}
		midiQueue.clear();
		pthread_mutex_unlock(&midiLock);

		outputManager.updateTriggers();
		lcd.updateTiming();
		sequencer.updateLEDs();
		sequencer.updateSelection();
		sequencer.play();

		// BPM
		rateADC.open(ADC_ADDR);
		uint16_t adcValue;	
		rateADC.readData(&adcValue);
		double bpm = 225 * rateADC.getPotPosition(adcValue + 1) + 50;
		GlobalParams::bpm = (uint16_t) bpm;
		if (prevBPM != GlobalParams::bpm) {
			lcd.updateBPM();
		}
		prevBPM = GlobalParams::bpm;

		// Sequencer reset
		bool resetValue;
		resetPin.readValue(&resetValue);
		if (resetValue) {
			if (resetTimer.get_s() > 2.5) {
				sequencer.resetAll();
			}
			else if (sequencer.isAcceptingInput()) {
				sequencer.setReset();
			}
			else {
				sequencer.syncLayers();
			}
		}
		else {
			resetTimer.set();
		}

		// Sequencer ratchet
		if (ratchetButton.wasClicked()) {
			sequencer.setRatchet(&lcd);
		}

		if (tieButton.wasClicked()) {
			sequencer.setTie(&lcd);
		}

		// Exit and sequencer rest
		bool restValue;
		restPin.readValue(&restValue);
		if (restValue) {
			if (restTimer.get_s() > 2.5) {
				break;
			}
			if (sequencer.isAcceptingInput()) {
				sequencer.setRest();
			}
		}
		else {
			restTimer.set();	
		}
	}

	lcd.clear();
	lcd.returnHome();
	lcd.writeStr("Goodbye!");

	usleep(3000000);
	lcd.clear();
	lcd.returnHome();
*/
	return 0;
}

