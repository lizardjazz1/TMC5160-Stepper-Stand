#include <Arduino.h>
#include <TMC5160.h>
#include <SPI.h>
#include "tmc.h"
#include "pins.h"
#include "config.h"
#include "api_types.h"
#include "eeprom_manager.h"

// Глобальные переменные
TMC5160_SPI *motor_ptr = nullptr;  // Указатель для динамического создания (extern в tmc.h)
bool tmc_initialized = false;
bool motor_enabled = false;
volatile bool stallguard_triggered = false;  // Флаг срабатывания StallGuard

// Переменные для последовательности от центра
bool center_sequence_active = false;
unsigned long sequence_start_time = 0;
int sequence_step = 0;
const int sequence_delay = 2000;

// Функция логирования
void add_log(String message) {
    Serial.println(message);
}

// ===== ФУНКЦИИ ВАЛИДАЦИИ =====

bool validate_current(uint16_t current_mA) {
    if (current_mA < MIN_CURRENT_MA) {
        add_log("⚠️ Current too low: " + String(current_mA) + "mA (min: " + String(MIN_CURRENT_MA) + "mA)");
        return false;
    }
    if (current_mA > MAX_CURRENT_MA) {
        add_log("⚠️ Current too high: " + String(current_mA) + "mA (max: " + String(MAX_CURRENT_MA) + "mA)");
        return false;
    }
    return true;
}

bool validate_speed(uint32_t speed) {
    if (speed > MAX_SPEED_STEPS) {
        add_log("⚠️ Speed too high: " + String(speed) + " steps/s (max: " + String(MAX_SPEED_STEPS) + ")");
        return false;
    }
    return true;
}

bool validate_acceleration(uint16_t accel) {
    if (accel > MAX_ACCELERATION) {
        add_log("⚠️ Acceleration too high: " + String(accel) + " steps/s² (max: " + String(MAX_ACCELERATION) + ")");
        return false;
    }
    return true;
}

// ===== РАСЧЁТ ТОКА TMC5160 (ПРАВИЛЬНАЯ ФОРМУЛА!) =====

void calculate_current_settings(uint16_t current_mA, float hold_multiplier,
                                 uint8_t *out_irun, uint8_t *out_ihold, uint16_t *out_globalScaler) {
    // Используем GLOBAL_SCALER = 128 (проверенное значение из теста!)
    *out_globalScaler = 128;
    
    // Формула из даташита TMC5160 (§9):
    // I_rms = (CS+1)/32 * (GLOBAL_SCALER/256) * V_fs/(R_SENSE+0.02Ω) / sqrt(2)
    // V_fs = 0.325V (внутреннее опорное напряжение)
    // R_SENSE = 0.033Ω
    
    // Перевернём формулу для нахождения CS:
    // CS = (I_rms * 32 * 256 * sqrt(2) * (R_SENSE+0.02)) / (GLOBAL_SCALER * V_fs) - 1
    
    const float V_fs = 0.325;
    const float R_SENSE_TOTAL = TMC5160_RSENSE + 0.02;  // 0.033 + 0.02 = 0.053
    const float I_rms_A = current_mA / 1000.0;
    
    float CS_float = (I_rms_A * 32.0 * 256.0 * 1.41421 * R_SENSE_TOTAL) / (*out_globalScaler * V_fs) - 1.0;
    
    // Ограничиваем CS в диапазоне 0-31
    *out_irun = constrain((int)(CS_float + 0.5), 0, 31);
    
    // Hold ток = irun * hold_multiplier
    *out_ihold = constrain((int)(*out_irun * hold_multiplier + 0.5), 0, 31);
}

// ===== ИНИЦИАЛИЗАЦИЯ TMC5160 =====

