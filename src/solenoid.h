#pragma once
#include <Arduino.h>

// Управление бистабильным соленоидом RSF22/08-O035 через L298N (модуль zx-040)
// Бистабильный соленоид требует короткий импульс для переключения состояния

// Инициализация пинов соленоида
void init_solenoid();

// Переключить соленоид в состояние A (импульс в одну сторону)
// duration_ms - длительность импульса (по умолчанию 100ms)
void solenoid_switch_to_a(uint16_t duration_ms = 100);

// Переключить соленоид в состояние B (импульс в другую сторону)
// duration_ms - длительность импульса (по умолчанию 100ms)
void solenoid_switch_to_b(uint16_t duration_ms = 100);

// Получить текущее состояние соленоида (последнее переключение)
// Возвращает: "A", "B" или "unknown"
String get_solenoid_state();

// Проверить, выполняется ли сейчас переключение
bool is_solenoid_switching();

// Проверить, включен ли соленоид (ток идет через обмотку)
// Возвращает: true если ENA = HIGH (ток идет), false если выключен
bool is_solenoid_enabled();

// Получить расчетный ток через обмотку (на основе напряжения питания и сопротивления)
// voltage_V - напряжение питания (по умолчанию 24V)
// resistance_ohm - сопротивление обмотки (по умолчанию 30 Ом для RSF22/08-O035)
// Возвращает: ток в мА, или 0 если соленоид выключен
uint16_t get_solenoid_current_mA(float voltage_V = 24.0, float resistance_ohm = 30.0);

// Ручной режим с проверкой доворота (неблокирующая версия)
// Возвращает: true если датчик сработал, false если нет или еще идет проверка
// Вызывать solenoid_check_loop() в loop() для обработки
bool solenoid_switch_with_check(uint8_t direction, uint16_t duration_ms, uint8_t hall_sensor, uint16_t timeout_ms = 500);

// Обработка проверки в loop() - вызывать в основном цикле
void solenoid_check_loop();

// Автоматический тест: цикл переключений с проверкой
// direction: 0 = A, 1 = B, 2 = оба по очереди
// test_duration_ms: время на один поворот (задержка между переключениями)
// cooldown_ms: время отдыха между переключениями для защиты от перегрева (0 = без отдыха)
// hall_sensor: какой датчик проверять (1 или 2) - используется только для direction != 2
// max_attempts: максимальное количество попыток на одно переключение (перед сменой полярности)
// max_failures: максимальное количество последовательных неудач после смены полярности (защита от клина)
// max_time_ms: максимальное время теста в мс (0 = бесконечно)
// max_cycles: максимальное количество циклов (0 = бесконечно)
void solenoid_test_mode(uint8_t direction, uint16_t test_duration_ms, uint16_t cooldown_ms, uint8_t hall_sensor, uint8_t max_attempts = 3, uint8_t max_failures = 5, uint32_t max_time_ms = 0, uint32_t max_cycles = 0);

// Остановить тест
void solenoid_stop_test();

// Проверить, идет ли тест
bool is_solenoid_testing();

// Обработка теста в loop() - вызывать в основном цикле
void solenoid_test_loop();

