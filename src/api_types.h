#pragma once

// ============================================================================
// ТИПЫ ДАННЫХ ДЛЯ API
// ============================================================================

// Структура для команды движения
struct MoveCommand {
    long steps = 200;
    String direction = "forward";
    uint32_t max_speed = 1000;
    uint16_t acceleration = 500;
    uint16_t deceleration = 500;
    uint16_t driver_current = 800;
    float driver_hold_mult = 0.5f;
    uint16_t driver_microsteps = 16;
};

// Структура для статуса драйвера
struct DriverStatus {
    bool initialized = false;
    bool enabled = false;
    bool is_moving = false;
    int32_t current_position = 0;
    int32_t target_position = 0;
    int32_t steps_remaining = 0;
    int32_t current_speed = 0;
    uint32_t max_speed_set = 1000;
    uint16_t accel_set = 500;
    uint16_t decel_set = 500;
    uint16_t steps_per_rev = 3200;
    bool stall_guard = false;
    bool short_circuit = false;
    bool overtemp = false;
    uint16_t current_mA = 0;
    String driver_status_text = "Не инициализирован";
};

// Структура для пресета двигателя
struct MotorPreset {
    String name;
    String description;
    uint16_t current_mA;        // Ток в миллиамперах (удобно для пользователя!)
    float hold_mult;            // Множитель hold тока (0.1-1.0)
    uint16_t microsteps;        // Количество микрошагов
    uint32_t max_speed;         // Макс. скорость (шаг/с)
    uint16_t acceleration;      // Ускорение (шаг/с²)
    uint16_t deceleration;      // Замедление (шаг/с²)
    uint16_t steps_per_rev;     // Шагов на оборот
};

// Пресеты для разных типов двигателей
// ⚠️ steps_per_rev = 200 (БЕЗ микрошагов!) - микрошаги применяются драйвером автоматически!
const MotorPreset NEMA_PRESETS[] = {
    // NEMA 8 - малый двигатель
    {
        "NEMA 8",
        "Малый двигатель (20x20мм), 0.6-1.0А (600mA)",
        600,        // 600mA (типичный ток для NEMA8)
        0.3f,       // 30% hold ток (меньше для малых моторов)
        32,         // 32 микрошага (высокая точность)
        300,        // 300 шаг/с (медленнее для малых моторов)
        400,        // 400 шаг/с²
        400,        // 400 шаг/с²
        200         // 200 шагов/оборот (БЕЗ микрошагов!)
    },
    
    // NEMA 14 - средний двигатель
    {
        "NEMA 14",
        "Средний двигатель (35x35мм), 0.8-1.0А (700mA)",
        700,        // 700mA (типичный ток для NEMA14)
        0.4f,       // 40% hold ток
        16,         // 16 микрошагов
        350,        // 350 шаг/с
        450,        // 450 шаг/с²
        450,        // 450 шаг/с²
        200         // 200 шагов/оборот (БЕЗ микрошагов!)
    },
    
    // NEMA 17 - популярный, универсальный
    {
        "NEMA 17",
        "Популярный двигатель (42x42мм), 0.4-1.7А (800mA)",
        800,        // 800mA (будет конвертировано в irun=11, ihold=5, globalScaler=128)
        0.5f,       // 50% hold ток
        16,         // 16 микрошагов
        400,        // 400 шаг/с (как в тесте!)
        500,        // 500 шаг/с² (как в тесте!)
        500,        // 500 шаг/с²
        200         // 200 шагов/оборот (БЕЗ микрошагов!)
    },
    
    // NEMA 23 - большой ток, высокий момент
    {
        "NEMA 23",
        "Большой двигатель (57x57мм), 2.8-4.2А (2500mA)",
        2500,       // 2500mA (безопасное значение для большинства NEMA23)
        0.6f,       // 60% hold ток
        8,          // 8 микрошагов
        600,        // 600 шаг/с
        500,        // 500 шаг/с²
        500,        // 500 шаг/с²
        200         // 200 шагов/оборот (БЕЗ микрошагов!)
    }
};

const int NEMA_PRESETS_COUNT = sizeof(NEMA_PRESETS) / sizeof(NEMA_PRESETS[0]);