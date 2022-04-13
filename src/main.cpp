#include <alsa/asoundlib.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "../include/LCD.h"
#include "../include/MIDIPacketQueue.h"
#include "../include/DebouncedButton.h"
#include "../include/OutputManager.h"

MIDIPacketQueue midiQueue;
pthread_t midiThread;
pthread_mutex_t midiLock;
const uint8_t MIDI_PACKET_SIZE = 3;

void *midiRead(void *arg) {
	(void)arg;

	snd_rawmidi_t *midi = nullptr;
	if (snd_rawmidi_open(&midi, nullptr, "hw:0,0", SND_RAWMIDI_SYNC) < 0) {
		printf("Error: Failed to open MIDI port\n");
		return nullptr;
	}

	printf("Successfully opened MIDI port\n");
	
	uint8_t bytesInPacket = 0;
	uint8_t packet[MIDI_PACKET_SIZE];
	uint8_t buffer[1];
	while (true) {
		if (snd_rawmidi_read(midi, buffer, 1) < 0) {
			printf("Error: Failed to read MIDI input\n");
			continue;
		}
		if (buffer[0] < 0b11110000) {
			// Channel voice message
			if (bytesInPacket == 0) {
				if (buffer[0] >= 0b10000000) {
					// Command byte
					packet[0] = buffer[0];
					bytesInPacket = 1;
				}
			}
			else {
				if (buffer[0] < 0b10000000) {
					// Data byte
					packet[bytesInPacket] = buffer[0];
					++bytesInPacket;
				}
			}
		}
		if (bytesInPacket == MIDI_PACKET_SIZE) {
			pthread_mutex_lock(&midiLock);
			midiQueue.push(packet);
			pthread_mutex_unlock(&midiLock);
			bytesInPacket = 0;
		}
	}

	snd_rawmidi_close(midi);

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
	int i2cFile = open("/dev/i2c-1", O_RDWR);
	if (i2cFile < 0) {
		printf("Error: Failed to open I2C bus\n");
		return 1;
	}

	LCD lcd(4, 5, 6, 7, 8, 9);
	lcd.setup();

	DebouncedButton outputButton(16);
	DebouncedButton channelButton(17);

	OutputManager outManager(i2cFile, &lcd, &outputButton, &channelButton);

	midiInit();

	while (true) {
		pthread_mutex_lock(&midiLock);
		for (size_t i = 0; i < midiQueue.getLength(); ++i) {			
			const uint8_t *packet = midiQueue.getPacket(i);
			uint8_t command = packet[0] >> 4;
			uint8_t channel = packet[0] & 0b00001111;
			if (command == 0b1001) {
				// Key pressed
				printf("Key pressed on channel %d (%d, %d)\n", channel, packet[1], packet[2]);
				outManager.pressKey(packet[1], channel);
			}
			else if (command == 0b1000) {
				// Key released
				printf("Key released on channel %d (%d, %d)\n", channel, packet[1], packet[2]);
				outManager.releaseKey(packet[1], channel);
			}
			else if (command == 0b1110) {
				// Pitch bend
				printf("Pitch bend on channel %d\n", channel);
			}
			else if (command == 0b1011 && packet[1] > 122) {
				printf("Turning all notes off on channel %d\n", channel);
				outManager.turnOffChannel(channel);
			}
			else {
				printf("Unknown command %d on channel %d (%d, %d)\n", command, channel, packet[1], packet[2]);
			}
		}
		midiQueue.clear();
		pthread_mutex_unlock(&midiLock);

		outManager.updateTriggers();
		outManager.updateSelectedOutput();
		outManager.updateChannelAssignments();

		if (outputButton.isPressed() && channelButton.isPressed() && outputButton.getHoldTime_s() > 5 && channelButton.getHoldTime_s() > 5) {
			lcd.clear();
			lcd.returnHome();
			lcd.writeStr("Exiting...");
			usleep(3000000);
			break;
		}

		usleep(100);
	}

	for (uint8_t i = 0; i < 8; ++i) {
		outManager.turnOffChannel(i);
	}

	lcd.clear();
	lcd.returnHome();

	return 0;
}

