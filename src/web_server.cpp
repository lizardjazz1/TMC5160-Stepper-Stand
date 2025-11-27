#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "web_server.h"
#include "tmc.h"
#include "eeprom_manager.h"
#include "api_types.h"
#include "pins.h"
#include "solenoid.h"
#include "hall_sensors.h"

AsyncWebServer server(80);

// Глобальный лог для системы
String system_logs = "";

// Функция форматирования времени (миллисекунды в формат MM:SS.mmm)
String format_time_ms(unsigned long ms) {
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    seconds = seconds % 60;
    unsigned long milliseconds = ms % 1000;
    
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu.%03lu", minutes, seconds, milliseconds);
    return String(buffer);
}

// Функция замены смайликов на текстовые метки
String replace_emojis(String message) {
    message.replace("✅", "[OK]");
    message.replace("❌", "[ERROR]");
    message.replace("⚠️", "[WARN]");
    message.replace("🚀", "[MOVE]");
    message.replace("🔄", "[SWITCH]");
    message.replace("🔧", "[CONFIG]");
    message.replace("🔋", "[ENABLE]");
    message.replace("🔌", "[DISABLE]");
    message.replace("📊", "[INFO]");
    message.replace("🎯", "[CENTER]");
    message.replace("🚨", "[STOP]");
    message.replace("💾", "[SAVE]");
    message.replace("⚙️", "[SETTINGS]");
    message.replace("⏹️", "[STOP]");
    message.replace("🧪", "[TEST]");
    message.replace("🛑", "[STOP]");
    message.replace("⏱️", "[TIME]");
    message.replace("🔢", "[CYCLES]");
    message.replace("🧹", "[CLEAR]");
    return message;
}

// Функция добавления в лог (объявлена в tmc.h)
void add_log_to_web(String message) {
    // Форматируем время и убираем смайлики
    String formatted_time = format_time_ms(millis());
    String clean_message = replace_emojis(message);
    system_logs += "[" + formatted_time + "] " + clean_message + "\n";
    
    // Ограничиваем размер лога (обрезаем по строкам, а не по символам)
    if (system_logs.length() > 10000) {
        int cutPos = system_logs.indexOf('\n', 5000);
        if (cutPos != -1) {
            system_logs = system_logs.substring(cutPos + 1);
        } else {
            system_logs = system_logs.substring(5000);
        }
    }
}

// JSON ответ для статуса
String getStatusJson() {
    JsonDocument doc;
    doc["success"] = true;
    
    JsonObject data = doc["data"].to<JsonObject>();
    data["initialized"] = tmc_initialized;
    data["enabled"] = motor_enabled;
    // Читаем регистры НАПРЯМУЮ для правильных значений (они В МИКРОШАГАХ!)
    int32_t xactual_microsteps = tmc_initialized ? (int32_t)motor.readRegister(TMC5160_Reg::XACTUAL) : 0;
    int32_t xtarget_microsteps = tmc_initialized ? (int32_t)motor.readRegister(TMC5160_Reg::XTARGET) : 0;
    int32_t vactual = tmc_initialized ? (int32_t)motor.readRegister(TMC5160_Reg::VACTUAL) : 0;
    
    // КОНВЕРТИРУЕМ МИКРОШАГИ → БАЗОВЫЕ ШАГИ (делим на microsteps)
    uint16_t microsteps = currentSettings.microsteps > 0 ? currentSettings.microsteps : 16;
    int32_t xactual = xactual_microsteps / microsteps;
    int32_t xtarget = xtarget_microsteps / microsteps;
    int32_t steps_remaining = abs(xtarget_microsteps - xactual_microsteps) / microsteps;
    
    data["current_position"] = xactual;
    data["target_position"] = xtarget;
    data["current_speed"] = vactual;
    data["steps_remaining"] = steps_remaining;
    data["is_moving"] = tmc_initialized ? !position_reached() : false;
    
    // Настройки для frontend (из currentSettings)
    JsonObject settings = data["settings"].to<JsonObject>();
    settings["steps_per_rev"] = currentSettings.steps_per_rev;
    settings["max_speed"] = currentSettings.max_speed;
    settings["acceleration"] = currentSettings.acceleration;
    settings["deceleration"] = currentSettings.deceleration;
    settings["current_mA"] = currentSettings.current_mA;
    settings["hold_multiplier"] = currentSettings.hold_multiplier;
    settings["microsteps"] = currentSettings.microsteps;
    settings["gear_ratio"] = currentSettings.gear_ratio;  // Передаточное число!
    settings["stealthchop"] = false;  // Всегда SpreadCycle для тестового стенда
    settings["stallguard_threshold"] = currentSettings.stallguard_threshold;
    
    // Данные о соленоиде
    JsonObject solenoid = data["solenoid"].to<JsonObject>();
    solenoid["state"] = get_solenoid_state();
    solenoid["switching"] = is_solenoid_switching();
    solenoid["testing"] = is_solenoid_testing();
    solenoid["enabled"] = is_solenoid_enabled();
    solenoid["current_mA"] = get_solenoid_current_mA(24.0, 30.0); // 24V питание, 30 Ом сопротивление (можно настроить)
    
    // Данные о датчиках Холла
    bool hall1 = read_hall_sensor_1();
    bool hall2 = read_hall_sensor_2();
    JsonObject hall_sensors = data["hall_sensors"].to<JsonObject>();
    hall_sensors["sensor1"]["active"] = hall1;
    hall_sensors["sensor1"]["state"] = hall1 ? "MAGNET_DETECTED" : "NO_MAGNET";
    hall_sensors["sensor2"]["active"] = hall2;
    hall_sensors["sensor2"]["state"] = hall2 ? "MAGNET_DETECTED" : "NO_MAGNET";
    
    String response;
    serializeJson(doc, response);
    return response;
}

