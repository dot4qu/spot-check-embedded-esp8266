#ifndef TIMER_H
#define TIMER_H

#include "constants.h"

#define RELOAD_ON_EXPIRATION true

#ifndef BUTTON_FOR_REQUESTS
#assert "Need to define BUTTON_FOR_REQUESTS as true or false to send request on button press (true) or timer periodically (false)"
#endif

#if BUTTON_FOR_REQUESTS
#define TIMER_PERIOD_MS (100)
#else
#define TIMER_PERIOD_MS (1000)
#endif

// Used for debouncing
volatile bool timer_expired;

// Used for debugging to send requests periodically
volatile int timer_count;

// Does NOT start timer, must use reset_timer to start count
void init_timer();

// Reset timer and begin counting up to period again.
// Clears interrupt flag as well
void reset_timer();

#endif
