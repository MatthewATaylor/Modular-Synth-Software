#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

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
			return value / 2047.0 * 2.048;
		}

		/*
		 * Get pot position (0 to 1) given voltage level, needed because of voltage divider on pot output
		 */
		static double getPotPosition(double v) {
			// Equation represents the inverse of the circuit's transfer function
			return (500 * v - 33 + sqrt(290000 * v * v - 33000 * v + 1089)) / (1000.0 * v);
		}
};

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

	private:
		uint8_t pinValues[2] = {0b00000000, 0b00000000};
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

			ePin.writeValue(0);
			rsPin.writeValue(0);

			writeData(0, 0, 1, 1);
			usleep(4500);

			writeData(0, 0, 1, 1);
			usleep(4500);

			writeData(0, 0, 1, 1);
			usleep(150);

			writeData(0, 0, 1, 0);  // 4-bit mode

			writeData(0, 0, 1, 0);
			writeData(1, 0, 0, 0);  // 2 lines, 5x8 dots

			writeData(0, 0, 0, 0);
			writeData(1, 0, 0, 0);  // Display off

			writeData(0, 0, 0, 0);
			writeData(0, 0, 0, 1);  // Display clear

			writeData(0, 0, 0, 0);
			writeData(0, 1, 1, 0);  // Entry mode set (increment on write, no shift)


			writeData(0, 0, 0, 0);
			writeData(1, 1, 0, 0);  // Display on

			writeData(0, 0, 0, 0);
			writeData(0, 0, 1, 0);  // Set cursor to home position
			usleep(2000);

			rsPin.writeValue(1);
			
			writeData(0, 1, 1, 0);
			writeData(1, 0, 0, 0);  // h

			writeData(0, 1, 1, 0);
			writeData(1, 0, 0, 1);
		}

	private:
		DigitalOutputPin ePin, rsPin, rwPin, db4Pin, db5Pin, db6Pin, db7Pin;
		
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
		OutputManager(int i2cFile) : dac(i2cFile), gpio(i2cFile) {
			gpio.open(0b0100010);
			gpio.pinMode(GPIOExpander::Port::A, 0);
		}	

		void pressKey(uint8_t keyID) {
			uint8_t channelToWrite = NUM_CHANNELS;
			bool keyIDTaken = false;
			for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
				if (!channels[i].isOn && channelToWrite == NUM_CHANNELS) {
					channelToWrite = i;
				}
				if (channels[i].isOn && channels[i].keyID == keyID) {
					keyIDTaken = true;
					break;
				}
			}
			if (keyIDTaken || channelToWrite == NUM_CHANNELS) {
				return;
			}
			
			double outputVoltage = (keyID - 53) / 12.0;
			uint16_t dacValue = (uint16_t) (outputVoltage / 5.0 * 4095);
			
			dac.open(DAC_ADDR);
			dac.writeData(dacValue, DAC::Command::WRITE_UPDATE, channelToWrite);
			
			gpio.open(GPIO_EXPANDER_ADDR);
			gpio.writePin(GPIOExpander::Port::A, channelToWrite, 1);
			
			channels[channelToWrite].isOn = true;
			channels[channelToWrite].keyID = keyID;
		}

		void releaseKey(uint8_t keyID) {
			gpio.open(GPIO_EXPANDER_ADDR);
			for (uint8_t i = 0; i < NUM_CHANNELS; ++i) {
				if (channels[i].isOn && channels[i].keyID == keyID) {
					gpio.writePin(GPIOExpander::Port::A, i, 0);
					channels[i].isOn = false;
				}
			}
		}

	private:
		struct OutputChannel {
			bool isOn = false;
			uint8_t keyID = 0;
			unsigned int triggerMillis = 0;
		};

		static const uint8_t NUM_CHANNELS = 8;
		static const uint8_t GPIO_EXPANDER_ADDR = 0b0100010;
		static const uint8_t DAC_ADDR = 0b1001000;

		OutputChannel channels[NUM_CHANNELS];
		DAC dac;
		GPIOExpander gpio;
};	

MIDIPacketQueue midiQueue;
pthread_t midiThread;
pthread_mutex_t midiLock;

void *midiRead(void *arg) {
	int descriptor = open("/dev/midi1", O_RDONLY);
	if (descriptor == -1) {
		printf("Error: Failed to open MIDI device\n");
		return nullptr;
	}
	while (true) {
		uint8_t packet[4];
		if (read(descriptor, packet, 4) == -1) {
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
	
	OutputManager outputManager(i2cFile);
	
	LCD lcd(12, 13, 4, 5, 6, 7);
	lcd.setup();

	midiInit();

	while (true) {
		pthread_mutex_lock(&midiLock);
		for (size_t i = 0; i < midiQueue.getLength(); ++i) {
			const uint8_t *packet = midiQueue.getPacket(i);
			if (packet[0] == 144) {
				outputManager.pressKey(packet[1]);
			}
			else if (packet[0] == 128) {
				outputManager.releaseKey(packet[1]);
			}
		}
		midiQueue.clear();
		pthread_mutex_unlock(&midiLock);
	}

	return 0;
}