bool setup_tmc5160(uint16_t current_mA, float hold_multiplier, uint16_t microsteps,
                   uint32_t max_speed, uint16_t accel, uint16_t decel) {

    Serial.println("=== TMC5160 SETUP ===");

    // 1. pinMode для EN (ВЫКЛЮЧЕН по умолчанию!)
    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);  // ВЫКЛЮЧЕН (активный LOW, значит HIGH = выключен)
    Serial.println("✅ EN pin configured (motor DISABLED)");

    // 2. РАССЧИТЫВАЕМ irun, ihold, globalScaler из current_mA
    uint8_t irun, ihold;
    uint16_t globalScaler;
    calculate_current_settings(current_mA, hold_multiplier, &irun, &ihold, &globalScaler);
    Serial.print("✅ Current: ");
    Serial.print(current_mA);
    Serial.print("mA → irun=");
    Serial.print(irun);
    Serial.print(", ihold=");
    Serial.print(ihold);
    Serial.print(", globalScaler=");
    Serial.println(globalScaler);

    // 3. PowerStageParameters (DEFAULTS!)
    TMC5160::PowerStageParameters powerStageParams;  // defaults

    // 4. MotorParameters (ПРЯМЫЕ ЗНАЧЕНИЯ!)
    TMC5160::MotorParameters motorParams;
    motorParams.globalScaler = globalScaler;
    motorParams.irun = irun;
    motorParams.ihold = ihold;

    // 5. SPI.begin()
    SPI.begin();
    Serial.println("✅ SPI.begin() called");

    // 6. Создаём объект ПОСЛЕ SPI.begin() с 100kHz SPI
    if (motor_ptr == nullptr) {
        motor_ptr = new TMC5160_SPI(CS_PIN, 12000000, SPISettings(100000, MSBFIRST, SPI_MODE3), SPI);
        Serial.println("✅ motor object created (SPI 100kHz)");
    }

    // 7. motor.begin()
    motor.begin(powerStageParams, motorParams, TMC5160::NORMAL_MOTOR_DIRECTION);
    Serial.println("✅ motor.begin() called");

    // 8. УСТАНАВЛИВАЕМ МИКРОШАГИ ВРУЧНУЮ! (библиотека tommag не имеет API для этого)
    // microsteps -> mres value: 256=0, 128=1, 64=2, 32=3, 16=4, 8=5, 4=6, 2=7, 1=8
    uint8_t mres = 8; // по умолчанию FULLSTEP
    if (microsteps == 256) mres = 0;
    else if (microsteps == 128) mres = 1;
    else if (microsteps == 64) mres = 2;
    else if (microsteps == 32) mres = 3;
    else if (microsteps == 16) mres = 4;
    else if (microsteps == 8) mres = 5;
    else if (microsteps == 4) mres = 6;
    else if (microsteps == 2) mres = 7;
    else if (microsteps == 1) mres = 8;
    
    // Читаем текущий CHOPCONF
    uint32_t chopconf = motor.readRegister(TMC5160_Reg::CHOPCONF);
    // Устанавливаем mres (биты 24-27)
    chopconf &= ~(0x0F << 24); // Очищаем биты mres
    chopconf |= (mres << 24);   // Устанавливаем новое значение
    motor.writeRegister(TMC5160_Reg::CHOPCONF, chopconf);
    Serial.print("✅ Microsteps set: ");
    Serial.print(microsteps);
    Serial.print(" (mres=");
    Serial.print(mres);
    Serial.println(")");

    // 9. ramp definition
    motor.setRampMode(TMC5160::POSITIONING_MODE);
    motor.setMaxSpeed(max_speed);
    motor.setAcceleration(accel);
    Serial.print("✅ Ramp: VMAX=");
    Serial.print(max_speed);
    Serial.print(", AMAX=");
    Serial.println(accel);

    Serial.println("starting up");

    // 10. СБРАСЫВАЕМ ПОЗИЦИЮ В 0!
    motor.writeRegister(TMC5160_Reg::XACTUAL, 0);
    motor.writeRegister(TMC5160_Reg::XTARGET, 0);
    Serial.println("✅ Position reset to 0");

    // 11. delay(1000) для автонастройки
    delay(1000);
    Serial.println("✅ Calibration complete");

    tmc_initialized = true;
    return true;
}

// ===== ФУНКЦИЯ ДВИЖЕНИЯ =====

