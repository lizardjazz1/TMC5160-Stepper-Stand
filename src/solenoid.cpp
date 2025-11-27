#include "solenoid.h"
#include "pins.h"
#include "hall_sensors.h"
#include "tmc.h"
#include <Arduino.h>

// Объявление функции для веб-логов (определена в web_server.cpp)
extern void add_log_to_web(String message);

// Текущее состояние соленоида
String solenoid_current_state = "unknown";
bool solenoid_switching = false;
unsigned long solenoid_switch_start_time = 0;
uint16_t solenoid_switch_duration = 0;

// Режим теста
bool solenoid_test_running = false;
uint8_t solenoid_test_direction = 0;
uint16_t solenoid_test_duration_ms = 1000;
uint16_t solenoid_test_cooldown_ms = 0; // Время отдыха между переключениями для защиты от перегрева
uint8_t solenoid_test_hall_sensor = 1;
uint8_t solenoid_test_max_attempts = 3;
unsigned long solenoid_test_next_switch_time = 0;
bool solenoid_test_current_state = false; // false = A, true = B
bool solenoid_test_initial_state = false; // Начальное состояние для подсчета циклов (A>B>A = 1 цикл)
uint8_t solenoid_test_attempt = 0;
uint32_t solenoid_test_max_time_ms = 0; // 0 = бесконечно
uint32_t solenoid_test_max_cycles = 0; // 0 = бесконечно
uint32_t solenoid_test_start_time = 0;
uint32_t solenoid_test_cycle_count = 0;
uint8_t solenoid_test_state = 0; // 0 = idle, 1 = switching, 2 = checking, 3 = waiting
unsigned long solenoid_test_sensor_check_start = 0;
unsigned long solenoid_test_stabilization_start = 0;
uint8_t solenoid_test_consecutive_failures_A = 0; // Счетчик последовательных неудач для позиции A
uint8_t solenoid_test_consecutive_failures_B = 0; // Счетчик последовательных неудач для позиции B
uint8_t solenoid_test_max_consecutive_failures = 5; // Максимум последовательных неудач перед остановкой теста (настраивается)

// Статистика теста
uint32_t solenoid_test_total_switches = 0; // Общее количество переключений
uint32_t solenoid_test_successful_switches = 0; // Успешных переключений
uint32_t solenoid_test_failed_switches = 0; // Неудачных переключений
uint32_t solenoid_test_total_response_time = 0; // Суммарное время ответа датчиков (мс)
uint32_t solenoid_test_min_response_time = 0xFFFFFFFF; // Минимальное время ответа
uint32_t solenoid_test_max_response_time = 0; // Максимальное время ответа
uint32_t solenoid_test_response_time_A = 0; // Суммарное время ответа для позиции A
uint32_t solenoid_test_response_time_B = 0; // Суммарное время ответа для позиции B
uint32_t solenoid_test_switches_A = 0; // Количество переключений в позицию A
uint32_t solenoid_test_switches_B = 0; // Количество переключений в позицию B

void init_solenoid() {
    pinMode(SOLENOID_IN1_PIN, OUTPUT);
    pinMode(SOLENOID_IN2_PIN, OUTPUT);
    pinMode(SOLENOID_ENA_PIN, OUTPUT);
    
    // Изначально все выключено
    digitalWrite(SOLENOID_IN1_PIN, LOW);
    digitalWrite(SOLENOID_IN2_PIN, LOW);
    digitalWrite(SOLENOID_ENA_PIN, LOW);
    
    solenoid_current_state = "unknown";
    solenoid_switching = false;
}

void solenoid_switch_to_a(uint16_t duration_ms) {
    if (solenoid_switching) return; // Уже идет переключение
    
    solenoid_switching = true;
    solenoid_switch_start_time = millis();
    solenoid_switch_duration = duration_ms;
    
    // Импульс в одну сторону (IN1=HIGH, IN2=LOW)
    digitalWrite(SOLENOID_IN1_PIN, HIGH);
    digitalWrite(SOLENOID_IN2_PIN, LOW);
    digitalWrite(SOLENOID_ENA_PIN, HIGH);
    
    solenoid_current_state = "A";
}

