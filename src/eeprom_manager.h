#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// Адреса в EEPROM
#define EEPROM_SETTINGS_ADDR 0
#define EEPROM_SETTINGS_SIZE sizeof(MotorSettings)

// Функции для работы с EEPROM
bool saveMotorSettings(const MotorSettings& settings);
bool loadMotorSettings(MotorSettings& settings);
void initMotorSettingsFromEEPROM();
void printMotorSettings();

extern MotorSettings currentSettings;