void move_motor_steps(int32_t steps, MotorControlMode mode) {
    if (!tmc_initialized) {
        add_log("❌ TMC5160 not initialized!");
        return;
    }

    if (!motor_enabled) {
        add_log("❌ Motor is disabled!");
        return;
    }

    add_log("🚀 Moving: " + String(steps) + " steps (" + 
            String(mode == MODE_MOTION_CONTROLLER ? "SPI" : "STEP/DIR") + ")");

    if (mode == MODE_MOTION_CONTROLLER) {
        // Motion Controller Mode (КАК В ПРИМЕРЕ!)
        float current_pos = motor.getCurrentPosition();
        float target_pos = current_pos + steps;
        
        motor.setTargetPosition(target_pos);  // КАК В ПРИМЕРЕ!
        add_log("✅ SPI Motion: steps=" + String(steps) + ", target=" + String(target_pos));
        
    } else {
        // STEP/DIR Mode
        bool forward = (steps >= 0);
        int32_t abs_steps = abs(steps);
        digitalWrite(DIR_PIN, forward ? HIGH : LOW);
        
        // Плавный разгон/торможение
        uint32_t min_delay = 160;
        uint32_t max_delay = 1000;
        uint32_t accel_steps = 200;
        uint32_t decel_steps = 200;
        
        for (int32_t i = 0; i < abs_steps; i++) {
            uint32_t delay_us;
            if (i < accel_steps) {
                delay_us = max_delay - ((max_delay - min_delay) * i / accel_steps);
            } else if (i > abs_steps - decel_steps) {
                int32_t decel_i = abs_steps - i;
                delay_us = max_delay - ((max_delay - min_delay) * decel_i / decel_steps);
            } else {
                delay_us = min_delay;
            }
            
        digitalWrite(STEP_PIN, HIGH);
            delayMicroseconds(delay_us);
        digitalWrite(STEP_PIN, LOW);
            delayMicroseconds(delay_us);
        }
        add_log("✅ Completed: " + String(abs_steps) + " steps");
    }
}

// ===== ENABLE/DISABLE =====

void enable_motor() {
    if (!tmc_initialized) {
        add_log("❌ TMC5160 not initialized!");
        return;
    }

    motor.enable();  // КАК В ПРИМЕРЕ!
    digitalWrite(EN_PIN, LOW);  // Активный LOW
    motor_enabled = true;
    add_log("✅ Motor enabled");
}

void disable_motor() {
    if (!tmc_initialized) return;

    motor.disable();  // КАК В ПРИМЕРЕ!
    digitalWrite(EN_PIN, HIGH);
    motor_enabled = false;
    add_log("❌ Motor disabled");
}

// ===== LOOP FUNCTIONS =====

void run_motor() {
    // Не используется в STEP/DIR режиме
}

void test_step_dir_mode() {
    if (!tmc_initialized || !motor_enabled) {
        add_log("❌ TMC5160 not ready for test!");
        return;
    }
    
    add_log("🧪 Testing STEP/DIR mode...");
    
    // Тест вперёд
    add_log("🧪 Test: 100 steps FORWARD");
    digitalWrite(DIR_PIN, HIGH);
    delay(10);
    
    for (int i = 0; i < 100; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(160);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(160);
    }
    
    add_log("✅ Forward test complete");
    delay(1000);
    
    // Тест назад
    add_log("🧪 Test: 100 steps BACKWARD");
    digitalWrite(DIR_PIN, LOW);
    delay(10);
    
    for (int i = 0; i < 100; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(160);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(160);
    }
    
    add_log("✅ Backward test complete");
    add_log("✅ STEP/DIR mode test passed!");
}

// ===== CENTER SEQUENCE =====

void update_center_sequence() {
    if (!center_sequence_active) return;
    
    unsigned long current_time = millis();
    MotorControlMode mode = (MotorControlMode)currentSettings.control_mode;
    
    switch (sequence_step) {
        case 0:
            if (current_time - sequence_start_time >= sequence_delay) {
                move_motor_steps(1000, mode);
                sequence_step = 1;
                sequence_start_time = current_time;
                add_log("🎯 Center sequence: Step 1 - Right");
            }
            break;
            
        case 1:
            if (current_time - sequence_start_time >= sequence_delay) {
                move_motor_steps(-1000, mode);
                sequence_step = 2;
                sequence_start_time = current_time;
                add_log("🎯 Center sequence: Step 2 - Left");
            }
            break;
            
        case 2:
            if (current_time - sequence_start_time >= sequence_delay) {
                move_motor_steps(1000, mode);
                sequence_step = 3;
                sequence_start_time = current_time;
                add_log("🎯 Center sequence: Step 3 - Return to center");
            }
            break;
            
        case 3:
            if (current_time - sequence_start_time >= sequence_delay) {
                center_sequence_active = false;
                add_log("✅ Center sequence completed");
            }
            break;
    }
}

bool position_reached() {
    if (!tmc_initialized) return true;
    if (!motor_enabled) return true;  // Если мотор выключен, считаем что достигли цели

    // Проверяем ТОЛЬКО скорость (не позицию, т.к. после stop() target не сбрасывается)
    int32_t vactual = motor.getCurrentSpeed();

    // Если скорость = 0, значит НЕ движется
    return (abs(vactual) < 1);
}

