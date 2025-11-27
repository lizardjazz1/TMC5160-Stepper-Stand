#pragma once

// ============================================================================
// ⚠️  КРИТИЧЕСКИ ВАЖНО: НЕ МЕНЯТЬ ПИНЫ! ⚠️
// ============================================================================
// Любое изменение может привести к неработоспособности системы!

// --- Пины TMC5160 (SPI Motion Controller Mode) ---
#define EN_PIN   21   // D21 / GPIO21 - Enable (активный LOW) 
#define CS_PIN   5    // D5 / GPIO5 - выбор чипа TMC5160 
#define MOSI_PIN 23   // D23 / GPIO23 - данные к TMC5160 
#define MISO_PIN 19   // D19 / GPIO19 - данные от TMC5160 
#define SCK_PIN  18   // D18 / GPIO18 - SPI тактовый сигнал 
#define DIAG_PIN 4    // D4 / GPIO4 - StallGuard DIAG (для sensorless homing)

// ⚠️ НЕ ПОДКЛЮЧАЙТЕ CLK пин TMC5160 к ESP32!
// CLK пин предназначен ТОЛЬКО для внешнего осциллятора, не для SPI!

// STEP/DIR пины (НЕ ИСПОЛЬЗУЮТСЯ в SPI режиме):
#define STEP_PIN 27   // D27 / GPIO27 (НЕ ИСПОЛЬЗУЕТСЯ в SPI режиме) 
#define DIR_PIN  22   // D22 / GPIO22 (НЕ ИСПОЛЬЗУЕТСЯ в SPI режиме)

// === ПИТАНИЕ TMC5160 Pro V1.5 ===
// VM: 24-48V (моторное питание)
// VIO: 3.3V-5.5V (логика)
// VSA: 12V (для кулера) - ОПЦИОНАЛЬНО

// --- Пины L298N (модуль zx-040) для бистабильного соленоида RSF22/08-O035 ---
#define SOLENOID_IN1_PIN  25   // D25 / GPIO25 - L298N IN1 (управление направлением)
#define SOLENOID_IN2_PIN  26   // D26 / GPIO26 - L298N IN2 (управление направлением)
#define SOLENOID_ENA_PIN  15   // D15 / GPIO15 - L298N ENA (включение, можно использовать PWM)
// OUT1, OUT2 L298N -> к бистабильному соленоиду RSF22/08-O035

// --- Пины датчиков Холла AH3134 ---
#define HALL_SENSOR_1_PIN 32   // D32 / GPIO32 - Датчик Холла 1 (INPUT_PULLUP)
#define HALL_SENSOR_2_PIN 33   // D33 / GPIO33 - Датчик Холла 2 (INPUT_PULLUP)
// Датчики работают "активный LOW" - при поднесении магнита = LOW