#pragma once

// --- TMC5160 Hardware Configuration ---
// ⚠️ КРИТИЧЕСКИ ВАЖНО: R_SENSE должен соответствовать физическому резистору на плате!
// Для TMC5160 Pro V1.5 (BigTreeTech/Watterott) = 0.033 Ом (согласно даташиту!)
#define TMC5160_RSENSE 0.033f  // Sense resistor value (DO NOT CHANGE unless hardware is different!)

// --- Режимы работы TMC5160 ---
enum MotorControlMode {
    MODE_MOTION_CONTROLLER = 0,  // SPI Motion Controller (RAMPMODE=0, плавное движение)
    MODE_STEP_DIR = 1            // STEP/DIR режим (RAMPMODE=3, классический)
};

// --- Параметры по умолчанию ---
// ⚠️ ВАЖНО: steps_per_rev = 200 (БЕЗ микрошагов!) - драйвер применяет микрошаги автоматически!
#define DEFAULT_STEPS_PER_REV 200
#define DEFAULT_MAX_SPEED 1000
#define DEFAULT_ACCEL 500
#define DEFAULT_DECEL 500
#define DEFAULT_CURRENT_MA 800
#define DEFAULT_MICROSTEPS 16
#define DEFAULT_HOLD_MULTIPLIER 0.5f
#define DEFAULT_CONTROL_MODE MODE_MOTION_CONTROLLER
#define EEPROM_SIZE 512
#define MAX_PROFILES 5
#define PROFILE_SIZE 100

// --- Лимиты безопасности ---
#define MAX_CURRENT_MA 3000        // Максимальный ток для TMC5160 (зависит от охлаждения)
#define MIN_CURRENT_MA 100         // Минимальный ток для работы
#define MAX_SPEED_STEPS 50000      // Максимальная скорость (шаги/с)
#define MAX_ACCELERATION 10000     // Максимальное ускорение (шаги/с²)

// Структура для хранения настроек в EEPROM
struct MotorSettings {
    uint16_t current_mA;        // Ток в мА (удобно для пользователя!)
    float hold_multiplier;      // Множитель hold тока (0.0-1.0)
    uint16_t microsteps;        // Микрошаги (1, 2, 4, 8, 16, 32, 64, 128, 256)
    uint32_t max_speed;         // Скорость (steps/s)
    uint16_t acceleration;      // Ускорение (steps/s²)
    uint16_t deceleration;      // Замедление (steps/s²)
    uint16_t steps_per_rev;     // Шагов на оборот (200*microsteps)
    uint8_t control_mode;       // Режим управления (MotorControlMode)
    float gear_ratio;           // Передаточное число (например: 3.0 = 1:3, 0.333 = 3:1)
    int8_t stallguard_threshold; // Порог StallGuard (-64 до 63, 0 = выкл)
    uint32_t checksum;          // Для проверки валидности данных
};

// --- Wi-Fi ---
#define WIFI_AP_SSID "Krya"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_IP "192.168.4.1"