// JSON ответ для диагностики
String getDiagnosticJson() {
    JsonDocument doc;
    doc["success"] = true;
    
    JsonObject data = doc["data"].to<JsonObject>();
    data["initialized"] = tmc_initialized;
    
    if (tmc_initialized) {
        // Проверяем связь с драйвером
        uint32_t ioin_value = motor.readRegister(TMC5160_Reg::IO_INPUT_OUTPUT);
        uint8_t version = (ioin_value >> 24) & 0xFF;
        bool communication_ok = (version != 0xFF && version != 0 && 
                                ioin_value != 0xFFFFFFFF && ioin_value != 0x00000000);
        
        // Основная информация
        data["chip_version"] = communication_ok ? String(version) : "N/A";
        data["current_position"] = communication_ok ? (int32_t)motor.readRegister(TMC5160_Reg::XACTUAL) : 0;
        data["target_position"] = communication_ok ? (int32_t)motor.readRegister(TMC5160_Reg::XTARGET) : 0;
        data["spi_communication"] = communication_ok;
        data["motor_enabled"] = motor_enabled;
        data["microsteps"] = communication_ok ? currentSettings.microsteps : 0;
        data["current_mA"] = communication_ok ? currentSettings.current_mA : 0;
        
        // Анализ состояния (для фронтенда)
        JsonObject analysis = data["analysis"].to<JsonObject>();
        analysis["chip_version"] = communication_ok ? String(version) : String("0");
        bool drv_enn = (ioin_value >> 1) & 0x01;
        analysis["driver_enabled"] = !drv_enn; // drv_enn = 0 означает включен
        analysis["vs_power_ok"] = true; // Предполагаем, что питание OK
        
        // Регистры (для фронтенда)
        JsonObject registers = data["registers"].to<JsonObject>();
        if (communication_ok) {
            registers["ioin"] = "0x" + String(motor.readRegister(TMC5160_Reg::IO_INPUT_OUTPUT), HEX);
            registers["gconf"] = "0x" + String(motor.readRegister(TMC5160_Reg::GCONF), HEX);
            registers["gstat"] = "0x" + String(motor.readRegister(TMC5160_Reg::GSTAT), HEX);
            registers["xactual"] = String((int32_t)motor.readRegister(TMC5160_Reg::XACTUAL));
            registers["xtarget"] = String((int32_t)motor.readRegister(TMC5160_Reg::XTARGET));
            registers["vmax"] = String(motor.readRegister(TMC5160_Reg::VMAX));
            registers["amax"] = String(motor.readRegister(TMC5160_Reg::AMAX));
            registers["dmax"] = String(motor.readRegister(TMC5160_Reg::DMAX));
        } else {
            registers["ioin"] = "N/A";
            registers["gconf"] = "N/A";
            registers["gstat"] = "N/A";
            registers["xactual"] = "N/A";
            registers["xtarget"] = "N/A";
            registers["vmax"] = "N/A";
            registers["amax"] = "N/A";
            registers["dmax"] = "N/A";
        }
        
        // Дополнительная диагностика
        data["vmax"] = communication_ok ? motor.readRegister(TMC5160_Reg::VMAX) : 0;
        data["amax"] = communication_ok ? motor.readRegister(TMC5160_Reg::AMAX) : 0;
        data["dmax"] = communication_ok ? motor.readRegister(TMC5160_Reg::DMAX) : 0;
        data["vactual"] = communication_ok ? (int32_t)motor.getCurrentSpeed() : 0;
        
        // Статус драйвера
        uint32_t gstat = communication_ok ? motor.readRegister(TMC5160_Reg::GSTAT) : 0;
        data["gstat"] = "0x" + String(gstat, HEX);
        data["gstat_reset"] = (gstat & 0x01) ? true : false;
        data["gstat_driver_error"] = (gstat & 0x02) ? true : false;
        
        // Статус движения
        uint32_t ramp_stat = communication_ok ? motor.readRegister(TMC5160_Reg::RAMP_STAT) : 0;
        data["ramp_stat"] = "0x" + String(ramp_stat, HEX);
        data["position_reached"] = (ramp_stat & 0x80) ? true : false;
        data["velocity_reached"] = (ramp_stat & 0x40) ? true : false;
        
        // Настройки
        uint32_t chopconf = communication_ok ? motor.readRegister(TMC5160_Reg::CHOPCONF) : 0;
        data["toff"] = communication_ok ? (chopconf & 0x0F) : 0;
        data["intpol"] = communication_ok ? ((chopconf >> 28) & 0x01) : false;
        uint32_t gconf = communication_ok ? motor.readRegister(TMC5160_Reg::GCONF) : 0;
        data["en_pwm_mode"] = communication_ok ? (gconf & 0x04) : false;
        uint32_t pwmconf = communication_ok ? motor.readRegister(TMC5160_Reg::PWMCONF) : 0;
        data["pwm_autoscale"] = communication_ok ? ((pwmconf >> 18) & 0x01) : false;
        
               // SPI метод и режим работы
               data["spi_method"] = communication_ok ? "SPI Mode 3" : "N/A";
               uint32_t rampmode = communication_ok ? motor.readRegister(TMC5160_Reg::RAMPMODE) : 0;
               data["ramp_mode"] = rampmode;
               data["mode_description"] = communication_ok ? 
                   (rampmode == 0 ? "Motion Controller Mode" : "STEP/DIR Mode") : "N/A";
        
        // Распиновка (из pins.h)
        JsonObject pins = data["pins"].to<JsonObject>();
        pins["cs"] = CS_PIN;
        pins["mosi"] = MOSI_PIN;
        pins["miso"] = MISO_PIN;
        pins["sck"] = SCK_PIN;
        pins["en"] = EN_PIN;
        pins["step"] = STEP_PIN;
        pins["dir"] = DIR_PIN;
        
    } else {
        data["spi_communication"] = false;
        data["spi_method"] = "N/A";
        data["error"] = "TMC5160 not initialized";
        
        // Пустые объекты для фронтенда
        data["analysis"] = JsonObject();
        data["registers"] = JsonObject();
        data["pins"] = JsonObject();
    }
    
    String response;
    serializeJson(doc, response);
    return response;
}

