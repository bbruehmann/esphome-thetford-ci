#include "thetford_ci.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "esphome/components/uart/uart_component_esp_idf.h"
#include <driver/uart.h>
#else
#error "thetford_ci benötigt einen ESP32 (LIN-Break über Baudratenumschaltung des IDF-UART-Treibers)"
#endif

#include <cmath>

namespace esphome {
namespace thetford_ci {

static const char *const TAG = "thetford_ci";

static const uint8_t LIN_SYNC = 0x55;
static const uint32_t LIN_BAUD = 19200;
static const uint32_t BREAK_BAUD = 9600;  // 0x00 @ 9600 Bd ≈ 18 Bitzeiten @ 19200 Bd
static const uint8_t NIGHT_BIT = 0x08;
static const uint8_t MODE_MASK = 0x07;

// ---------------------------------------------------------------------------
// LIN-Grundlagen
// ---------------------------------------------------------------------------

uint8_t ThetfordCI::pid_(uint8_t id) {
  id &= 0x3F;
  auto bit = [id](uint8_t n) -> uint8_t { return (id >> n) & 0x01; };
  uint8_t p0 = bit(0) ^ bit(1) ^ bit(2) ^ bit(4);
  uint8_t p1 = (~(bit(1) ^ bit(3) ^ bit(4) ^ bit(5))) & 0x01;
  return id | (p0 << 6) | (p1 << 7);
}

uint8_t ThetfordCI::checksum_(const uint8_t *data, size_t len, uint8_t pid, bool enhanced) {
  uint16_t sum = enhanced ? pid : 0;
  for (size_t i = 0; i < len; i++) {
    sum += data[i];
    if (sum > 0xFF)
      sum -= 0xFF;
  }
  return static_cast<uint8_t>(~sum & 0xFF);
}

void ThetfordCI::send_break_() {
  auto *idf = static_cast<uart::IDFUARTComponent *>(this->parent_);
  auto port = static_cast<uart_port_t>(idf->get_hw_serial_number());
  uart_wait_tx_done(port, pdMS_TO_TICKS(20));
  uart_set_baudrate(port, BREAK_BAUD);
  const uint8_t zero = 0x00;
  uart_write_bytes(port, reinterpret_cast<const char *>(&zero), 1);
  uart_wait_tx_done(port, pdMS_TO_TICKS(10));
  uart_set_baudrate(port, LIN_BAUD);
}

void ThetfordCI::send_header_(uint8_t id) {
  this->send_break_();
  const uint8_t hdr[2] = {LIN_SYNC, pid_(id)};
  this->write_array(hdr, 2);
}

void ThetfordCI::send_frame_(uint8_t id, const uint8_t *data, size_t len) {
  bool enhanced = this->checksum_mode_ == CHECKSUM_ENHANCED ||
                  (this->checksum_mode_ == CHECKSUM_AUTO && this->detected_enhanced_);
  uint8_t pid = pid_(id);
  uint8_t buf[11];
  buf[0] = LIN_SYNC;
  buf[1] = pid;
  for (size_t i = 0; i < len; i++)
    buf[2 + i] = data[i];
  buf[2 + len] = checksum_(data, len, pid, enhanced);
  this->send_break_();
  this->write_array(buf, len + 3);
  this->flush();
  ESP_LOGD(TAG, "TX 0x%02X: %s (%s-Checksumme)", id, format_hex_pretty(data, len).c_str(),
           enhanced ? "enhanced" : "classic");
}

// ---------------------------------------------------------------------------
// Lebenszyklus
// ---------------------------------------------------------------------------

void ThetfordCI::setup() {
  // Wie das Original-Skript: einmal einen Header senden, um schlafende Slaves zu wecken.
  this->set_timeout("thetford_wakeup", 500, [this]() {
    this->send_header_(0x00);
    this->flush();
  });
  this->set_offline_();
}

void ThetfordCI::dump_config() {
  ESP_LOGCONFIG(TAG, "Thetford CI-Bus (LIN-Master):");
  ESP_LOGCONFIG(TAG, "  Status-Frame:  0x%02X (PID 0x%02X)", this->status_id_, pid_(this->status_id_));
  ESP_LOGCONFIG(TAG, "  Control-Frame: 0x%02X (PID 0x%02X)", this->control_id_, pid_(this->control_id_));
  ESP_LOGCONFIG(TAG, "  Schreiben:     %s", this->allow_write_ ? "ERLAUBT" : "gesperrt (nur lesen)");
  ESP_LOGCONFIG(TAG, "  Checksumme:    %s",
                this->checksum_mode_ == CHECKSUM_AUTO      ? "auto"
                : this->checksum_mode_ == CHECKSUM_CLASSIC ? "classic"
                                                           : "enhanced");
  ESP_LOGCONFIG(TAG, "  Modus an/aus:  0x%02X / 0x%02X", this->on_mode_, this->off_mode_);
  LOG_UPDATE_INTERVAL(this);
}

void ThetfordCI::update() {
  if (this->state_ != State::IDLE)
    return;

  if (this->control_pending_ > 0 && this->can_write_("Steuerframe")) {
    this->send_frame_(this->control_id_, this->control_.data(), this->control_.size());
    this->control_pending_--;
    // kurze Pause, dann Status lesen, um den Effekt zu prüfen
    this->set_timeout("thetford_after_write", 20, [this]() { this->request_status_(); });
    return;
  }
  this->control_pending_ = 0;
  this->request_status_();
}

void ThetfordCI::loop() {
  if (this->state_ == State::WAIT_RESPONSE && millis() - this->request_time_ >= this->response_timeout_) {
    this->process_response_();
    this->state_ = State::IDLE;
    if (this->control_pending_ > 0) {
      this->set_timeout("thetford_next", 50, [this]() { this->update(); });
    }
  }
}

// ---------------------------------------------------------------------------
// Status lesen
// ---------------------------------------------------------------------------

void ThetfordCI::request_status_() {
  if (this->state_ != State::IDLE)
    return;
  // Empfangspuffer leeren (Echo/Altdaten)
  uint8_t dummy;
  while (this->available() > 0)
    this->read_byte(&dummy);

  this->send_header_(this->status_id_);
  this->request_time_ = millis();
  this->state_ = State::WAIT_RESPONSE;
}

void ThetfordCI::process_response_() {
  // Der LIN-Transceiver spiegelt den eigenen Header auf RX. Erwartet wird:
  //   [Break-Echo 0x00 …] 0x55 PID D0..D7 CS
  uint8_t buf[32];
  size_t n = 0;
  while (this->available() > 0 && n < sizeof(buf)) {
    this->read_byte(&buf[n]);
    n++;
  }

  const uint8_t pid = pid_(this->status_id_);
  int start = -1;
  for (size_t i = 0; i + 1 < n; i++) {
    if (buf[i] == LIN_SYNC && buf[i + 1] == pid) {
      start = static_cast<int>(i) + 2;
      break;
    }
  }

  bool ok = false;
  if (start >= 0 && n >= static_cast<size_t>(start) + 9) {
    const uint8_t *d = &buf[start];
    const uint8_t cs = d[8];
    const bool cs_enh = checksum_(d, 8, pid, true) == cs;
    const bool cs_cls = checksum_(d, 8, pid, false) == cs;

    bool accept = false;
    switch (this->checksum_mode_) {
      case CHECKSUM_CLASSIC:
        accept = cs_cls;
        break;
      case CHECKSUM_ENHANCED:
        accept = cs_enh;
        break;
      default:
        accept = cs_enh || cs_cls;
        break;
    }

    if (accept) {
      if (this->checksum_mode_ == CHECKSUM_AUTO && !this->checksum_detected_) {
        this->detected_enhanced_ = cs_enh;
        this->checksum_detected_ = true;
        ESP_LOGI(TAG, "Checksummen-Typ erkannt: %s", cs_enh ? "enhanced (LIN 2.x)" : "classic (LIN 1.x)");
      }
      this->handle_status_(d);
      ok = true;
    } else {
      ESP_LOGW(TAG, "Checksummenfehler: %s", format_hex_pretty(buf, n).c_str());
    }
  } else if (n > 0) {
    ESP_LOGV(TAG, "Unvollständige Antwort (%u Byte): %s", (unsigned) n, format_hex_pretty(buf, n).c_str());
  }

  if (ok) {
    this->missed_ = 0;
    if (!this->online_state_) {
      this->online_state_ = true;
      ESP_LOGI(TAG, "Kühlschrank antwortet");
#ifdef USE_BINARY_SENSOR
      if (this->online_ != nullptr)
        this->online_->publish_state(true);
#endif
    }
  } else if (++this->missed_ >= this->offline_after_ && this->online_state_) {
    ESP_LOGW(TAG, "Keine Antwort vom Kühlschrank (%u Versuche)", this->missed_);
    this->set_offline_();
  }
}

void ThetfordCI::handle_status_(const uint8_t *d) {
  std::copy(d, d + 8, this->last_status_.begin());
  this->have_status_ = true;

  const uint8_t mode = d[0];
  const uint8_t level = d[1];
  const uint8_t code = d[3];
  const bool running = this->mode_is_on_(mode);
  const bool night = running && (mode & NIGHT_BIT);
  const uint8_t fridge = level & 0x0F;
  const uint8_t freezer = (level >> 4) & 0x0F;
  const float volts = d[5] / 10.0f;

  ESP_LOGD(TAG, "RX: %s | Modus %s, Stufe K%u/G%u, Code %u, %.1f V", format_hex_pretty(d, 8).c_str(),
           mode_to_string_(mode), fridge, freezer, code, volts);

#ifdef USE_SENSOR
  if (this->supply_voltage_ != nullptr)
    this->supply_voltage_->publish_state(volts);
  if (this->fridge_level_ != nullptr)
    this->fridge_level_->publish_state(fridge);
  if (this->freezer_level_ != nullptr)
    this->freezer_level_->publish_state(freezer);
  if (this->error_code_ != nullptr)
    this->error_code_->publish_state(code);
  if (this->mode_raw_ != nullptr)
    this->mode_raw_->publish_state(mode);
#endif
#ifdef USE_BINARY_SENSOR
  if (this->running_ != nullptr)
    this->running_->publish_state(running);
  if (this->night_ != nullptr)
    this->night_->publish_state(night);
  if (this->fault_ != nullptr)
    this->fault_->publish_state(code != 0);
#endif
#ifdef USE_TEXT_SENSOR
  if (this->mode_text_ != nullptr) {
    std::string s = mode_to_string_(mode);
    if (night)
      s += " (Nacht)";
    this->mode_text_->publish_state(s);
  }
  if (this->status_text_ != nullptr) {
    const char *e = error_to_string_(code);
    this->status_text_->publish_state(e != nullptr ? std::string(e) : str_sprintf("Code %u (unbekannt)", code));
  }
  if (this->raw_frame_ != nullptr)
    this->raw_frame_->publish_state(format_hex_pretty(d, 8));
#endif
  this->republish_controls_();
}

void ThetfordCI::set_offline_() {
  this->online_state_ = false;
  this->have_status_ = false;
#ifdef USE_BINARY_SENSOR
  if (this->online_ != nullptr)
    this->online_->publish_state(false);
#endif
#ifdef USE_SENSOR
  for (auto *s : {this->supply_voltage_, this->fridge_level_, this->freezer_level_, this->error_code_, this->mode_raw_}) {
    if (s != nullptr)
      s->publish_state(NAN);
  }
#endif
}

void ThetfordCI::republish_controls_() {
  if (!this->have_status_)
    return;
  const uint8_t mode = this->last_status_[0];
  const bool running = this->mode_is_on_(mode);
#ifdef USE_SWITCH
  if (this->power_switch_ != nullptr)
    this->power_switch_->publish_state(running);
  if (this->night_switch_ != nullptr)
    this->night_switch_->publish_state(running && (mode & NIGHT_BIT));
#endif
#ifdef USE_NUMBER
  if (this->fridge_level_number_ != nullptr)
    this->fridge_level_number_->publish_state(this->last_status_[1] & 0x0F);
  if (this->freezer_level_number_ != nullptr)
    this->freezer_level_number_->publish_state((this->last_status_[1] >> 4) & 0x0F);
#endif
}

// ---------------------------------------------------------------------------
// Steuerung
// ---------------------------------------------------------------------------

bool ThetfordCI::can_write_(const char *what) {
  if (!this->allow_write_) {
    ESP_LOGW(TAG, "%s verworfen: Schreiben ist gesperrt (allow_write: false)", what);
    return false;
  }
  if (!this->have_status_) {
    ESP_LOGW(TAG, "%s verworfen: noch kein gültiger Status vom Kühlschrank", what);
    return false;
  }
  if (this->checksum_mode_ == CHECKSUM_AUTO && !this->checksum_detected_) {
    ESP_LOGW(TAG, "%s verworfen: Checksummen-Typ noch nicht erkannt", what);
    return false;
  }
  return true;
}

void ThetfordCI::queue_control_(uint8_t mode, uint8_t level) {
  this->control_.fill(0x00);
  this->control_[0] = mode;
  this->control_[1] = level;
  this->control_pending_ = this->write_repeat_;
  ESP_LOGI(TAG, "Steuerbefehl vorgemerkt: Modus 0x%02X, Stufe 0x%02X", mode, level);
  this->set_timeout("thetford_next", 10, [this]() { this->update(); });
}

bool ThetfordCI::request_power(bool on) {
  if (!this->can_write_("Ein/Aus")) {
    this->republish_controls_();
    return false;
  }
  uint8_t mode = on ? this->on_mode_ : this->off_mode_;
  if (on && (this->last_status_[0] & NIGHT_BIT) && this->mode_is_on_(this->last_status_[0]))
    mode |= NIGHT_BIT;
  this->queue_control_(mode, this->last_status_[1]);
  return true;
}

bool ThetfordCI::request_night(bool on) {
  if (!this->can_write_("Nachtmodus")) {
    this->republish_controls_();
    return false;
  }
  if (!this->mode_is_on_(this->last_status_[0])) {
    ESP_LOGW(TAG, "Nachtmodus verworfen: Kühlschrank ist aus");
    this->republish_controls_();
    return false;
  }
  uint8_t mode = (this->last_status_[0] & MODE_MASK) | (on ? NIGHT_BIT : 0);
  this->queue_control_(mode, this->last_status_[1]);
  return true;
}

bool ThetfordCI::request_level(LevelType type, uint8_t level) {
  if (!this->can_write_("Stufe")) {
    this->republish_controls_();
    return false;
  }
  uint8_t lv = this->last_status_[1];
  if (type == LevelType::FRIDGE) {
    lv = (lv & 0xF0) | (level & 0x0F);
  } else {
    lv = (lv & 0x0F) | ((level & 0x0F) << 4);
  }
  this->queue_control_(this->last_status_[0], lv);
  return true;
}

// ---------------------------------------------------------------------------
// Dekodierung
// ---------------------------------------------------------------------------

bool ThetfordCI::mode_is_on_(uint8_t mode) const {
  switch (mode & MODE_MASK) {
    case 0x01:  // an / Auto
    case 0x03:  // Gas (nur N4000)
    case 0x05:  // 12 V
    case 0x07:  // 230 V (nur N4000)
      return true;
    default:
      return false;
  }
}

const char *ThetfordCI::mode_to_string_(uint8_t mode) {
  switch (mode & MODE_MASK) {
    case 0x00:
    case 0x02:
      return "Aus";
    case 0x01:
      return "An";
    case 0x03:
      return "Gas";
    case 0x04:
      return "Störung Batterie?";
    case 0x05:
      return "12 V";
    case 0x06:
      return "Störung?";
    case 0x07:
      return "230 V";
    default:
      return "?";
  }
}

const char *ThetfordCI::error_to_string_(uint8_t code) {
  switch (code) {
    case 0:
      return "OK";
    case 3:
      return "Fehler Gas";
    case 4:
      return "Fehler 12-V-Heizstab";
    case 6:
      return "Fehler 12 V";
    case 7:
      return "Fehler D+";
    case 8:
      return "Fehler 230-V-Heizstab";
    case 9:
      return "Fehler Steuerung";
    case 10:
      return "Fehler 230 V";
    case 11:
      return "Keine Energiequelle";
    case 13:
      return "Fehler Temperaturfühler";
    default:
      return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Entitäten
// ---------------------------------------------------------------------------

#ifdef USE_SWITCH
void ThetfordSwitch::write_state(bool state) {
  if (this->type_ == SwitchType::POWER) {
    this->parent_->request_power(state);
  } else {
    this->parent_->request_night(state);
  }
  // Zustand wird erst mit dem nächsten Status-Frame bestätigt (republish_controls_).
}
#endif

#ifdef USE_NUMBER
void ThetfordLevel::control(float value) {
  if (std::isnan(value))
    return;
  this->parent_->request_level(this->type_, static_cast<uint8_t>(std::lround(value)));
}
#endif

}  // namespace thetford_ci
}  // namespace esphome