void start_center_sequence() {
    if (!tmc_initialized || !motor_enabled) {
        add_log("❌ Cannot start center sequence - motor not ready");
        return;
    }
    
    center_sequence_active = true;
    sequence_step = 0;
    sequence_start_time = millis();
    add_log("🎯 Center sequence started");
}

void stop_center_sequence() {
    center_sequence_active = false;
    add_log("🛑 Center sequence stopped");
}

// ===== STATUS FUNCTIONS =====

String get_movement_status() {
    if (!tmc_initialized) return "Not initialized";
    
    if (position_reached()) {
        return "At target";
    } else {
        return "Moving";
    }
}

int32_t get_current_speed() {
    if (!tmc_initialized) return 0;
    return (int32_t)motor.getCurrentSpeed();  // КАК В ПРИМЕРЕ!
}

uint32_t get_driver_status() {
    if (!tmc_initialized) return 0;
    return motor.readRegister(TMC5160_Reg::DRV_STATUS);
}

String get_detailed_diagnostics() {
    if (!tmc_initialized) {
        return "TMC5160 not initialized";
    }
    
    String diag = "=== TMC5160 DETAILED DIAGNOSTICS ===\n\n";
    
    // 1. GSTAT анализ (как в Config Wizard)
    TMC5160_Reg::GSTAT_Register gstat = {0};
    gstat.value = motor.readRegister(TMC5160_Reg::GSTAT);
    
    diag += "GSTAT: 0x" + String(gstat.value, HEX) + " - ";
    if (gstat.uv_cp) {
        diag += "⚠️ Charge pump undervoltage!\n";
    } else if (gstat.drv_err) {
        diag += "❌ Driver error detected!\n";
    } else {
        diag += "✅ No errors\n";
    }
    diag += "\n";

    // 2. DRV_STATUS детальный анализ (как в Config Wizard)
    TMC5160_Reg::DRV_STATUS_Register drvStatus = {0};
    drvStatus.value = motor.readRegister(TMC5160_Reg::DRV_STATUS);
    
    diag += "DRV_STATUS: 0x" + String(drvStatus.value, HEX) + "\n";
    if (drvStatus.s2vsa) diag += "  ❌ Short to supply phase A\n";
    if (drvStatus.s2vsb) diag += "  ❌ Short to supply phase B\n";
    if (drvStatus.s2ga) diag += "  ❌ Short to ground phase A\n";
    if (drvStatus.s2gb) diag += "  ❌ Short to ground phase B\n";
    if (drvStatus.ot) diag += "  ⚠️ Overtemperature warning\n";
    if (drvStatus.otpw) diag += "  ⚠️ Overtemperature pre-warning\n";
    if (drvStatus.stst) diag += "  ✅ Standstill detected\n";
    if (!drvStatus.s2vsa && !drvStatus.s2vsb && !drvStatus.s2ga && !drvStatus.s2gb && !drvStatus.ot) {
        diag += "  ✅ No error conditions\n";
    }
    diag += "\n";

    // 3. IOIN (версия драйвера)
    uint32_t ioin = motor.readRegister(TMC5160_Reg::IO_INPUT_OUTPUT);
    uint8_t version = (ioin >> 24) & 0xFF;
    diag += "IC Version: 0x" + String(version, HEX);
    diag += (version == 0x30) ? " (TMC5160)\n" : " (Unknown)\n";
    diag += "\n";

    // 4. Позиция и скорость
    diag += "Position: " + String(motor.getCurrentPosition()) + " steps\n";
    diag += "Target: " + String(motor.getTargetPosition()) + " steps\n";
    diag += "Speed: " + String(motor.getCurrentSpeed()) + " steps/s\n";
    diag += "\n";

    // 5. Ток
    uint32_t ihold_irun = motor.readRegister(TMC5160_Reg::IHOLD_IRUN);
    uint8_t irun = (ihold_irun >> 8) & 0x1F;
    uint8_t ihold = (ihold_irun >> 0) & 0x1F;
    uint32_t globalScaler = motor.readRegister(TMC5160_Reg::GLOBAL_SCALER);
    diag += "IRUN: " + String(irun) + ", IHOLD: " + String(ihold) + ", GLOBAL_SCALER: " + String(globalScaler) + "\n";
    diag += "Current (RMS): " + String(get_motor_current()) + " mA\n";

    return diag;
}

// ===== CURRENT CONTROL =====

