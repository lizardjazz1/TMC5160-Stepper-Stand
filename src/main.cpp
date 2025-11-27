#include <Arduino.h>
#include <WiFi.h>
#include "tmc.h"
#include "web_server.h"
#include "config.h"
#include "pins.h"
#include "eeprom_manager.h"
#include "LittleFS.h"
#include <EEPROM.h>
#include "solenoid.h"
#include "hall_sensors.h"

// SPI Motion Controller - никаких extern переменных!
void handleClient(); // Объявление функции из web_server.cpp

// Функция инициализации WiFi
void init_wifi_ap() {
    const char* ssid = "Krya";
    const char* password = "12345678";

    WiFi.softAP(ssid, password);

    IPAddress IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(IP, gateway, subnet);

    Serial.print("WiFi AP started: ");
    Serial.println(ssid);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

void setup() {
    Serial.begin(115200);
    Serial.println("=== ESP32 STARTING ===");

    // СНАЧАЛА WiFi - чтобы точка доступа поднялась быстро
    Serial.println("Starting WiFi AP...");
    init_wifi_ap();

    // === ИНИЦИАЛИЗАЦИЯ ФАЙЛОВОЙ СИСТЕМЫ ПЕРЕД ВЕБ-СЕРВЕРОМ ===
    Serial.println("Starting LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS failed!");
        while (1) delay(1000);
    }
    Serial.println("✅ LittleFS initialized successfully");

    Serial.println("Starting web server...");
    init_web_server();

    Serial.println("WiFi and Web Server ready!");

    if (!EEPROM.begin(EEPROM_SIZE)) {
        Serial.println("⚠️ EEPROM failed - continuing...");
    } else {
        // Загружаем настройки из EEPROM
        initMotorSettingsFromEEPROM();
    }

    // === ИНИЦИАЛИЗАЦИЯ ПИНОВ ДЛЯ TMC5160 (только SPI) ===
    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);  // ВЫКЛЮЧАЕМ драйвер (активация через веб-интерфейс)

    // === НАСТРОЙКА STEP/DIR ПИНОВ ===
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN, LOW);

    // === ИНИЦИАЛИЗАЦИЯ СОЛЕНОИДА ===
    Serial.println("Initializing solenoid (L298N)...");
    init_solenoid();
    Serial.println("✅ Solenoid initialized");

    // === ИНИЦИАЛИЗАЦИЯ ДАТЧИКОВ ХОЛЛА ===
    Serial.println("Initializing Hall sensors...");
    init_hall_sensors();
    Serial.println("✅ Hall sensors initialized");

    // === ИНИЦИАЛИЗАЦИЯ TMC5160 (SPI инициализируется в setup_tmc5160) ===
    Serial.println("=== TMC5160 INITIALIZATION ===");

    // Инициализация TMC5160 с настройками из EEPROM
    Serial.println("Initializing TMC5160 with stored settings...");
    if (!setup_tmc5160(currentSettings.current_mA, currentSettings.hold_multiplier, currentSettings.microsteps, 
                      currentSettings.max_speed, currentSettings.acceleration, currentSettings.deceleration)) {
        Serial.println("❌ Failed to initialize TMC5160!");
        Serial.println("System will continue without TMC5160...");
    } else {
        Serial.println("✅ TMC5160 initialized successfully!");
        Serial.print("Mode: ");
        Serial.println(currentSettings.control_mode == MODE_MOTION_CONTROLLER ? "Motion Controller" : "STEP/DIR");
    }

    // TMC5160 готов к работе через веб-интерфейс

    Serial.println("=== SYSTEM READY ===");
    Serial.println("Connect to WiFi 'Krya' and go to 192.168.4.1");
}

void loop() {
    // Мониторинг TMC5160
    run_motor();

    // Проверка состояния соленоида (завершение импульса)
    is_solenoid_switching();
    
    // Обработка неблокирующей проверки датчиков
    solenoid_check_loop();
    
    // Обработка автоматического теста соленоида
    solenoid_test_loop();

    // Обрабатываем веб-запросы
    handleClient();

    delay(10);
}