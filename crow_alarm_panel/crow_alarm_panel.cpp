#include "crow_alarm_panel.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace crow_alarm_panel {

static const char *TAG = "crow_alarm_panel";

static int decode_year_suffix_from_byte(uint8_t yb) {
  int hi = (yb >> 4) & 0x0F;
  int lo = yb & 0x0F;
  if (hi <= 9 && lo <= 9) {
    return hi * 10 + lo;
  }
  return (int) yb;
}

void CrowAlarmPanelStore::setup(InternalGPIOPin *clock_pin, InternalGPIOPin *data_pin) {
  clock_pin->setup();
  data_pin->setup();
  this->clock_pin_ = clock_pin->to_isr();
  this->data_pin_ = data_pin->to_isr();
  memset(this->buffer, 0, sizeof(this->buffer));
  memset(this->buffer2, 0, sizeof(this->buffer2));
  this->data_length = 0;
  this->num_bits_ = 0;
  this->boundary_buffer_ = 0;
  clock_pin->attach_interrupt(CrowAlarmPanelStore::interrupt, this, gpio::INTERRUPT_FALLING_EDGE);
}

std::string binary_indices(uint8_t byte) {
  std::string str;
  for (uint8_t i = 0; i < 8; i++) {
    if ((byte >> i) & 0x01) {
      if (str.length() > 0) {
        str += ",";
      }
      str += to_string(i + 1);
    }
  }
  return str;
}

void IRAM_ATTR HOT CrowAlarmPanelStore::interrupt(CrowAlarmPanelStore *arg) {
  uint32_t now = micros();
  arg->last_clock_time_ = now;  // Track last clock edge for bus idle detection

  // Clock glitch filtering - ignore edges that are too close together
  if (now - arg->prev_falling_edge_time_us_ < MIN_FALLING_EDGE_INTERVAL_US) {
    return;  // Glitch detected, ignore this edge
  }
  arg->prev_falling_edge_time_us_ = now;

  // Handle receive mode
  bool data_bit = arg->data_pin_.digital_read();

  if (!arg->data && data_bit)
    return;

  arg->data = true;

  // Check for boundary
  arg->boundary_buffer_ = (uint8_t) ((arg->boundary_buffer_ << 1) | data_bit);

  if (arg->inside_) {
    uint8_t idx = arg->num_bits_ / 8;
    arg->buffer[idx] = (arg->buffer[idx] >> 1) | ((data_bit ? 1 : 0) << 7);
    arg->num_bits_++;

    if (arg->boundary_buffer_ == BOUNDARY) {
      //  Save data
      memcpy(arg->buffer2, arg->buffer, arg->num_bits_ / 8);
      arg->data_length = arg->num_bits_ / 8;
      //  Reset
      memset(arg->buffer, 0, BUFFER_LENGTH);
      arg->boundary_buffer_ = 0;
      arg->inside_ = false;
      arg->num_bits_ = 0;
      arg->data = false;
      return;
    } else if (arg->num_bits_ >= BUFFER_LENGTH * 8) {
      // Wrong side of boundary.
      arg->inside_ = false;
      arg->num_bits_ = 0;
      memset(arg->buffer, 0, BUFFER_LENGTH);
      arg->boundary_buffer_ = 0;
      arg->data = false;  // clear data flag on overflow
    }
  }

  if (arg->boundary_buffer_ == BOUNDARY) {
    arg->inside_ = true;
    return;
  }
}

void CrowAlarmPanel::setup() {
  this->store_.setup(this->clock_pin_, this->data_pin_);

  // Ensure our configured keypad address is present for logging/lookup
  bool have_self = false;
  for (const auto &kp : this->keypads_) {
    if (kp.address == this->keypad_address_) {
      have_self = true;
      break;
    }
  }
  if (!have_self) {
    this->keypads_.push_back(CrowAlarmPanelKeypad{
        .name = "Virtual Keypad",
        .address = this->keypad_address_,
    });
  }

  if (this->armed_state_ != nullptr) {
    this->armed_state_->publish_state("disarmed");
  }
  if (this->hardware_version_ != nullptr) {
    this->hardware_version_->publish_state("unknown");
  }
  if (this->firmware_version_ != nullptr) {
    this->firmware_version_->publish_state("unknown");
  }
  if (this->panel_time_ != nullptr) {
    this->panel_time_->publish_state("unknown");
  }
  if (this->panel_date_ != nullptr) {
    this->panel_date_->publish_state("unknown");
  }
  if (this->panel_year_ != nullptr) {
    this->panel_year_->publish_state("unknown");
  }
  if (this->suspected_temperature_ != nullptr) {
    this->suspected_temperature_->publish_state("unknown");
  }
  if (this->alarm_control_panel_ != nullptr) {
    this->alarm_control_panel_->publish_state(alarm_control_panel::ACP_STATE_DISARMED);
  }
};