void solenoid_switch_to_b(uint16_t duration_ms) {
    if (solenoid_switching) return; // Уже идет переключение
    
    solenoid_switching = true;
    solenoid_switch_start_time = millis();
    solenoid_switch_duration = duration_ms;
    
    // Импульс в другую сторону (IN1=LOW, IN2=HIGH)
    digitalWrite(SOLENOID_IN1_PIN, LOW);
    digitalWrite(SOLENOID_IN2_PIN, HIGH);
    digitalWrite(SOLENOID_ENA_PIN, HIGH);
    
    solenoid_current_state = "B";
}

String get_solenoid_state() {
    return solenoid_current_state;
}

bool is_solenoid_switching() {
    // Проверяем, не истекло ли время импульса
    if (solenoid_switching) {
        if (millis() - solenoid_switch_start_time >= solenoid_switch_duration) {
            // Импульс завершен - отключаем все
            digitalWrite(SOLENOID_IN1_PIN, LOW);
            digitalWrite(SOLENOID_IN2_PIN, LOW);
            digitalWrite(SOLENOID_ENA_PIN, LOW);
            solenoid_switching = false;
        }
    }
    return solenoid_switching;
}

bool is_solenoid_enabled() {
    // Проверяем состояние ENA пина - если HIGH, то ток идет
    return digitalRead(SOLENOID_ENA_PIN) == HIGH;
}

uint16_t get_solenoid_current_mA(float voltage_V, float resistance_ohm) {
    if (!is_solenoid_enabled()) {
        return 0; // Соленоид выключен, ток = 0
    }
    
    // Расчет тока по закону Ома: I = U / R
    float current_A = voltage_V / resistance_ohm;
    uint16_t current_mA = (uint16_t)(current_A * 1000.0);
    
    return current_mA;
}

// Неблокирующая версия - использует статическую переменную для состояния
static struct {
    bool active = false;
    uint8_t direction;
    uint16_t duration_ms;
    uint8_t hall_sensor;
    uint16_t timeout_ms;
    unsigned long start_time;
    bool waiting_for_impulse;
    bool checking_sensor;
    bool result;
} switch_check_state;

bool solenoid_switch_with_check(uint8_t direction, uint16_t duration_ms, uint8_t hall_sensor, uint16_t timeout_ms) {
    // direction: 0 = A, 1 = B
    // hall_sensor: 1 или 2
    // timeout_ms: время ожидания срабатывания датчика
    
    if (solenoid_switching) return false; // Уже идет переключение
    
    // Если уже идет проверка - возвращаем результат если готов
    if (switch_check_state.active) {
        if (!switch_check_state.checking_sensor && 
            (millis() - switch_check_state.start_time > switch_check_state.duration_ms + switch_check_state.timeout_ms + 100)) {
            // Проверка завершена
            bool result = switch_check_state.result;
            switch_check_state.active = false;
            return result;
        }
        // Еще идет проверка
        return false;
    }
    
    // Начинаем новую проверку
    switch_check_state.active = true;
    switch_check_state.direction = direction;
    switch_check_state.duration_ms = duration_ms;
    switch_check_state.hall_sensor = hall_sensor;
    switch_check_state.timeout_ms = timeout_ms;
    switch_check_state.start_time = millis();
    switch_check_state.waiting_for_impulse = true;
    switch_check_state.checking_sensor = false;
    switch_check_state.result = false;
    
    // Переключаем
    if (direction == 0) {
        solenoid_switch_to_a(duration_ms);
    } else {
        solenoid_switch_to_b(duration_ms);
    }
    
    return false; // Результат будет позже
}

// Неблокирующая обработка проверки - вызывать в loop()
void solenoid_check_loop() {
    if (!switch_check_state.active) return;
    
    unsigned long now = millis();
    unsigned long elapsed = now - switch_check_state.start_time;
    
    if (switch_check_state.waiting_for_impulse) {
        // Ждем завершения импульса
        if (!is_solenoid_switching() && elapsed > switch_check_state.duration_ms) {
            // Импульс завершен, ждем стабилизации
            if (elapsed > switch_check_state.duration_ms + 50) {
                switch_check_state.waiting_for_impulse = false;
                switch_check_state.checking_sensor = true;
                switch_check_state.start_time = now; // Сбрасываем таймер для проверки датчика
            }
        }
    } else if (switch_check_state.checking_sensor) {
        // Проверяем датчик
        bool sensor_state = (switch_check_state.hall_sensor == 1) ? 
                           read_hall_sensor_1() : read_hall_sensor_2();
        
        if (sensor_state) {
            switch_check_state.result = true;
            switch_check_state.active = false;
            return;
        }
        
        // Проверяем таймаут
        if (elapsed > switch_check_state.timeout_ms) {
            switch_check_state.result = false;
            switch_check_state.active = false;
        }
    }
}

