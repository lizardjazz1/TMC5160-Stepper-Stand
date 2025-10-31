#include "eeprom_manager.h"
#include "tmc.h"

MotorSettings currentSettings;

// Простая функция для вычисления checksum
uint32_t calculateChecksum(const MotorSettings& settings) {
    uint32_t sum = 0;
    sum += settings.current_mA;
    sum += settings.microsteps;
    sum += settings.max_speed;
    sum += settings.acceleration;
    sum += settings.deceleration;
    sum += settings.steps_per_rev;
    sum += (uint32_t)(settings.hold_multiplier * 1000); // float -> int для checksum
    sum += settings.control_mode;
    return sum;
}

bool saveMotorSettings(const MotorSettings& settings) {
    // Вычисляем checksum
    MotorSettings settingsToSave = settings;
    settingsToSave.checksum = calculateChecksum(settings);

    // Сохраняем в EEPROM
    EEPROM.put(EEPROM_SETTINGS_ADDR, settingsToSave);
    EEPROM.commit();

    add_log("💾 Motor settings saved to EEPROM");

    return true;
}

bool loadMotorSettings(MotorSettings& settings) {
    // Загружаем из EEPROM
    EEPROM.get(EEPROM_SETTINGS_ADDR, settings);

    // Проверяем checksum
    uint32_t expectedChecksum = calculateChecksum(settings);

    if (settings.checksum != expectedChecksum) {
        add_log("⚠️ Invalid EEPROM checksum, using defaults");
        return false;
    }

    add_log("📚 Motor settings loaded from EEPROM");

    return true;
}

void initMotorSettingsFromEEPROM() {
    // Пытаемся загрузить настройки из EEPROM
    if (!loadMotorSettings(currentSettings)) {
        // Если не удалось загрузить, используем значения по умолчанию
        currentSettings.current_mA = 800;              // 800mA (NEMA17)
        currentSettings.hold_multiplier = 0.5f;        // 50% hold
        currentSettings.microsteps = DEFAULT_MICROSTEPS;
        currentSettings.max_speed = 400;               // Как в тесте!
        currentSettings.acceleration = 500;            // Как в тесте!
        currentSettings.deceleration = DEFAULT_DECEL;
        currentSettings.steps_per_rev = 200;           // БЕЗ микрошагов! (драйвер применяет их автоматически)
        currentSettings.control_mode = DEFAULT_CONTROL_MODE;
        currentSettings.gear_ratio = 1.0f;             // По умолчанию 1:1 (прямая передача)
        currentSettings.stallguard_threshold = 0;      // StallGuard выключен по умолчанию

        // Сохраняем значения по умолчанию
        saveMotorSettings(currentSettings);
        add_log("🔄 Using default motor settings (NEMA17 800mA)");
    }

    printMotorSettings();
}

void printMotorSettings() {
    add_log("⚙️ Current motor settings:");
    add_log("  Current: " + String(currentSettings.current_mA) + " mA");
    add_log("  Hold multiplier: " + String(currentSettings.hold_multiplier, 2));
    add_log("  Microsteps: " + String(currentSettings.microsteps));
    add_log("  Steps per rev: " + String(currentSettings.steps_per_rev));
    add_log("  Max speed: " + String(currentSettings.max_speed) + " steps/s");
    add_log("  Acceleration: " + String(currentSettings.acceleration) + " steps/s²");
    add_log("  Deceleration: " + String(currentSettings.deceleration) + " steps/s²");
    add_log("  Control mode: " + String(currentSettings.control_mode == MODE_MOTION_CONTROLLER ? "Motion Controller" : "STEP/DIR"));
}
