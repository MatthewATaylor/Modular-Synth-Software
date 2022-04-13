#ifndef MIDI_PACKET_QUEUE_H
#define MIDI_PACKET_QUEUE_H

#include <stdint.h>
#include <string.h>

class MIDIPacketQueue {
	public:
		const static uint8_t PACKET_SIZE = 4;

		MIDIPacketQueue();
		
		void push(uint8_t *packet);
		void deallocate();
		void reset();
		void clear();
		size_t getLength() const;
		const uint8_t *getPacket(size_t index) const;
		
		~MIDIPacketQueue();

	private:
		const static uint8_t BUFFER_SIZE = 8;

		uint8_t **queue = nullptr;
		size_t capacity = 0;
		size_t length = 0;
};

#endif

