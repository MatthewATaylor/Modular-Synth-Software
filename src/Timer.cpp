#include "../include/Timer.h"

Timer::Timer() {
	set();
}

void Timer::set() {
	clock_gettime(CLOCK_MONOTONIC, &setTime);
}

double Timer::get_s() {
	struct timespec currentTime;
	clock_gettime(CLOCK_MONOTONIC, &currentTime);
	return currentTime.tv_sec - setTime.tv_sec + (currentTime.tv_nsec - setTime.tv_nsec) / 1000000000.0;
}

double Timer::get_ms() {
	return get_s() * 1000.0;
}

