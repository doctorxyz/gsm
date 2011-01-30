#ifndef TIMER_H
#define TIMER_H

#include <tamtypes.h>

/* timer.c */
extern u64 WaitTime;
extern u64 CurrTime;

void TimerInit(void);
u64  Timer(void);
void TimerEnd(void);

#endif //TIMER_H
