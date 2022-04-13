#ifndef TIME_H
#define TIME_H

#include <sys/time.h>
#include <time.h>

class Timer {
	public:
		Timer();
		void set();
		double get_s();
		double get_ms();

	private:
		struct timespec setTime;
};

#endif

