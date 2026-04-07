#ifndef NICLA_OLED_H
#define NICLA_OLED_H

#include <stdint.h> 
#include "Nicla_System.h"

extern unsigned long lastSwitch;
extern int currentScreen;
extern int frame;
extern int daysGoalReached;

void initOLED();
void updateDisplay(uint32_t steps, bool galvanicWorn, bool isWorn, String time);

#endif