void solenoid_test_mode(uint8_t direction, uint16_t test_duration_ms, uint16_t cooldown_ms, uint8_t hall_sensor, uint8_t max_attempts, uint8_t max_failures, uint32_t max_time_ms, uint32_t max_cycles) {
    solenoid_test_running = true;
    solenoid_test_direction = direction; // 0 = A, 1 = B, 2 = оба
    solenoid_test_duration_ms = test_duration_ms;
    solenoid_test_cooldown_ms = cooldown_ms; // Время отдыха для защиты от перегрева
    solenoid_test_hall_sensor = hall_sensor;
    solenoid_test_max_attempts = max_attempts;
    solenoid_test_max_consecutive_failures = max_failures; // Настраиваемый параметр защиты от клина
    solenoid_test_max_time_ms = max_time_ms;
    solenoid_test_max_cycles = max_cycles;
    solenoid_test_next_switch_time = millis();
    solenoid_test_current_state = false; // Начинаем с A
    solenoid_test_initial_state = false; // Начальное состояние для подсчета циклов
    solenoid_test_attempt = 0;
    solenoid_test_state = 0; // idle
    solenoid_test_start_time = millis();
    solenoid_test_cycle_count = 0;
    solenoid_test_consecutive_failures_A = 0; // Сбрасываем счетчики последовательных неудач
    solenoid_test_consecutive_failures_B = 0;
    
    // Сбрасываем статистику
    solenoid_test_total_switches = 0;
    solenoid_test_successful_switches = 0;
    solenoid_test_failed_switches = 0;
    solenoid_test_total_response_time = 0;
    solenoid_test_min_response_time = 0xFFFFFFFF;
    solenoid_test_max_response_time = 0;
    solenoid_test_response_time_A = 0;
    solenoid_test_response_time_B = 0;
    solenoid_test_switches_A = 0;
    solenoid_test_switches_B = 0;
    
    // Формируем сообщение о запуске теста в формате вариант 2
    String direction_str = (direction == 0) ? "A" : (direction == 1) ? "B" : "A ⇄ B";
    String log_msg = "[TEST] Тест запущен: " + direction_str + " | Задержка: " + String(test_duration_ms) + "ms";
    if (cooldown_ms > 0) {
        log_msg += " | Отдых: " + String(cooldown_ms) + "ms";
    }
    if (max_cycles > 0) {
        log_msg += " | Циклов: " + String(max_cycles);
    }
    if (max_time_ms > 0) {
        log_msg += " | Время: " + String(max_time_ms / 1000) + "с";
    }
    add_log("🧪 " + log_msg);
    add_log_to_web(log_msg);
    add_log_to_web("────────────────────────────────────────────────────────────────");
}