void set_motor_current(uint16_t current_mA, float hold_multiplier) {
    if (!tmc_initialized) {
        add_log("❌ TMC5160 not initialized!");
        return;
    }

    // Пересчитываем irun/ihold
    uint8_t irun, ihold;
    uint16_t globalScaler;
    calculate_current_settings(current_mA, hold_multiplier, &irun, &ihold, &globalScaler);

    // Записываем в регистры
    motor.writeRegister(TMC5160_Reg::GLOBAL_SCALER, globalScaler);
    
    uint32_t ihold_irun = (ihold << 0) | (irun << 8) | (7 << 16);  // iholddelay=7
    motor.writeRegister(TMC5160_Reg::IHOLD_IRUN, ihold_irun);

    add_log("🔧 Current updated: " + String(current_mA) + "mA (irun=" + String(irun) + ", ihold=" + String(ihold) + ")");
}

uint16_t get_motor_current() {
    if (!tmc_initialized) return 0;
    
    uint32_t ihold_irun = motor.readRegister(TMC5160_Reg::IHOLD_IRUN);
    uint8_t irun = (ihold_irun >> 8) & 0x1F;
    uint32_t globalScaler = motor.readRegister(TMC5160_Reg::GLOBAL_SCALER);
    
    // Обратный расчёт
    const float V_fs = 0.325;
    const float R_SENSE_TOTAL = TMC5160_RSENSE + 0.02;
    
    float I_rms_A = ((irun + 1.0) / 32.0) * (globalScaler / 256.0) * (V_fs / R_SENSE_TOTAL) / 1.41421;
    uint16_t current_mA = (uint16_t)(I_rms_A * 1000.0);
    
    return current_mA;
}

String get_current_diagnostics() {
    if (!tmc_initialized) {
        return "TMC5160 not initialized";
    }

    String diag = "=== CURRENT DIAGNOSTICS ===\n";
    
    uint32_t ihold_irun = motor.readRegister(TMC5160_Reg::IHOLD_IRUN);
    uint8_t irun = (ihold_irun >> 8) & 0x1F;
    uint8_t ihold = (ihold_irun >> 0) & 0x1F;
    uint32_t globalScaler = motor.readRegister(TMC5160_Reg::GLOBAL_SCALER);
    
    diag += "IRUN: " + String(irun) + " (0-31)\n";
    diag += "IHOLD: " + String(ihold) + " (0-31)\n";
    diag += "GLOBAL_SCALER: " + String(globalScaler) + "\n";
    diag += "R_SENSE: " + String(TMC5160_RSENSE, 3) + " Ω\n";
    diag += "Calculated I_RMS: " + String(get_motor_current()) + " mA\n";
    
    return diag;
}

// ============================================================================
// STALLGUARD ФУНКЦИИ
// ============================================================================

// Прерывание при срабатывании StallGuard
void IRAM_ATTR stallguard_isr() {
    stallguard_triggered = true;
}

// Настройка StallGuard
void setup_stallguard(int8_t threshold) {
    if (!tmc_initialized) {
        add_log("❌ Cannot setup StallGuard - TMC not initialized");
        return;
    }
    
    if (threshold == 0) {
        // Выключаем StallGuard
        detachInterrupt(digitalPinToInterrupt(DIAG_PIN));
        add_log("🔇 StallGuard disabled");
        return;
    }
    
    // Настраиваем DIAG пин как вход с подтяжкой вниз
    pinMode(DIAG_PIN, INPUT_PULLDOWN);
    
    // Настраиваем StallGuard через регистры
    // COOLCONF: настройка StallGuard
    uint32_t coolconf = motor.readRegister(TMC5160_Reg::COOLCONF);
    coolconf &= ~(0x7F << 16);  // Очищаем биты SGT (StallGuard Threshold)
    coolconf |= ((threshold & 0x7F) << 16);  // Устанавливаем новый порог (-64...63)
    motor.writeRegister(TMC5160_Reg::COOLCONF, coolconf);
    
    // TCOOLTHRS: порог скорости для StallGuard (работает только выше этой скорости)
    motor.writeRegister(TMC5160_Reg::TCOOLTHRS, 0xFFFFF);  // Всегда активен
    
    // Настраиваем прерывание на DIAG пин
    attachInterrupt(digitalPinToInterrupt(DIAG_PIN), stallguard_isr, RISING);
    
    add_log("✅ StallGuard enabled, threshold: " + String(threshold));
}

// Проверка флага StallGuard
bool is_stallguard_triggered() {
    if (stallguard_triggered) {
        stallguard_triggered = false;  // Сбрасываем флаг
        return true;
    }
    return false;
}
