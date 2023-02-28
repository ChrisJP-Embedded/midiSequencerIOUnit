#include "driver/gptimer.h"
#include "driver/uart.h"

#define MIDI_UART_NUM 1

extern volatile bool deltaTimerFired;

void initSystemLowLevel(void);
void startDeltaTimer(uint32_t deltaTime);