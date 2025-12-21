#include "crow_alarm_panel.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#define HIGH 1
#define LOW 0

namespace esphome {
namespace crow_alarm_panel {

static const char *TAG = "crow_alarm_panel";

void CrowAlarmPanelStore::setup(InternalGPIOPin *clock_pin, InternalGPIOPin *data_pin) {
  clock_pin->setup();
  data_pin->setup();
  this->clock_pin_ = clock_pin->to_isr();
  this->data_pin_ = data_pin->to_isr();
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

void CrowAlarmPanel::loop() {
  if (this->store_.data_length) {
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
            if (this->armed_state_->state != "arming")
              this->armed_state_->publish_state("disarmed");  // Assume disarmed if motion detected in this byte
            clear = false;
          }
          if (triggered_alarmed) {
            this->armed_state_->publish_state("pending");
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
          } else if (data[0] == 0x01 && data[1] == 0x00) {
            this->armed_state_->publish_state("armed_away");
            ESP_LOGD(TAG, "Armed Away [%02x.%s]", type, format_hex_pretty(data).c_str());
          } else if (data[0] == 0x00 && data[1] == 0x00) {
            this->armed_state_->publish_state("disarmed");
            ESP_LOGD(TAG, "Disarmed [%02x.%s]", type, format_hex_pretty(data).c_str());
          } else {
            ESP_LOGD(TAG, "Armed state unknown [%02x.%s]", type, format_hex_pretty(data).c_str());
          }
        }
        break;
      }
      case KEYPRESS_ALT: {
        uint8_t key = data[1];
        CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "Key %s (%d) pressed on %s [%02x.%s]", KEYS_ALT[key], key, keypad.name.c_str(), type,
                 format_hex_pretty(data).c_str());
        break;
      }
      case CURRENT_TIME: {
        const char *day_of_week = DAYS[data[0] - 1];
        uint8_t mins = data[2];
        uint8_t hour = data[1];
        if (mins >= 60) {
          hour = hour + (mins / 60);
          mins = mins % 60;
        }
        ESP_LOGV(TAG, "Date/Time %s 20%02d-%02d-%02d %02d:%02d:%02d", day_of_week, data[6], data[5], data[4], hour,
                 mins, data[3]);
        // ESP_LOGD(TAG, "[%s]", format_hex_pretty(data).c_str());
        break;
      }
      case RESPONSE_TIME:
        ESP_LOGD(TAG, "Current time setting received [%d]", (data[1] << 8) | data[2]);
        break;
      case KEYPAD_COMMAND: {
        CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "%s command [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        break;
      }
      case KEYPAD_STATE: {
        CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        if (data[1] == 00) {
          ESP_LOGD(TAG, "%s in normal state [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        } else if (data[1] == 02) {
          ESP_LOGD(TAG, "%s in installer mode [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        } else if (data[1] == 03) {
          ESP_LOGD(TAG, "%s programming %d [%02x.%s]", keypad.name.c_str(), data[2], type,
                   format_hex_pretty(data).c_str());
        } else {
          ESP_LOGD(TAG, "%s state unknown [%02x.%s]", keypad.name.c_str(), type, format_hex_pretty(data).c_str());
        }
        break;
      }
      case SETTING_VALUE: {
        ESP_LOGD(TAG, "Address %d-%d has options: %s [%02x.%s]", data[3], data[4], binary_indices(data[2]).c_str(),
                 type, format_hex_pretty(data).c_str());
        break;
      }
      case SETTING_VALUE2: {
        // CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "Address %d-%d has value %d [%02x.%s]", data[2], data[3], data[1], type,
                 format_hex_pretty(data).c_str());
        break;
      }
      case SETTING_VALUE3: {
        // CrowAlarmPanelKeypad keypad = this->find_keypad_(data[0]);
        ESP_LOGD(TAG, "Address %d-%d has value %d [%02x.%s]", data[3], data[4], (data[1] << 8) | data[2], type,
                 format_hex_pretty(data).c_str());
        break;
      }
      case MEMORY_EVENT: {
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
      ESP_LOGD(TAG, "Sending queued keypress: %s (%d)", KEYS_ALT[key], key);
      this->keypress(key);
      this->last_keypress_sent_ms_ = now_ms;
    }
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

void CrowAlarmPanel::wait_for_clock_edge_(bool wait_for_state) {
  // Wait for clock to reach desired state
  while (this->clock_pin_->digital_read() != wait_for_state) {
    delayMicroseconds(10);  // Yield to watchdog/WiFi
  }
}

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
    this->wait_for_clock_edge_(HIGH);
    this->wait_for_clock_edge_(LOW);
    this->wait_for_clock_edge_(HIGH);

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
    this->wait_for_clock_edge_(LOW);

    // Wait for rising edge before next bit. This prevents the line from releasing high too early
    this->wait_for_clock_edge_(HIGH);
  }

  // Release data pin back to input (idle high via panel pull-up)
  this->data_pin_->pin_mode(gpio::FLAG_INPUT);

  // Track last transmission time
  this->store_.last_transmission_time_ = millis();
}

void CrowAlarmPanel::arm_away() {
  ESP_LOGD(TAG, "Arm away");
  this->keypress(KEY_ARM_ALT);  // ARM key
}

void CrowAlarmPanel::arm_stay() {
  ESP_LOGD(TAG, "Arm stay");
  this->keypress(KEY_STAY_ALT);  // STAY key
}

void CrowAlarmPanel::disarm(const std::string &code) {
  ESP_LOGI(TAG, "Disarm with code (queued)");
  for (char c : code) {
    if (c >= '0' && c <= '9') {
      this->keypress_queue_.push_back(c - '0');
    }
  }
  this->keypress_queue_.push_back(KEY_ENTER_ALT);
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
  ESP_LOGI(TAG, "Keypress: %s (%d)", KEYS_ALT[key], key);
  std::vector<uint8_t> data = {key};
  this->send_packet(KEYPRESS_ALT, data);
}

}  // namespace crow_alarm_panel
}  // namespace esphome