void CrowAlarmPanel::dump_config() {
  ESP_LOGCONFIG(TAG, "Crow Alarm Panel:");
  LOG_PIN("  Clock Pin: ", this->clock_pin_);
  LOG_PIN("  Data Pin: ", this->data_pin_);
}

CrowAlarmPanelKeypad CrowAlarmPanel::find_keypad_(uint8_t address) {
  for (CrowAlarmPanelKeypad keypad : this->keypads_) {
    if (keypad.address == address) {
      return keypad;
    }
  }
  return {};
}

void CrowAlarmPanel::apply_battery_low_heuristic_() {
  if (this->battery_state_experimental_ == nullptr) {
    return;
  }
  if (!this->mains_fault_active_) {
    this->battery_state_experimental_->publish_state(false);
    return;
  }
  if (!this->last_time_prefix_valid_) {
    return;
  }
  const uint8_t d0 = this->last_time_dow_;
  const uint8_t d1 = this->last_time_h_;
  const uint8_t d2 = this->last_time_m_;
  const bool unplug_style = (d0 == 0x01 && d1 == 0x01);
  const bool bat_low_hint = (d0 == 0x01 && d1 == 0x00 &&
                             (d2 == 0xC2 || d2 == 0xC3 || (d2 >= 0x80 && d2 < 0xC0)));
  if (unplug_style) {
    this->battery_state_experimental_->publish_state(false);
  } else if (bat_low_hint) {
    this->battery_state_experimental_->publish_state(true);
  }
}

