#pragma once
#include <Arduino.h>

// Чтение датчиков Холла AH3134
// Датчики работают "активный LOW" - при поднесении магнита = LOW

// Инициализация датчиков Холла
void init_hall_sensors();

// Чтение состояния датчика 1
// Возвращает: true = магнит обнаружен (LOW), false = магнит отсутствует (HIGH)
bool read_hall_sensor_1();

// Чтение состояния датчика 2
// Возвращает: true = магнит обнаружен (LOW), false = магнит отсутствует (HIGH)
bool read_hall_sensor_2();

// Получить JSON строку с состоянием обоих датчиков
String get_hall_sensors_json();

