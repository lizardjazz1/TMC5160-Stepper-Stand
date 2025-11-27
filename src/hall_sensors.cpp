#include "hall_sensors.h"
#include "pins.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// Переменные для детекции изменений состояния
bool hall_sensor_1_prev = false;
bool hall_sensor_2_prev = false;
unsigned long hall_sensor_1_last_change = 0;
unsigned long hall_sensor_2_last_change = 0;

void init_hall_sensors() {
    // Настройка пинов как входы с подтягивающим резистором
    pinMode(HALL_SENSOR_1_PIN, INPUT_PULLUP);
    pinMode(HALL_SENSOR_2_PIN, INPUT_PULLUP);
    
    // Инициализация предыдущих состояний
    hall_sensor_1_prev = digitalRead(HALL_SENSOR_1_PIN) == LOW;
    hall_sensor_2_prev = digitalRead(HALL_SENSOR_2_PIN) == LOW;
    hall_sensor_1_last_change = millis();
    hall_sensor_2_last_change = millis();
}

bool read_hall_sensor_1() {
    // Датчик активен (магнит обнаружен) когда пин = LOW
    return digitalRead(HALL_SENSOR_1_PIN) == LOW;
}

bool read_hall_sensor_2() {
    // Датчик активен (магнит обнаружен) когда пин = LOW
    return digitalRead(HALL_SENSOR_2_PIN) == LOW;
}

String get_hall_sensors_json() {
    JsonDocument doc;
    doc["success"] = true;
    
    JsonObject data = doc["data"].to<JsonObject>();
    
    bool sensor1_active = read_hall_sensor_1();
    bool sensor2_active = read_hall_sensor_2();
    
    data["sensor1"]["active"] = sensor1_active;
    data["sensor1"]["state"] = sensor1_active ? "MAGNET_DETECTED" : "NO_MAGNET";
    
    data["sensor2"]["active"] = sensor2_active;
    data["sensor2"]["state"] = sensor2_active ? "MAGNET_DETECTED" : "NO_MAGNET";
    
    // Детекция изменений
    if (sensor1_active != hall_sensor_1_prev) {
        hall_sensor_1_last_change = millis();
        hall_sensor_1_prev = sensor1_active;
    }
    if (sensor2_active != hall_sensor_2_prev) {
        hall_sensor_2_last_change = millis();
        hall_sensor_2_prev = sensor2_active;
    }
    
    data["sensor1"]["last_change_ms"] = hall_sensor_1_last_change;
    data["sensor2"]["last_change_ms"] = hall_sensor_2_last_change;
    
    String response;
    serializeJson(doc, response);
    return response;
}

