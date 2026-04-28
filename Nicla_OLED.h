#ifndef NICLA_OLED_H
#define NICLA_OLED_H

#include <stdint.h> 
#include "Nicla_System.h"

extern unsigned long lastSwitch;
extern int currentScreen;
extern int frame;

void initOLED();
void updateDisplay(uint32_t steps, bool isWorn, String time, uint32_t wornSeconds, int goalHours, int currentStreak, int longestStreak, int batteryLevel);
void displayWarningScreen();
void clearDisplay();

#endif
