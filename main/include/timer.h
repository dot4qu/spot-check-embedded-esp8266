#ifndef TIMER_H
#define TIMER_H

#define RELOAD_ON_EXPIRATION true
#define TIMER_PERIOD_MS (1000)

volatile bool timer_expired;
volatile int timer_count;

// Does NOT start timer, must use reset_timer to start count
void init_timer();

// Reset timer and begin counting up to period again.
// Clears interrupt flag as well
void reset_timer();

#endif
