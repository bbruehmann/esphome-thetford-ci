#pragma once

// Thetford T2000 / N4000 CI-Bus (LIN 19200 Bd) – ESP32 als LIN-Master.
// Protokoll: horchi/linux-homectld, skrebber/thetford (GPL-2.0).
//
// Status-Frame  0x0C (Kühlschrank antwortet, 8 Byte):
//   B0 Betriebsmodus (Bit 3 = Nacht/Auto-Bit, Bits 0..2 = Modus)
//   B1 Leistungsstufe – T2000: High-Nibble Gefrierfach, Low-Nibble Kühlteil
//   B3 Fehler-/Statuscode (0 = OK)
//   B5 12-V-Versorgung × 10
// Control-Frame 0x0B (Master sendet, immer 8 Byte): B0 Modus, B1 Stufe.

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif

#include <array>
#include <string>

namespace esphome {
namespace thetford_ci {

enum ChecksumMode : uint8_t {
  CHECKSUM_AUTO = 0,
  CHECKSUM_CLASSIC = 1,
  CHECKSUM_ENHANCED = 2,
};

enum class SwitchType : uint8_t { POWER, NIGHT };
enum class LevelType : uint8_t { FRIDGE, FREEZER };

class ThetfordCI;

#ifdef USE_SWITCH
class ThetfordSwitch : public switch_::Switch, public Parented<ThetfordCI> {
 public:
  void set_type(SwitchType type) { this->type_ = type; }

 protected:
  void write_state(bool state) override;
  SwitchType type_{SwitchType::POWER};
};
#endif

#ifdef USE_NUMBER
class ThetfordLevel : public number::Number, public Parented<ThetfordCI> {
 public:
  void set_type(LevelType type) { this->type_ = type; }

 protected:
  void control(float value) override;
  LevelType type_{LevelType::FRIDGE};
};
#endif

class ThetfordCI : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Konfiguration
  void set_allow_write(bool v) { this->allow_write_ = v; }
  void set_checksum_mode(ChecksumMode m) { this->checksum_mode_ = m; }
  void set_status_frame_id(uint8_t id) { this->status_id_ = id; }
  void set_control_frame_id(uint8_t id) { this->control_id_ = id; }
  void set_response_timeout(uint32_t ms) { this->response_timeout_ = ms; }
  void set_offline_after(uint8_t n) { this->offline_after_ = n; }
  void set_on_mode_value(uint8_t v) { this->on_mode_ = v; }
  void set_off_mode_value(uint8_t v) { this->off_mode_ = v; }
  void set_write_repeat(uint8_t n) { this->write_repeat_ = n; }

#ifdef USE_SENSOR
  void set_supply_voltage_sensor(sensor::Sensor *s) { this->supply_voltage_ = s; }
  void set_fridge_level_sensor(sensor::Sensor *s) { this->fridge_level_ = s; }
  void set_freezer_level_sensor(sensor::Sensor *s) { this->freezer_level_ = s; }
  void set_error_code_sensor(sensor::Sensor *s) { this->error_code_ = s; }
  void set_mode_raw_sensor(sensor::Sensor *s) { this->mode_raw_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_online_binary_sensor(binary_sensor::BinarySensor *s) { this->online_ = s; }
  void set_running_binary_sensor(binary_sensor::BinarySensor *s) { this->running_ = s; }
  void set_night_binary_sensor(binary_sensor::BinarySensor *s) { this->night_ = s; }
  void set_fault_binary_sensor(binary_sensor::BinarySensor *s) { this->fault_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_mode_text_sensor(text_sensor::TextSensor *s) { this->mode_text_ = s; }
  void set_status_text_sensor(text_sensor::TextSensor *s) { this->status_text_ = s; }
  void set_raw_frame_text_sensor(text_sensor::TextSensor *s) { this->raw_frame_ = s; }
#endif
#ifdef USE_SWITCH
  void set_power_switch(ThetfordSwitch *s) { this->power_switch_ = s; }
  void set_night_switch(ThetfordSwitch *s) { this->night_switch_ = s; }
#endif
#ifdef USE_NUMBER
  void set_fridge_level_number(ThetfordLevel *n) { this->fridge_level_number_ = n; }
  void set_freezer_level_number(ThetfordLevel *n) { this->freezer_level_number_ = n; }
#endif

  // Steuerbefehle (aus Switch/Number). Liefern false, wenn gesperrt/nicht möglich.
  bool request_power(bool on);
  bool request_night(bool on);
  bool request_level(LevelType type, uint8_t level);

  bool has_status() const { return this->have_status_; }

 protected:
  static uint8_t pid_(uint8_t id);
  static uint8_t checksum_(const uint8_t *data, size_t len, uint8_t pid, bool enhanced);

  void send_break_();
  void send_header_(uint8_t id);
  void send_frame_(uint8_t id, const uint8_t *data, size_t len);
  void request_status_();
  void process_response_();
  void handle_status_(const uint8_t *d);
  void set_offline_();
  bool can_write_(const char *what);
  void queue_control_(uint8_t mode, uint8_t level);
  void republish_controls_();

  static const char *mode_to_string_(uint8_t mode);
  static const char *error_to_string_(uint8_t code);
  bool mode_is_on_(uint8_t mode) const;

  // Konfiguration
  bool allow_write_{false};
  ChecksumMode checksum_mode_{CHECKSUM_AUTO};
  uint8_t status_id_{0x0C};
  uint8_t control_id_{0x0B};
  uint32_t response_timeout_{30};
  uint8_t offline_after_{3};
  uint8_t on_mode_{0x01};
  uint8_t off_mode_{0x08};
  uint8_t write_repeat_{3};

  // Zustand
  enum class State : uint8_t { IDLE, WAIT_RESPONSE } state_{State::IDLE};
  uint32_t request_time_{0};
  uint8_t missed_{0};
  bool online_state_{false};
  bool have_status_{false};
  bool detected_enhanced_{false};
  bool checksum_detected_{false};
  std::array<uint8_t, 8> last_status_{};

  // ausstehender Steuerbefehl
  std::array<uint8_t, 8> control_{};
  uint8_t control_pending_{0};

#ifdef USE_SENSOR
  sensor::Sensor *supply_voltage_{nullptr};
  sensor::Sensor *fridge_level_{nullptr};
  sensor::Sensor *freezer_level_{nullptr};
  sensor::Sensor *error_code_{nullptr};
  sensor::Sensor *mode_raw_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *online_{nullptr};
  binary_sensor::BinarySensor *running_{nullptr};
  binary_sensor::BinarySensor *night_{nullptr};
  binary_sensor::BinarySensor *fault_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *mode_text_{nullptr};
  text_sensor::TextSensor *status_text_{nullptr};
  text_sensor::TextSensor *raw_frame_{nullptr};
#endif
#ifdef USE_SWITCH
  ThetfordSwitch *power_switch_{nullptr};
  ThetfordSwitch *night_switch_{nullptr};
#endif
#ifdef USE_NUMBER
  ThetfordLevel *fridge_level_number_{nullptr};
  ThetfordLevel *freezer_level_number_{nullptr};
#endif
};

}  // namespace thetford_ci
}  // namespace esphome