void solenoid_stop_test() {
    if (!solenoid_test_running) return;
    
    unsigned long test_duration = millis() - solenoid_test_start_time;
    
    // Выводим разделитель перед статистикой
    add_log_to_web("────────────────────────────────────────────────────────────────");
    
    // Выводим причину остановки
    String stop_msg = "[STOP] Тест остановлен | Время: " + String(test_duration / 1000.0, 1) + "с | Циклов: " + String(solenoid_test_cycle_count);
    add_log("🛑 " + stop_msg);
    add_log_to_web(stop_msg);
    
    // Выводим итоговую статистику в формате вариант 2
    add_log_to_web("[STATS] ════════════════════════════════════════════");
    
    // Общая статистика
    float success_rate = (solenoid_test_total_switches > 0) ? 
        (100.0 * solenoid_test_successful_switches / solenoid_test_total_switches) : 0.0;
    
    add_log_to_web("[STATS] Время теста: " + String(test_duration / 1000.0, 1) + " секунд");
    add_log_to_web("[STATS] Выполнено циклов: " + String(solenoid_test_cycle_count));
    add_log_to_web("[STATS] Успешных переключений: " + String(solenoid_test_successful_switches));
    add_log_to_web("[STATS] Ошибок: " + String(solenoid_test_failed_switches));
    add_log_to_web("[STATS] Успешность: " + String(success_rate, 1) + "%");
    
    // Статистика по времени ответа
    if (solenoid_test_successful_switches > 0) {
        float avg_response = (float)solenoid_test_total_response_time / solenoid_test_successful_switches;
        add_log_to_web("[STATS] Среднее время ответа: " + String(avg_response, 1) + "мс");
        
        if (solenoid_test_min_response_time != 0xFFFFFFFF) {
            add_log_to_web("[STATS] Минимальное время: " + String(solenoid_test_min_response_time) + "мс");
        }
        if (solenoid_test_max_response_time > 0) {
            add_log_to_web("[STATS] Максимальное время: " + String(solenoid_test_max_response_time) + "мс");
        }
        
        // Статистика по позициям
        if (solenoid_test_switches_A > 0) {
            float avg_A = (float)solenoid_test_response_time_A / solenoid_test_switches_A;
            add_log_to_web("[STATS] Среднее время ответа A: " + String(avg_A, 1) + "мс (" + String(solenoid_test_switches_A) + " переключений)");
        }
        if (solenoid_test_switches_B > 0) {
            float avg_B = (float)solenoid_test_response_time_B / solenoid_test_switches_B;
            add_log_to_web("[STATS] Среднее время ответа B: " + String(avg_B, 1) + "мс (" + String(solenoid_test_switches_B) + " переключений)");
        }
    } else {
        add_log_to_web("[STATS] Нет успешных переключений для статистики");
    }
    
    add_log_to_web("[STATS] ════════════════════════════════════════════");
    
    solenoid_test_running = false;
    solenoid_test_state = 0;
}

bool is_solenoid_testing() {
    return solenoid_test_running;
}

