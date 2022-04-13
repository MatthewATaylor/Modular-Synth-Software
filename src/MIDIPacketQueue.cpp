#include "../include/MIDIPacketQueue.h"

MIDIPacketQueue::MIDIPacketQueue() {
	queue = new uint8_t*[BUFFER_SIZE];
	for (uint8_t i = 0; i < BUFFER_SIZE; ++i) {
		queue[i] = new uint8_t[PACKET_SIZE];
	}
	capacity = BUFFER_SIZE;
}

void MIDIPacketQueue::push(uint8_t *packet) {
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

void MIDIPacketQueue::deallocate() {
	for (uint8_t i = 0; i < capacity; ++i) {
		delete[] queue[i];
	}
	delete[] queue;
}

void MIDIPacketQueue::reset() {
	deallocate();
	capacity = 0;
	length = 0;
}

void MIDIPacketQueue::clear() {
	// Only leave a capacity of BUFFER_SIZE remaining
	for (uint8_t i = BUFFER_SIZE; i < capacity; ++i) {
		delete[] queue[i];
	}
	capacity = BUFFER_SIZE;
	length = 0;
}

size_t MIDIPacketQueue::getLength() const {
	return length;
}

const uint8_t *MIDIPacketQueue::getPacket(size_t index) const {
	return queue[index];
}

MIDIPacketQueue::~MIDIPacketQueue() {
	deallocate();
}

