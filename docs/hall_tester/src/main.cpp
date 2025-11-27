#include <Arduino.h>

/*
 * Программа тестирования датчиков Холла AH3134
 * Arduino Nano с ATmega328P
 * 
 * Описание: Программа проверяет работу двух датчиков Холла TO-92UA
 * и зажигает соответствующие встроенные светодиоды при обнаружении магнитного поля
 * 
 * Встроенные светодиоды:
 * - L (пин 13) - индикация датчика Холла 1
 * - RX (пин 0) - тестовая индикация при запуске (3 моргания)
 * - TX (пин 1) - индикация датчика Холла 2
 */

// Определение пинов для датчиков Холла
const int HALL_SENSOR_1_PIN = 2;  // Датчик Холла 1 подключен к пину D2
const int HALL_SENSOR_2_PIN = 3;  // Датчик Холла 2 подключен к пину D3

// Встроенные светодиоды Arduino Nano
const int LED_L_PIN = 13;         // Светодиод L (пин 13) - для датчика 1
const int LED_RX_PIN = 0;         // Светодиод RX (пин 0) - тестовая индикация
const int LED_TX_PIN = 1;         // Светодиод TX (пин 1) - для датчика 2

// Флаги состояния датчиков
bool sensor1Active = false;       // Флаг активности датчика 1
bool sensor2Active = false;       // Флаг активности датчика 2

// Переменные для хранения состояния датчиков
int hallSensor1State = 0;
int hallSensor2State = 0;

// Переменные для хранения предыдущего состояния (для детекции изменений)
int prevHallSensor1State = 0;
int prevHallSensor2State = 0;

void setup() {
  // Инициализация последовательного порта для отладки
  Serial.begin(9600);
  Serial.println("Тестирование датчиков Холла AH3134");
  Serial.println("==================================");
  Serial.println("Используются встроенные светодиоды:");
  Serial.println("- L (пин 13) - датчик Холла 1");
  Serial.println("- RX (пин 0) - тестовая индикация при запуске");
  Serial.println("- TX (пин 1) - датчик Холла 2");
  
  // Настройка пинов датчиков Холла как входы с подтягивающим резистором
  pinMode(HALL_SENSOR_1_PIN, INPUT_PULLUP);
  pinMode(HALL_SENSOR_2_PIN, INPUT_PULLUP);
  
  // Настройка встроенных светодиодов как выходы
  pinMode(LED_L_PIN, OUTPUT);
  pinMode(LED_RX_PIN, OUTPUT);
  pinMode(LED_TX_PIN, OUTPUT);
  
  // Инициализация светодиодов (выключены)
  digitalWrite(LED_L_PIN, LOW);
  digitalWrite(LED_RX_PIN, LOW);
  digitalWrite(LED_TX_PIN, LOW);
  
  // Тестовая индикация - 3 моргания светодиодом RX
  Serial.println("Тестовая индикация - 3 моргания светодиодом RX...");
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_RX_PIN, HIGH);
    delay(200);
    digitalWrite(LED_RX_PIN, LOW);
    delay(200);
  }
  
  Serial.println("Система готова к тестированию!");
  Serial.println("Поднесите магнит к датчикам для проверки работы");
  Serial.println();
}

void loop() {
  // Чтение состояния датчиков Холла
  hallSensor1State = digitalRead(HALL_SENSOR_1_PIN);
  hallSensor2State = digitalRead(HALL_SENSOR_2_PIN);
  
  // Обработка датчика Холла 1 - светодиод L
  if (hallSensor1State != prevHallSensor1State) {
    if (hallSensor1State == LOW) {  // Магнит обнаружен (датчик активен)
      sensor1Active = true;  // Устанавливаем флаг HIGH
      digitalWrite(LED_L_PIN, HIGH);
      Serial.println("Датчик 1: МАГНИТ ОБНАРУЖЕН! Флаг HIGH, светодиод L включен");
    } else {  // Магнит убран
      sensor1Active = false;  // Сбрасываем флаг LOW
      digitalWrite(LED_L_PIN, LOW);
      Serial.println("Датчик 1: Магнит убран. Флаг LOW, светодиод L выключен");
    }
    prevHallSensor1State = hallSensor1State;
  }
  
  // Обработка датчика Холла 2 - светодиод TX
  if (hallSensor2State != prevHallSensor2State) {
    if (hallSensor2State == LOW) {  // Магнит обнаружен (датчик активен)
      sensor2Active = true;  // Устанавливаем флаг HIGH
      digitalWrite(LED_TX_PIN, HIGH);
      Serial.println("Датчик 2: МАГНИТ ОБНАРУЖЕН! Флаг HIGH, светодиод TX включен");
    } else {  // Магнит убран
      sensor2Active = false;  // Сбрасываем флаг LOW
      digitalWrite(LED_TX_PIN, LOW);
      Serial.println("Датчик 2: Магнит убран. Флаг LOW, светодиод TX выключен");
    }
    prevHallSensor2State = hallSensor2State;
  }
  
  // Небольшая задержка для стабильности
  delay(50);
} 