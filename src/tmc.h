#pragma once
#include <Arduino.h>
#include <TMC5160.h>  // ✅ Новая библиотека!
#include <SPI.h>
#include "pins.h"
#include "config.h"
#include "api_types.h"

// Глобальные переменные для TMC5160
extern TMC5160_SPI *motor_ptr;  // Указатель на объект
extern bool tmc_initialized;
extern bool motor_enabled;
extern volatile bool stallguard_triggered;  // Флаг срабатывания StallGuard

// Макрос для удобства обращения к motor
#define motor (*motor_ptr)

// Основные функции
void add_log(String message);
bool setup_tmc5160(uint16_t current_mA, float hold_multiplier, uint16_t microsteps,
                   uint32_t max_speed, uint16_t accel, uint16_t decel);
void move_motor_steps(int32_t steps, MotorControlMode mode);
void enable_motor();
void disable_motor();

// Функции валидации
bool validate_current(uint16_t current_mA);
bool validate_speed(uint32_t speed);
bool validate_acceleration(uint16_t accel);

// Функции для loop()
void run_motor();
void update_center_sequence();
bool position_reached();

// Дополнительные функции
void start_center_sequence();
void stop_center_sequence();
String get_movement_status();
int32_t get_current_speed();
uint32_t get_driver_status();
String get_detailed_diagnostics();

// Управление током (можно менять в реальном времени!)
void set_motor_current(uint16_t current_mA, float hold_multiplier);
uint16_t get_motor_current();
String get_current_diagnostics();

// Тестовая функция
void test_step_dir_mode();

// StallGuard функции
void setup_stallguard(int8_t threshold);
void IRAM_ATTR stallguard_isr();
bool is_stallguard_triggered();