void init_web_server() {
    // Главная страница
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });

    // API: Статус системы
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getStatusJson());
    });

    // API: Включить мотор
    server.on("/api/enable", HTTP_POST, [](AsyncWebServerRequest *request) {
        enable_motor();
        add_log("🔋 Motor enabled via API");
        add_log_to_web("🔋 Motor enabled via API");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Motor enabled";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Выключить мотор
    server.on("/api/disable", HTTP_POST, [](AsyncWebServerRequest *request) {
        disable_motor();
        add_log("🔌 Motor disabled via API");
        add_log_to_web("🔌 Motor disabled via API");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Motor disabled";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Движение по шагам (с валидацией и выбором режима)
    server.on("/api/move", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("steps", true)) {
            int32_t steps = request->getParam("steps", true)->value().toInt();

            // Считываем параметры из запроса
            uint16_t driver_current = request->hasParam("driver_current", true) ?
                request->getParam("driver_current", true)->value().toInt() : currentSettings.current_mA;
            float hold_mult = request->hasParam("hold_multiplier", true) ?
                request->getParam("hold_multiplier", true)->value().toFloat() : currentSettings.hold_multiplier;
            uint16_t microsteps = request->hasParam("driver_microsteps", true) ?
                request->getParam("driver_microsteps", true)->value().toInt() : currentSettings.microsteps;
            uint32_t max_speed = request->hasParam("max_speed", true) ?
                request->getParam("max_speed", true)->value().toInt() : currentSettings.max_speed;
            uint16_t acceleration = request->hasParam("acceleration", true) ?
                request->getParam("acceleration", true)->value().toInt() : currentSettings.acceleration;
            uint16_t deceleration = request->hasParam("deceleration", true) ?
                request->getParam("deceleration", true)->value().toInt() : currentSettings.deceleration;

            // Валидация параметров
            if (!validate_current(driver_current)) {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Invalid current value: " + String(driver_current) + "mA";
                String response; serializeJson(doc, response);
                request->send(400, "application/json", response);
                return;
            }
            if (!validate_speed(max_speed)) {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Invalid speed value: " + String(max_speed) + " steps/s";
                String response; serializeJson(doc, response);
                request->send(400, "application/json", response);
                return;
            }
            if (!validate_acceleration(acceleration) || !validate_acceleration(deceleration)) {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Invalid acceleration/deceleration values";
                String response; serializeJson(doc, response);
                request->send(400, "application/json", response);
                return;
            }

            // Проверяем что TMC инициализирован
            if (!tmc_initialized) {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "TMC5160 not initialized";
                String response; serializeJson(doc, response);
                request->send(500, "application/json", response);
                return;
            }

            // Проверяем что мотор включен
            if (!motor_enabled) {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Motor is not enabled. Please enable motor first.";
                String response; serializeJson(doc, response);
                request->send(400, "application/json", response);
                return;
            }

            // Обновляем скорость/ускорение для этого движения (БЕЗ переинициализации!)
            motor.setMaxSpeed(max_speed);
            motor.setAcceleration(acceleration);
            add_log("⚙️ Speed/Accel updated: VMAX=" + String(max_speed) + ", AMAX=" + String(acceleration));

            // Используем режим из currentSettings
            MotorControlMode mode = (MotorControlMode)currentSettings.control_mode;
            move_motor_steps(steps, mode);
            
            add_log("🚀 Movement: " + String(steps) + " steps in " + 
                    String(mode == MODE_MOTION_CONTROLLER ? "Motion Controller" : "STEP/DIR") + " mode");
            add_log_to_web("🚀 Movement started: " + String(steps) + " steps");

            JsonDocument doc;
            doc["success"] = true;
            doc["message"] = "Movement started: " + String(steps) + " steps in " + 
                              String(mode == MODE_MOTION_CONTROLLER ? "Motion Controller" : "STEP/DIR") + " mode";

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Missing steps parameter";

            String response;
            serializeJson(doc, response);
            request->send(400, "application/json", response);
        }
    });

    // API: Движение по углу (используем текущий режим)
    server.on("/api/move_angle", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("angle", true) && request->hasParam("direction", true)) {
            float angle = request->getParam("angle", true)->value().toFloat();
            String direction = request->getParam("direction", true)->value();

            // Используем steps_per_rev из currentSettings
            int32_t steps = (angle / 360.0) * currentSettings.steps_per_rev;

            add_log("📊 Angle: " + String(angle) + "°");
            add_log("📊 Steps per rev: " + String(currentSettings.steps_per_rev));
            add_log("📊 Calculated steps: " + String(steps));

            if (direction == "backward") steps = -steps;

            // Используем текущий режим
            MotorControlMode mode = (MotorControlMode)currentSettings.control_mode;
            move_motor_steps(steps, mode);
            
            add_log("🔄 Angle movement: " + String(angle) + "° " + direction + " (" + String(steps) + " steps)");
            add_log_to_web("🔄 Angle movement: " + String(angle) + "° " + direction);

            JsonDocument doc;
            doc["success"] = true;
            doc["message"] = "Angle movement started: " + String(angle) + "° " + direction;
            doc["steps"] = steps;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Missing angle or direction parameter";

            String response;
            serializeJson(doc, response);
            request->send(400, "application/json", response);
        }
    });

    // API: Движение от центра
    server.on("/api/move_from_center", HTTP_POST, [](AsyncWebServerRequest *request) {
        start_center_sequence(); // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ!
        add_log("🎯 Center sequence initiated");
        add_log_to_web("🎯 Center sequence initiated");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Center cycle initiated";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Экстренная остановка
    server.on("/api/emergency_stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        digitalWrite(EN_PIN, HIGH);
        motor_enabled = false;
        
        if (tmc_initialized) {
            motor.stop();  // КАК В ПРИМЕРЕ!
        }
        
        add_log("🚨 EMERGENCY STOP activated!");
        add_log_to_web("🚨 EMERGENCY STOP activated!");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Emergency stop activated";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Применить пресет
    server.on("/api/apply_preset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("preset_id", true)) {
            int preset_id = request->getParam("preset_id", true)->value().toInt();
            
            if (preset_id >= 0 && preset_id < NEMA_PRESETS_COUNT) {
                const MotorPreset& preset = NEMA_PRESETS[preset_id];
                
                bool success = setup_tmc5160(
                    preset.current_mA,
                    preset.hold_mult,
                    preset.microsteps,
                    preset.max_speed,
                    preset.acceleration,
                    preset.deceleration
                );
                
                if (success) {
                    // Сохраняем настройки в EEPROM и currentSettings
                    MotorSettings newSettings;
                    newSettings.current_mA = preset.current_mA;
                    newSettings.hold_multiplier = preset.hold_mult;  // ✅ Добавлено!
                    newSettings.microsteps = preset.microsteps;
                    newSettings.max_speed = preset.max_speed;
                    newSettings.acceleration = preset.acceleration;
                    newSettings.deceleration = preset.deceleration;
                    newSettings.steps_per_rev = preset.steps_per_rev;
                    newSettings.control_mode = MODE_MOTION_CONTROLLER;
                    
                    if (saveMotorSettings(newSettings)) {
                        currentSettings = newSettings;
                        add_log("💾 Preset settings saved to EEPROM");
                        add_log_to_web("💾 Preset settings saved to EEPROM");
                    }
                    
                    add_log("⚙️ Preset applied: " + String(preset.name));
                    add_log_to_web("⚙️ Preset applied: " + String(preset.name));
                    
                    JsonDocument doc;
                    doc["success"] = true;
                    doc["message"] = "Preset " + String(preset.name) + " applied and saved";
                    
                    JsonObject data = doc["data"].to<JsonObject>();
                    data["name"] = preset.name;
                    data["current_mA"] = preset.current_mA;
                    data["hold_mult"] = preset.hold_mult;
                    data["microsteps"] = preset.microsteps;
                    data["max_speed"] = preset.max_speed;
                    data["acceleration"] = preset.acceleration;
                    data["deceleration"] = preset.deceleration;
                    data["steps_per_rev"] = preset.steps_per_rev;
                    
                    String response;
                    serializeJson(doc, response);
                    request->send(200, "application/json", response);
                } else {
                    JsonDocument doc;
                    doc["success"] = false;
                    doc["message"] = "Failed to apply preset";
                    
                    String response;
                    serializeJson(doc, response);
                    request->send(500, "application/json", response);
                }
            } else {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Invalid preset ID";
                
                String response;
                serializeJson(doc, response);
                request->send(400, "application/json", response);
            }
        } else {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Missing preset_id parameter";
            
            String response;
            serializeJson(doc, response);
            request->send(400, "application/json", response);
        }
    });

    // API: Список пресетов (для UI)
    server.on("/api/presets", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["success"] = true;
        JsonArray arr = doc["data"].to<JsonArray>();
        for (int i = 0; i < NEMA_PRESETS_COUNT; i++) {
            const MotorPreset& p = NEMA_PRESETS[i];
            JsonObject o = arr.add<JsonObject>();
            o["id"] = i;
            o["name"] = p.name;
            o["current_mA"] = p.current_mA;
            o["hold_mult"] = p.hold_mult;
            o["microsteps"] = p.microsteps;
            o["max_speed"] = p.max_speed;
            o["acceleration"] = p.acceleration;
            o["deceleration"] = p.deceleration;
        }
        String response; serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Диагностика
    server.on("/api/diagnostic", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getDiagnosticJson());
    });

    // API: Подробная диагностика
    server.on("/api/detailed_diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
        String diagnostics = get_detailed_diagnostics();
        request->send(200, "text/plain", diagnostics);
    });

    // ❌ VACTUAL API удалён - используем только Motion Controller режим

    // API: Установить ток в Амперах (amps -> mA), как в PoC
    server.on("/api/set_current_amps", HTTP_POST, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        if (!tmc_initialized) {
            doc["success"] = false;
            doc["message"] = "TMC5160 not initialized";
            String response; serializeJson(doc, response);
            request->send(400, "application/json", response);
            return;
        }

        if (request->hasParam("amps", true)) {
            const float amps = request->getParam("amps", true)->value().toFloat();
            const uint16_t mA = (uint16_t)roundf(amps * 1000.0f);
            const float hold = currentSettings.hold_multiplier;

            set_motor_current(mA, hold);
            add_log("🔧 Current set via API (amps): " + String(amps, 3) + "A");
            add_log_to_web("🔧 Current set via API (amps): " + String(amps, 3) + "A");

            doc["success"] = true;
            doc["message"] = "Current updated";
            doc["current_mA"] = (int)mA;
            String response; serializeJson(doc, response);
            request->send(200, "application/json", response);
        } else {
            doc["success"] = false;
            doc["message"] = "Missing amps parameter";
            String response; serializeJson(doc, response);
            request->send(400, "application/json", response);
        }
    });

    // API: Остановка движения
    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (tmc_initialized) {
            motor.stop();  // КАК В ПРИМЕРЕ!
        }
        add_log("⏹️ Movement stopped via API");
        add_log_to_web("⏹️ Movement stopped via API");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Movement stopped";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Сброс позиции
    server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (tmc_initialized) {
            motor.setCurrentPosition(0);  // КАК В ПРИМЕРЕ!
            motor.setTargetPosition(0);
        }
        add_log("🔄 Position reset via API");
        add_log_to_web("🔄 Position reset via API");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Position reset to zero";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Логи
    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["success"] = true;
        doc["data"] = system_logs;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Очистить логи
    server.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
        system_logs = "";
        add_log("🧹 Logs cleared");
        add_log_to_web("🧹 Logs cleared");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Logs cleared";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Скачать логи как текстовый файл
    server.on("/api/logs/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Генерируем имя файла с текущей датой/временем (в миллисекундах от старта)
        String filename = "logs_" + String(millis()) + ".txt";
        
        // Отправляем логи как plain text с заголовком для скачивания
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain; charset=utf-8", system_logs);
        response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        request->send(response);
    });

    // ❌ STEP/DIR тест удалён - используем только Motion Controller (SPI) режим

    // API: Сохранить настройки в EEPROM
    server.on("/api/save_settings", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("current_mA", true) && request->hasParam("microsteps", true) &&
            request->hasParam("max_speed", true) && request->hasParam("acceleration", true) &&
            request->hasParam("deceleration", true)) {

            MotorSettings newSettings;
            newSettings.current_mA = request->getParam("current_mA", true)->value().toInt();
            newSettings.microsteps = request->getParam("microsteps", true)->value().toInt();
            newSettings.max_speed = request->getParam("max_speed", true)->value().toInt();
            newSettings.acceleration = request->getParam("acceleration", true)->value().toInt();
            newSettings.deceleration = request->getParam("deceleration", true)->value().toInt();
            
            // steps_per_rev опционально, по умолчанию = 200 * microsteps
            if (request->hasParam("steps_per_rev", true)) {
                newSettings.steps_per_rev = request->getParam("steps_per_rev", true)->value().toInt();
            } else {
                newSettings.steps_per_rev = 200 * newSettings.microsteps;
            }
            
            // hold_multiplier опционально
            if (request->hasParam("hold_multiplier", true)) {
                newSettings.hold_multiplier = request->getParam("hold_multiplier", true)->value().toFloat();
            } else {
                newSettings.hold_multiplier = currentSettings.hold_multiplier;
            }
            
            // gear_ratio опционально
            if (request->hasParam("gear_ratio", true)) {
                newSettings.gear_ratio = request->getParam("gear_ratio", true)->value().toFloat();
            } else {
                newSettings.gear_ratio = currentSettings.gear_ratio;
            }
            
            // control_mode фиксирован (Motion Controller)
            newSettings.control_mode = MODE_MOTION_CONTROLLER;

            // Валидация
            if (!validate_current(newSettings.current_mA)) {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Invalid current value";
                String response; serializeJson(doc, response);
                request->send(400, "application/json", response);
                return;
            }

            // Сохраняем в EEPROM
            if (saveMotorSettings(newSettings)) {
                // Обновляем текущие настройки
                currentSettings = newSettings;
                
                add_log("💾 Settings saved: I=" + String(newSettings.current_mA) + "mA, µ=" + String(newSettings.microsteps) +
                        ", mode=" + String(newSettings.control_mode == MODE_MOTION_CONTROLLER ? "MC" : "SD"));
                add_log_to_web("💾 Settings saved to EEPROM");

                JsonDocument doc;
                doc["success"] = true;
                doc["message"] = "Settings saved to EEPROM";

                String response;
                serializeJson(doc, response);
                request->send(200, "application/json", response);
            } else {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Failed to save settings";

                String response;
                serializeJson(doc, response);
                request->send(500, "application/json", response);
            }
        } else {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Missing required parameters";

            String response;
            serializeJson(doc, response);
            request->send(400, "application/json", response);
        }
    });

    // API: Управление соленоидом - переключить в состояние A
    server.on("/api/solenoid/switch_a", HTTP_POST, [](AsyncWebServerRequest *request) {
        uint16_t duration = 100; // По умолчанию 100ms
        if (request->hasParam("duration", true)) {
            duration = request->getParam("duration", true)->value().toInt();
            if (duration < 50) duration = 50;   // Минимум 50ms
            if (duration > 500) duration = 500; // Максимум 500ms
        }
        
        solenoid_switch_to_a(duration);
        add_log("🔌 Solenoid switched to state A (duration: " + String(duration) + "ms)");
        add_log_to_web("🔌 Solenoid switched to state A");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Solenoid switched to state A";
        doc["duration_ms"] = duration;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Управление соленоидом - переключить в состояние B
    server.on("/api/solenoid/switch_b", HTTP_POST, [](AsyncWebServerRequest *request) {
        uint16_t duration = 100; // По умолчанию 100ms
        if (request->hasParam("duration", true)) {
            duration = request->getParam("duration", true)->value().toInt();
            if (duration < 50) duration = 50;   // Минимум 50ms
            if (duration > 500) duration = 500; // Максимум 500ms
        }
        
        solenoid_switch_to_b(duration);
        add_log("🔌 Solenoid switched to state B (duration: " + String(duration) + "ms)");
        add_log_to_web("🔌 Solenoid switched to state B");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Solenoid switched to state B";
        doc["duration_ms"] = duration;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Получить состояние соленоида
    server.on("/api/solenoid/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["success"] = true;
        doc["state"] = get_solenoid_state();
        doc["switching"] = is_solenoid_switching();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Получить состояние датчиков Холла
    server.on("/api/hall_sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
        String response = get_hall_sensors_json();
        request->send(200, "application/json", response);
    });

    // API: Ручной режим с проверкой доворота
    server.on("/api/solenoid/switch_with_check", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("direction", true) || !request->hasParam("hall_sensor", true)) {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Missing parameters: direction, hall_sensor";
            String response; serializeJson(doc, response);
            request->send(400, "application/json", response);
            return;
        }
        
        uint8_t direction = request->getParam("direction", true)->value().toInt(); // 0 = A, 1 = B
        uint8_t hall_sensor = request->getParam("hall_sensor", true)->value().toInt(); // 1 или 2
        uint16_t duration = request->hasParam("duration", true) ? 
                           request->getParam("duration", true)->value().toInt() : 100;
        uint16_t timeout = request->hasParam("timeout", true) ? 
                          request->getParam("timeout", true)->value().toInt() : 500;
        
        // Синхронная проверка
        bool success = false;
        
        // Переключаем
        if (direction == 0) {
            solenoid_switch_to_a(duration);
        } else {
            solenoid_switch_to_b(duration);
        }
        
        // Ждем завершения импульса
        unsigned long wait_start = millis();
        while (is_solenoid_switching() && (millis() - wait_start < duration + 100)) {
            delay(10);
        }
        delay(50); // Стабилизация
        
        // Проверяем датчик
        unsigned long check_start = millis();
        while ((millis() - check_start) < timeout) {
            bool sensor_state = (hall_sensor == 1) ? read_hall_sensor_1() : read_hall_sensor_2();
            if (sensor_state) {
                success = true;
                break;
            }
            delay(10);
        }
        
        String pos_name = direction == 0 ? "A (+90°)" : "B (-90°)";
        String sensor_name = hall_sensor == 1 ? "Датчик 1" : "Датчик 2";
        add_log("🔌 Переключение в " + pos_name + ", проверка " + sensor_name + ": " + String(success ? "✅ Магнит найден" : "❌ Магнит не найден"));
        add_log_to_web("🔌 " + pos_name + " → " + String(success ? "✅ Магнит на месте" : "❌ Магнит не обнаружен"));
        
        JsonDocument doc;
        doc["success"] = success;
        doc["message"] = success ? "Датчик сработал" : "Датчик не сработал";
        doc["direction"] = direction;
        doc["hall_sensor"] = hall_sensor;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Запустить тест (неблокирующий, работает в фоне)
    server.on("/api/solenoid/start_test", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("direction", true) || !request->hasParam("test_duration", true)) {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Missing parameters: direction, test_duration";
            String response; serializeJson(doc, response);
            request->send(400, "application/json", response);
            return;
        }
        
        uint8_t direction = request->getParam("direction", true)->value().toInt(); // 0 = A, 1 = B, 2 = оба
        uint16_t test_duration = request->getParam("test_duration", true)->value().toInt();
        uint16_t cooldown_ms = request->hasParam("cooldown_ms", true) ? 
                              request->getParam("cooldown_ms", true)->value().toInt() : 0;
        uint8_t hall_sensor = request->hasParam("hall_sensor", true) ? 
                             request->getParam("hall_sensor", true)->value().toInt() : 1;
        uint8_t max_attempts = request->hasParam("max_attempts", true) ? 
                              request->getParam("max_attempts", true)->value().toInt() : 3;
        uint8_t max_failures = request->hasParam("max_failures", true) ? 
                              request->getParam("max_failures", true)->value().toInt() : 5;
        uint32_t max_time_ms = request->hasParam("max_time_sec", true) ? 
                              request->getParam("max_time_sec", true)->value().toInt() * 1000UL : 0;
        uint32_t max_cycles = request->hasParam("max_cycles", true) ? 
                             request->getParam("max_cycles", true)->value().toInt() : 0;
        
        // Проверяем, не идет ли уже тест
        if (is_solenoid_testing()) {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Тест уже запущен";
            String response; serializeJson(doc, response);
            request->send(400, "application/json", response);
            return;
        }
        
        // Проверяем, не движется ли мотор
        if (tmc_initialized && motor_enabled) {
            int32_t vactual = (int32_t)motor.readRegister(TMC5160_Reg::VACTUAL);
            if (abs(vactual) > 10) {
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = "Мотор движется, остановите перед тестом";
                String response; serializeJson(doc, response);
                request->send(400, "application/json", response);
                return;
            }
        }
        
        // Запускаем тест
        solenoid_test_mode(direction, test_duration, cooldown_ms, hall_sensor, max_attempts, max_failures, max_time_ms, max_cycles);
        
        String test_mode = direction == 2 ? "A ⇄ B" : (direction == 0 ? "Только A" : "Только B");
        add_log_to_web("🧪 Тест соленоида запущен: " + test_mode);
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Тест запущен";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Остановить тест
    server.on("/api/solenoid/stop_test", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!is_solenoid_testing()) {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = "Тест не запущен";
            String response; serializeJson(doc, response);
            request->send(400, "application/json", response);
            return;
        }
        
        solenoid_stop_test();
        add_log_to_web("🛑 Тест остановлен");
        
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = "Тест остановлен";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // Статические файлы из LittleFS
    server.serveStatic("/", LittleFS, "/");

    // 404 обработчик
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });

    // Запуск сервера
    server.begin();
    add_log("✅ ESPAsyncWebServer started with full API support");
    add_log_to_web("✅ ESPAsyncWebServer started with full API support");
}

void handleClient() {
    // С ESPAsyncWebServer эта функция не нужна в loop()
    // Сервер обрабатывает клиентов асинхронно
    // Оставляем для совместимости
}