void CrowAlarmPanel::loop() {
  if (this->store_.data_length) {
    if (this->store_.data_length < 2) {
      ESP_LOGW(TAG, "Discarding short frame (%d bytes)", this->store_.data_length);
      InterruptLock lock;
      memset(this->store_.buffer2, 0, BUFFER_LENGTH);
      this->store_.data_length = 0;
      return;
    }

    uint8_t type;
    std::vector<uint8_t> data;
    {
      InterruptLock lock;
      type = this->store_.buffer2[0];
      data.insert(data.begin(), this->store_.buffer2 + 1, this->store_.buffer2 + this->store_.data_length - 1);
      memset(this->store_.buffer2, 0, BUFFER_LENGTH);
      this->store_.data_length = 0;
    }

    ESP_LOGD(TAG, "Received data: [%02x.%s]", type, format_hex_pretty(data).c_str());

    switch (type) {
      case OUTPUT_STATE:
        if (data.size() < 1) {
          ESP_LOGW(TAG, "Output state too short, discarding");
          break;
        }
        ESP_LOGD(TAG, "Output state [%s]", format_hex_pretty(data).c_str());
        for (CrowAlarmPanelOutput output : this->outputs_) {
          bool on = ((data[0] >> (output.number - 1)) & 0x01);
          output.the_switch->publish_state(on);
        }
        break;
      case ZONE_STATE: {
        if (data.size() < 6) {
          ESP_LOGW(TAG, "Zone state invalid length, discarding");
          return;
        }
        ESP_LOGD(TAG, "Zone state received [%s]", format_hex_pretty(data).c_str());
        bool clear = true;
        for (CrowAlarmPanelZone zone : this->zones_) {
          bool triggered = ((data[1] >> (zone.zone - 1)) & 0x01);
          bool triggered_alarmed = ((data[2] >> (zone.zone - 1)) & 0x01);
          bool bypassed = ((data[3] >> (zone.zone - 1)) & 0x01);

          if (zone.motion_binary_sensor != nullptr) {
            zone.motion_binary_sensor->publish_state(triggered | triggered_alarmed);
          }
          if (zone.bypass_binary_sensor != nullptr) {
            zone.bypass_binary_sensor->publish_state(bypassed);
          }

          if (triggered) {
            ESP_LOGD(TAG, "Zone %d active", zone.zone);
            if (this->armed_state_ != nullptr && this->armed_state_->state != "arming") {
              this->armed_state_->publish_state("disarmed");  // Assume disarmed if motion detected in this byte
            }
            if (this->alarm_control_panel_ != nullptr &&
                this->alarm_control_panel_->get_state() != alarm_control_panel::ACP_STATE_ARMING) {
              this->alarm_control_panel_->publish_state(alarm_control_panel::ACP_STATE_DISARMED);
            }
            clear = false;
          }
          if (triggered_alarmed) {
            if (this->armed_state_ != nullptr) {
              this->armed_state_->publish_state("pending");
            }
            if (this->alarm_control_panel_ != nullptr) {
              this->alarm_control_panel_->publish_state(alarm_control_panel::ACP_STATE_PENDING);
            }
            ESP_LOGD(TAG, "Alarm pending from zone %d", zone.zone);
            clear = false;
          }
        }
        if (clear) {
          ESP_LOGD(TAG, "All zones clear");
        }
        break;
      }
      case ARMED_STATE: {
        if (armed_state_ != nullptr) {
          if (data[0] == 0x00 && data[1] == 0x01) {
            this->armed_state_->publish_state("arming");
            ESP_LOGD(TAG, "Arming [%02x.%s]", type, format_hex_pretty(data).c_str());
            if (this->alarm_control_panel_ != nullptr) {
              this->alarm_control_panel_->publish_state(alarm_control_panel::ACP_STATE_ARMING);
            }
          } else if (data[0] == 0x01 && data[1] == 0x00) {
            this->armed_state_->publish_state("armed_away");
            ESP_LOGD(TAG, "Armed Away [%02x.%s]", type, format_hex_pretty(data).c_str());
            if (this->alarm_control_panel_ != nullptr) {
              this->alarm_control_panel_->publish_state(alarm_control_panel::ACP_STATE_ARMED_AWAY);
            }
          } else if (data[0] == 0x00 && data[1] == 0x00) {
            this->armed_state_->publish_state("disarmed");
            ESP_LOGD(TAG, "Disarmed [%02x.%s]", type, format_hex_pretty(data).c_str());
            if (this->alarm_control_panel_ != nullptr) {
              this->alarm_control_panel_->publish_state(alarm_control_panel::ACP_STATE_DISARMED);
            }
          } else {
            ESP_LOGD(TAG, "Armed state unknown [%02x.%s]", type, format_hex_pretty(data).c_str());
          }
        }
        break;
      }
      case PANEL_READY: {
        if (data.size() < 3) {
          break;
        }
        if (this->panel_ready_ != nullptr) {
          uint8_t b = data[2];
          if (b == 0xC1 || b == 0xC0 || b == 0x60) {
            this->panel_ready_->publish_state(b == 0xC1);
          }
        }
        if (data[0] == 0x00) {
          uint8_t b1 = data[1];
          uint8_t b2 = data[2];
          const bool mains_fault = ((b1 == 0x02 || b1 == 0x03) && (b2 == 0xC2 || b2 == 0xC3));
          const bool mains_ok = (b1 == 0x00 && (b2 == 0xC0 || b2 == 0xC1));
          if (mains_fault) {
            this->mains_fault_active_ = true;
          } else if (mains_ok) {
            this->mains_fault_active_ = false;
          }
          if (this->mains_power_ != nullptr) {
            if (mains_fault) {
              this->mains_power_->publish_state(true);
            } else if (mains_ok) {
              this->mains_power_->publish_state(false);
            }
          }
          this->apply_battery_low_heuristic_();
        }
        break;
      }
      case PANEL_INFO: {
        const bool need_hwfw = this->hardware_version_ != nullptr || this->firmware_version_ != nullptr;
        const bool need_suspect = this->suspected_temperature_ != nullptr;
        if (!need_hwfw && !need_suspect) {
          break;
        }
        if (need_hwfw) {
          if (data.size() < 5) {
            ESP_LOGW(TAG, "Panel info (0x23) too short for HW/FW");
          } else {
            int main = (int) ((uint16_t(data[1]) << 8) | data[2]);
            int s1 = data[3];
            int s2 = data[4];
            char hw[16];
            char fw[16];
            snprintf(hw, sizeof(hw), "v%d", main);
            snprintf(fw, sizeof(fw), "v%d.%d", s1, s2);
            if (this->hardware_version_ != nullptr) {
              this->hardware_version_->publish_state(hw);
            }
            if (this->firmware_version_ != nullptr) {
              this->firmware_version_->publish_state(fw);
            }
          }
        }
        if (need_suspect && !data.empty()) {
          char sbuf[160];
          if (data.size() >= 8) {
            snprintf(sbuf, sizeof(sbuf),
                     "b0=0x%02X tail=%02X.%02X.%02X | unverified (legacy guess: temp in 0x23); b6=%u dec",
                     data[0], data[5], data[6], data[7], (unsigned) data[6]);
          } else if (data.size() >= 6) {
            snprintf(sbuf, sizeof(sbuf),
                     "b0=0x%02X partial=%02X.%02X | unverified (legacy guess: temp in 0x23); b6=%u dec",
                     data[0], data[5], data[6], (unsigned) data[6]);
          } else if (data.size() >= 1) {
            snprintf(sbuf, sizeof(sbuf),
                     "only %zu B, b0=0x%02X | unverified (legacy guess: temp in 0x23)", data.size(), data[0]);
          } else {
            snprintf(sbuf, sizeof(sbuf), "empty payload | unverified (legacy guess: temp in 0x23)");
          }
          this->suspected_temperature_->publish_state(sbuf);
        }
        ESP_LOGD(TAG, "Panel info (0x23) [%s]", format_hex_pretty(data).c_str());
        break;
      }
      case KEYPRESS: {
        if (data.size() < 2) {
          ESP_LOGW(TAG, "Keypress too short, discarding");
          break;
        }
        uint8_t key = data[1];
        if (key >= sizeof(KEYS) / sizeof(KEYS[0])) {
          ESP_LOGW(TAG, "Unknown key index %d, discarding", key);
          break;
        }
        CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "Key %s (%d) pressed on %s [%02x.%s]", KEYS[key], key, keypad.name.c_str(), type,
                 format_hex_pretty(data).c_str());
        break;
      }
      case CURRENT_TIME: {
        if (data.size() < 7) {
          ESP_LOGW(TAG, "Current time too short, discarding");
          break;
        }
        if (data[0] == 0 || data[0] > 7) {
          ESP_LOGW(TAG, "Current time has invalid day index %d", data[0]);
          break;
        }
        this->last_time_dow_ = data[0];
        this->last_time_h_ = data[1];
        this->last_time_m_ = data[2];
        this->last_time_prefix_valid_ = true;
        this->apply_battery_low_heuristic_();

        const char *day_of_week = DAYS[data[0] - 1];
        int hour = data[1];
        int mins = data[2];
        if (mins >= 60) {
          hour += mins / 60;
          mins %= 60;
        }
        uint8_t sb = data[3];
        int sec = 0;
        bool phase_ok = false;
        if (sb == 0x00) {
          sec = 0;
          phase_ok = true;
        } else if (sb == 0x0F) {
          sec = 15;
          phase_ok = true;
        } else if (sb == 0x1E) {
          sec = 30;
          phase_ok = true;
        } else if (sb == 0x2D) {
          sec = 45;
          phase_ok = true;
        }
        bool date_trusted = (sb == 0x00 || sb == 0x1E || sb == 0x2D);
        int day = data[4];
        int month = data[5];
        uint8_t yb = data[6];
        int naive_yy = decode_year_suffix_from_byte(yb);
        int naive_year = 2000 + naive_yy;

        if (date_trusted && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
          int year = naive_year;
          if (year < 2020 || year > 2039) {
            if (this->clock_time_valid_ && this->pt_year_ >= 2020 && this->pt_year_ <= 2039) {
              year = this->pt_year_;
            } else {
              year = 2026;
            }
          } else if (this->clock_time_valid_ && this->pt_year_ >= 2020 && this->pt_year_ <= 2039) {
            int prev = this->pt_year_;
            if (year > prev + 2 || year < prev - 2) {
              year = prev;
            }
          }
          this->pt_year_ = year;
          this->pt_month_ = month;
          this->pt_day_ = day;
        }

        if (phase_ok && hour <= 23 && mins <= 59) {
          this->pt_h_ = hour;
          this->pt_m_ = mins;
          this->pt_s_ = sec;
          this->clock_time_valid_ = true;
        }

        if (this->clock_time_valid_ &&
            (this->panel_time_ != nullptr || this->panel_date_ != nullptr || this->panel_year_ != nullptr)) {
          if (this->panel_time_ != nullptr) {
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", this->pt_h_, this->pt_m_, this->pt_s_);
            this->panel_time_->publish_state(tbuf);
          }
          if (this->pt_month_ >= 1 && this->pt_month_ <= 12 && this->pt_day_ >= 1 && this->pt_day_ <= 31 &&
              this->pt_year_ >= 2020) {
            char dbuf[16];
            char ybuf[8];
            snprintf(dbuf, sizeof(dbuf), "%04d-%02d-%02d", this->pt_year_, this->pt_month_, this->pt_day_);
            snprintf(ybuf, sizeof(ybuf), "%04d", this->pt_year_);
            if (this->panel_date_ != nullptr) {
              this->panel_date_->publish_state(dbuf);
            }
            if (this->panel_year_ != nullptr) {
              this->panel_year_->publish_state(ybuf);
            }
          }
        }

        ESP_LOGV(TAG, "Date/Time %s 20%02d-%02d-%02d %02d:%02d:%02d", day_of_week, data[6], data[5], data[4], hour,
                 mins, data[3]);
        break;
      }
      case RESPONSE_TIME:
        if (data.size() < 3) {
          ESP_LOGW(TAG, "Response time too short, discarding");
          break;
        }
        ESP_LOGD(TAG, "Current time setting received [%d]", (data[1] << 8) | data[2]);
        break;
      case KEYPAD_COMMAND: {
        if (data.empty()) {
          ESP_LOGW(TAG, "Keypad command too short, discarding");
          break;
        }
        CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "%s command [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        break;
      }
      case KEYPAD_STATE: {
        if (data.size() < 2) {
          ESP_LOGW(TAG, "Keypad state too short, discarding");
          break;
        }
        CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        if (data[1] == 00) {
          ESP_LOGD(TAG, "%s in normal state [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        } else if (data[1] == 02) {
          ESP_LOGD(TAG, "%s in installer mode [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        } else if (data[1] == 03) {
          if (data.size() < 3) {
            ESP_LOGW(TAG, "Keypad programming state too short, discarding");
            break;
          }
          ESP_LOGD(TAG, "%s programming %d [%02x.%s]", keypad.name.c_str(), data[2], type,
                   format_hex_pretty(data).c_str());
        } else {
          ESP_LOGD(TAG, "%s state unknown [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        }
        break;
      }
      case SETTING_VALUE: {
        if (data.size() < 5) {
          ESP_LOGW(TAG, "Setting value too short, discarding");
          break;
        }
        ESP_LOGD(TAG, "Address %d-%d has options: %s [%02x.%s]", data[3], data[4], binary_indices(data[2]).c_str(),
                 type, format_hex_pretty(data).c_str());
        break;
      }
      case SETTING_VALUE2: {
        if (data.size() < 4) {
          ESP_LOGW(TAG, "Setting value 2 too short, discarding");
          break;
        }
        // CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "Address %d-%d has value %d [%02x.%s]", data[2], data[3], data[1], type,
                 format_hex_pretty(data).c_str());
        break;
      }
      case SETTING_VALUE3: {
        if (data.size() < 5) {
          ESP_LOGW(TAG, "Setting value 3 too short, discarding");
          break;
        }
        // CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "Address %d-%d has value %d [%02x.%s]", data[3], data[4], (data[1] << 8) | data[2], type,
                 format_hex_pretty(data).c_str());
        break;
      }
      case MEMORY_EVENT: {
        if (data.size() < 2) {
          ESP_LOGW(TAG, "Memory event too short, discarding");
          break;
        }
        // CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "Memory event #%d ", data[1]);
        break;
      }
      default:
        ESP_LOGD(TAG, "Unknown [%02x.%s]", type, format_hex_pretty(data).c_str());
        break;
    }
    this->on_message_trigger_->trigger(type, data);
  }

  // Send queued keypresses without blocking the caller that enqueued them
  if (!this->keypress_queue_.empty() && this->is_bus_idle_()) {
    const uint32_t now_ms = millis();
    if (now_ms - this->last_keypress_sent_ms_ >= 500) {
      uint8_t key = this->keypress_queue_.front();
      this->keypress_queue_.erase(this->keypress_queue_.begin());
      ESP_LOGD(TAG, "Sending queued keypress: %s (%d)", KEYS[key], key);
      this->keypress(key);
      this->last_keypress_sent_ms_ = now_ms;
      
      // Clear disarm flag when ENTER is sent during a disarm operation
      if (key == KEY_ENTER && this->disarm_in_progress_) {
        this->disarm_in_progress_ = false;
        this->arm_in_progress_ = false;  // Also clear arm flag since disarm completes any arm operation
        ESP_LOGD(TAG, "Disarm sequence completed, clearing disarm and arm flags");
      }
      
      // Clear arm flag when ARM or STAY keys are sent during an arm operation
      if ((key == KEY_ARM || key == KEY_STAY) && this->arm_in_progress_) {
        this->arm_in_progress_ = false;
        this->disarm_in_progress_ = false;  // Also clear disarm flag since arm completes any disarm operation
        ESP_LOGD(TAG, "Arm sequence completed, clearing arm and disarm flags");
      }
    }
  }
  
  // Timeout protection: Clear disarm flag if it's been too long (30 seconds)
  if (this->disarm_in_progress_ && (millis() - this->disarm_started_ms_ > 30000)) {
    ESP_LOGW(TAG, "Disarm timeout reached, clearing disarm flag");
    this->disarm_in_progress_ = false;
  }
  
  // Timeout protection: Clear arm flag if it's been too long (30 seconds)
  if (this->arm_in_progress_ && (millis() - this->arm_started_ms_ > 30000)) {
    ESP_LOGW(TAG, "Arm timeout reached, clearing arm flag");
    this->arm_in_progress_ = false;
  }
}

bool CrowAlarmPanel::is_bus_idle_() {
  // Check if we're currently inside a message
  if (this->store_.inside_) {
    ESP_LOGV(TAG, "Bus not idle: inside message boundary");
    return false;
  }

  // Check if data line is pulled low (someone is transmitting)
  // Data line pulls high when idle, so low means active transmission
  if (!this->data_pin_->digital_read()) {
    ESP_LOGV(TAG, "Bus not idle: data line is low (active transmission)");
    return false;
  }

  // Check minimum interval since last transmission (anti-spam)
  uint32_t now_ms = millis();
  uint32_t time_since_tx = now_ms - this->store_.last_transmission_time_;

  if (time_since_tx < CrowAlarmPanelStore::MIN_TX_INTERVAL_MS) {
    ESP_LOGV(TAG, "Bus not idle: only %ums since last TX (need %ums)", time_since_tx,
             CrowAlarmPanelStore::MIN_TX_INTERVAL_MS);
    return false;
  }

  ESP_LOGV(TAG, "Bus is idle: not inside message, data line high, %ums since last TX", time_since_tx);
  return true;
}

/**
 * @brief Wait for the clock pin to reach the desired state, with timeout.
 * @return true if the desired state was reached, false on timeout.
 */
bool CrowAlarmPanel::wait_for_clock_edge_(bool wait_for_state, uint32_t timeout_us) {
  const uint32_t start = micros();
  while (this->clock_pin_->digital_read() != wait_for_state) {
    if (micros() - start >= timeout_us) {
      return false;
    }
    delayMicroseconds(10);  // Yield to watchdog/WiFi
  }
  return true;
}

/**
 * @brief Send a packet over the Crow alarm panel bus, blocking until complete.
 */
void IRAM_ATTR CrowAlarmPanel::send_packet_blocking_(const std::vector<uint8_t> &packet) {
  InterruptLock lock;

  // Convert bytes to bits (LSB first)
  bool bits[200];
  uint16_t bit_count = 0;
  for (uint8_t byte : packet) {
    for (uint8_t j = 0; j < 8; j++) {
      bits[bit_count++] = (byte >> j) & 1;
    }
  }

  // Wait for a full clock cycle to hopefully synchronize better
  if (!this->wait_for_clock_edge_(HIGH, CrowAlarmPanelStore::TX_START_TIMEOUT_US) ||
      !this->wait_for_clock_edge_(LOW, CrowAlarmPanelStore::TX_START_TIMEOUT_US) ||
      !this->wait_for_clock_edge_(HIGH, CrowAlarmPanelStore::TX_START_TIMEOUT_US)) {
    // Release data pin back to input (idle high via panel pull-up) before bailing
    this->data_pin_->pin_mode(gpio::FLAG_INPUT);
    return;
  }

  // Transmit each bit on clock falling edge
  for (uint16_t i = 0; i < bit_count; i++) {
    // Drive the next bit before the falling edge so the panel samples the correct value.
    // Use open-drain style: pull low for 0, release (input) for 1 to avoid fighting the panel.
    if (bits[i]) {
      this->data_pin_->pin_mode(gpio::FLAG_INPUT);  // release line for logic high
    } else {
      this->data_pin_->pin_mode(gpio::FLAG_OUTPUT);
      this->data_pin_->digital_write(LOW);  // actively pull low for 0
    }

    // Wait for falling edge to ensure the panel has sampled the bit
    if (!this->wait_for_clock_edge_(LOW, CrowAlarmPanelStore::TX_BIT_TIMEOUT_US)) {
      break;
    }

    // Wait for rising edge before next bit. This prevents the line from releasing high too early
    if (!this->wait_for_clock_edge_(HIGH, CrowAlarmPanelStore::TX_BIT_TIMEOUT_US)) {
      break;
    }
  }

  // Release data pin back to input (idle high via panel pull-up)
  this->data_pin_->pin_mode(gpio::FLAG_INPUT);

  // Track last transmission time
  this->store_.last_transmission_time_ = millis();
}

void CrowAlarmPanel::arm_away() {
  if (this->arm_in_progress_) {
    ESP_LOGW(TAG, "Arm operation already in progress, ignoring new arm request");
    return;
  }
  
  ESP_LOGD(TAG, "Arm away");
  this->arm_in_progress_ = true;
  this->arm_started_ms_ = millis();
  this->keypress(KEY_ARM);
}

void CrowAlarmPanel::arm_stay() {
  if (this->arm_in_progress_) {
    ESP_LOGW(TAG, "Arm operation already in progress, ignoring new arm request");
    return;
  }
  
  ESP_LOGD(TAG, "Arm stay");
  this->arm_in_progress_ = true;
  this->arm_started_ms_ = millis();
  this->keypress(KEY_STAY);
}

void CrowAlarmPanel::disarm(const std::string &code) {
  if (!this->is_armed()) {
    ESP_LOGW(TAG, "Cannot disarm - alarm is not armed (current state: %s)", 
             this->armed_state_ ? this->armed_state_->state.c_str() : "unknown");
    return;
  }
  
  if (this->disarm_in_progress_) {
    ESP_LOGW(TAG, "Disarm already in progress, ignoring new disarm request");
    return;
  }
  
  ESP_LOGD(TAG, "Disarm with code (queued)");
  this->disarm_in_progress_ = true;
  this->disarm_started_ms_ = millis();
  for (char c : code) {
    if (c >= '0' && c <= '9') {
      this->keypress_queue_.push_back(c - '0');
    }
  }
  this->keypress_queue_.push_back(KEY_ENTER);
}

void CrowAlarmPanel::set_output(uint8_t output, bool state) {}

void CrowAlarmPanel::send_packet(uint8_t type, const std::vector<uint8_t> &data) {
  // Build packet with boundaries
  std::vector<uint8_t> packet;
  packet.push_back(BOUNDARY);  // Start boundary
  packet.push_back(type);
  packet.push_back(this->keypad_address_);
  packet.insert(packet.end(), data.begin(), data.end());
  packet.push_back(BOUNDARY);                // End boundary
  packet.push_back(PACKET_COMPLETE_MARKER);  // Explicit end marker

  // Check if bus is idle
  while (!this->is_bus_idle_()) {
    ESP_LOGI(TAG, "Waiting for bus to become idle before sending packet...");
    delay(10); // Each packet takes around 30ms to fully send
    yield();
  }

  ESP_LOGI(TAG, "Sending packet: [%s]", format_hex_pretty(packet).c_str());

  // Send it (blocks until complete)
  this->send_packet_blocking_(packet);

  ESP_LOGI(TAG, "Packet sent");
}

void CrowAlarmPanel::keypress(uint8_t key) {
  ESP_LOGD(TAG, "Keypress: %s (%d)", KEYS[key], key);
  std::vector<uint8_t> data = {key};
  this->send_packet(KEYPRESS, data);
}

bool CrowAlarmPanel::is_armed() const {
  if (this->armed_state_ != nullptr) {
    return this->armed_state_->state != "disarmed";
  }
  if (this->alarm_control_panel_ != nullptr) {
    return this->alarm_control_panel_->get_state() != alarm_control_panel::ACP_STATE_DISARMED;
  }
  return false;
}
}  // namespace crow_alarm_panel
}  // namespace esphome