// Функция для вызова в loop() - обрабатывает автоматический тест
void solenoid_test_loop() {
    if (!solenoid_test_running) return;
    
    // Проверяем ограничения по времени и количеству циклов
    if (solenoid_test_max_time_ms > 0) {
        if ((millis() - solenoid_test_start_time) >= solenoid_test_max_time_ms) {
            String time_msg = "[TIME] Тест завершен по времени (" + String(solenoid_test_max_time_ms / 1000.0, 1) + "с, циклов: " + String(solenoid_test_cycle_count) + ")";
            add_log("⏱️ " + time_msg);
            add_log_to_web(time_msg);
            solenoid_stop_test();
            return;
        }
    }
    
    if (solenoid_test_max_cycles > 0) {
        if (solenoid_test_cycle_count >= solenoid_test_max_cycles) {
            String cycles_msg = "[CYCLES] Тест завершен по количеству циклов (" + String(solenoid_test_cycle_count) + ")";
            add_log("🔢 " + cycles_msg);
            add_log_to_web(cycles_msg);
            solenoid_stop_test();
            return;
        }
    }
    
    // Проверяем, не движется ли мотор (чтобы не мешать)
    extern bool motor_enabled;
    extern bool tmc_initialized;
    if (tmc_initialized && motor_enabled) {
        extern TMC5160_SPI *motor_ptr;
        if (motor_ptr) {
            int32_t vactual = (int32_t)(*motor_ptr).readRegister(TMC5160_Reg::VACTUAL);
            if (abs(vactual) > 10) {
                // Мотор движется - пропускаем этот цикл
                return;
            }
        }
    }
    
    // Обрабатываем состояния теста
    if (solenoid_test_state == 0) {
        // idle - ждем времени для следующего переключения
        if (millis() >= solenoid_test_next_switch_time) {
            // Определяем направление для текущего переключения
            uint8_t current_direction;
            uint8_t expected_sensor;
            String pos_name;
            
            if (solenoid_test_direction == 2) {
                // Оба по очереди: A → датчик 1, B → датчик 2
                current_direction = solenoid_test_current_state ? 1 : 0;
                expected_sensor = solenoid_test_current_state ? 2 : 1;
                pos_name = solenoid_test_current_state ? "B (-90°)" : "A (+90°)";
            } else {
                current_direction = solenoid_test_direction;
                expected_sensor = solenoid_test_direction == 0 ? 1 : 2;
                pos_name = solenoid_test_direction == 0 ? "A (+90°)" : "B (-90°)";
            }
            
            // Начинаем переключение: подаем импульс 100мс, затем проверяем датчик
            // Каждая попытка = один импульс (100мс) + проверка датчика (до 500мс)
            // Если не сработал - повторяем через 200мс (следующая попытка)
            // После всех попыток (max_attempts) - меняем полярность и пробуем снова
            if (current_direction == 0) {
                solenoid_switch_to_a(100); // Импульс 100мс в сторону A
            } else {
                solenoid_switch_to_b(100); // Импульс 100мс в сторону B
            }
            solenoid_test_state = 1; // switching - ждем завершения импульса
            solenoid_test_sensor_check_start = 0;
            solenoid_test_stabilization_start = 0;
            
            // Формат вариант 2 без "Переключение в"
            String switch_msg = "[SWITCH] " + pos_name + " | Попытка " + String(solenoid_test_attempt + 1) + "/" + String(solenoid_test_max_attempts);
            add_log("🔄 " + switch_msg);
            add_log_to_web(switch_msg);
            solenoid_test_total_switches++; // Увеличиваем счетчик переключений
        }
    } else if (solenoid_test_state == 1) {
        // switching - ждем завершения импульса и стабилизации
        if (!is_solenoid_switching()) {
            // Импульс завершен, ждем стабилизации (50мс)
            if (solenoid_test_stabilization_start == 0) {
                solenoid_test_stabilization_start = millis();
            }
            if ((millis() - solenoid_test_stabilization_start) >= 50) {
                // Стабилизация завершена, начинаем проверку датчика
                solenoid_test_state = 2; // checking
                solenoid_test_sensor_check_start = millis();
                solenoid_test_stabilization_start = 0; // Сбрасываем для следующего раза
            }
        } else {
            // Импульс еще идет, сбрасываем таймер стабилизации
            solenoid_test_stabilization_start = 0;
        }
    } else if (solenoid_test_state == 2) {
        // checking - проверяем датчик
        uint8_t expected_sensor;
        if (solenoid_test_direction == 2) {
            expected_sensor = solenoid_test_current_state ? 2 : 1;
        } else {
            expected_sensor = solenoid_test_direction == 0 ? 1 : 2;
        }
        
        bool sensor_state = (expected_sensor == 1) ? read_hall_sensor_1() : read_hall_sensor_2();
        
        if (sensor_state) {
            // Датчик сработал!
            unsigned long sensor_time = millis() - solenoid_test_sensor_check_start;
            String pos_name = (solenoid_test_direction == 2) ? 
                (solenoid_test_current_state ? "B (-90°)" : "A (+90°)") :
                (solenoid_test_direction == 0 ? "A (+90°)" : "B (-90°)");
            String sensor_name = "H" + String(expected_sensor);
            
            // Формат вариант 2
            String success_msg = "[OK] " + pos_name + " → " + sensor_name + " сработал за " + String(sensor_time) + "мс";
            add_log("✅ " + success_msg);
            add_log_to_web(success_msg);
            
            // Собираем статистику
            solenoid_test_successful_switches++;
            solenoid_test_total_response_time += sensor_time;
            if (sensor_time < solenoid_test_min_response_time) {
                solenoid_test_min_response_time = sensor_time;
            }
            if (sensor_time > solenoid_test_max_response_time) {
                solenoid_test_max_response_time = sensor_time;
            }
            
            // Статистика по позициям
            if (solenoid_test_direction == 2) {
                if (solenoid_test_current_state) {
                    // Позиция B
                    solenoid_test_response_time_B += sensor_time;
                    solenoid_test_switches_B++;
                } else {
                    // Позиция A
                    solenoid_test_response_time_A += sensor_time;
                    solenoid_test_switches_A++;
                }
            } else {
                if (solenoid_test_direction == 0) {
                    // Позиция A
                    solenoid_test_response_time_A += sensor_time;
                    solenoid_test_switches_A++;
                } else {
                    // Позиция B
                    solenoid_test_response_time_B += sensor_time;
                    solenoid_test_switches_B++;
                }
            }
            
            solenoid_test_attempt = 0;
            
            // Сбрасываем счетчик последовательных неудач для успешной позиции
            if (solenoid_test_direction == 2) {
                if (solenoid_test_current_state) {
                    // Успешно переключились в B - сбрасываем счетчик для B
                    solenoid_test_consecutive_failures_B = 0;
                } else {
                    // Успешно переключились в A - сбрасываем счетчик для A
                    solenoid_test_consecutive_failures_A = 0;
                }
            } else {
                // Для режимов "только A" или "только B" сбрасываем соответствующий счетчик
                if (solenoid_test_direction == 0) {
                    solenoid_test_consecutive_failures_A = 0;
                } else {
                    solenoid_test_consecutive_failures_B = 0;
                }
            }
            
            // Меняем состояние для следующего переключения
            if (solenoid_test_direction == 2) {
                solenoid_test_current_state = !solenoid_test_current_state;
                
                // Цикл = A>B>A (туда-обратно), считаем только когда вернулись в начальное состояние
                if (solenoid_test_current_state == solenoid_test_initial_state) {
                    solenoid_test_cycle_count++;
                }
            } else {
                // Для режимов "только A" или "только B" цикл = каждое переключение
                solenoid_test_cycle_count++;
            }
            
            // Планируем следующее переключение (задержка + время отдыха для защиты от перегрева)
            solenoid_test_next_switch_time = millis() + solenoid_test_duration_ms + solenoid_test_cooldown_ms;
            solenoid_test_state = 0; // idle
        } else {
            // Проверяем таймаут
            if ((millis() - solenoid_test_sensor_check_start) >= 500) {
                // Таймаут - датчик не сработал
                solenoid_test_attempt++;
                String pos_name = (solenoid_test_direction == 2) ? 
                    (solenoid_test_current_state ? "B (-90°)" : "A (+90°)") :
                    (solenoid_test_direction == 0 ? "A (+90°)" : "B (-90°)");
                String sensor_name = "H" + String(expected_sensor);
                
                // Формат вариант 2
                String fail_msg = "[ERROR] " + pos_name + " → " + sensor_name + " НЕ сработал | Попытка " + String(solenoid_test_attempt) + "/" + String(solenoid_test_max_attempts);
                add_log("❌ " + fail_msg);
                add_log_to_web(fail_msg);
                
                if (solenoid_test_attempt >= solenoid_test_max_attempts) {
                    // Все попытки исчерпаны - увеличиваем счетчик последовательных неудач для текущей позиции
                    solenoid_test_failed_switches++; // Увеличиваем счетчик неудач
                    
                    uint8_t* failure_counter = nullptr;
                    String pos_name_for_counter = (solenoid_test_direction == 2) ? 
                        (solenoid_test_current_state ? "B" : "A") :
                        (solenoid_test_direction == 0 ? "A" : "B");
                    
                    if (solenoid_test_direction == 2) {
                        if (solenoid_test_current_state) {
                            // Проблема с позицией B
                            solenoid_test_consecutive_failures_B++;
                            failure_counter = &solenoid_test_consecutive_failures_B;
                        } else {
                            // Проблема с позицией A
                            solenoid_test_consecutive_failures_A++;
                            failure_counter = &solenoid_test_consecutive_failures_A;
                        }
                    } else {
                        if (solenoid_test_direction == 0) {
                            solenoid_test_consecutive_failures_A++;
                            failure_counter = &solenoid_test_consecutive_failures_A;
                        } else {
                            solenoid_test_consecutive_failures_B++;
                            failure_counter = &solenoid_test_consecutive_failures_B;
                        }
                    }
                    
                    // Проверяем защиту от клина
                    if (failure_counter && *failure_counter >= solenoid_test_max_consecutive_failures) {
                        // Слишком много последовательных неудач для этой позиции - останавливаем тест
                        String jam_msg = "[STOP] КРИТИЧЕСКАЯ ОШИБКА: Позиция " + pos_name_for_counter + " заклинила или неисправна! Тест остановлен после " + 
                                        String(*failure_counter) + " последовательных неудач";
                        add_log("🚨 " + jam_msg);
                        add_log_to_web(jam_msg);
                        solenoid_stop_test();
                        return;
                    }
                    
                    // Меняем полярность и пробуем снова
                    String polarity_msg = "[SWITCH] Меняем полярность и пробуем снова | Неудач " + pos_name_for_counter + " подряд: " + 
                                        String(*failure_counter) + "/" + 
                                        String(solenoid_test_max_consecutive_failures);
                    add_log("🔄 " + polarity_msg);
                    add_log_to_web(polarity_msg);
                    solenoid_test_current_state = !solenoid_test_current_state;
                    solenoid_test_attempt = 0;
                }
                
                // Планируем повторную попытку через короткое время
                solenoid_test_next_switch_time = millis() + 200;
                solenoid_test_state = 0; // idle
            }
        }
    }
}

