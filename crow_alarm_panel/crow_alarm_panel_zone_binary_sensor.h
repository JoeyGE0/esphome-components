#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace crow_alarm_panel {

class CrowAlarmPanelZoneBinarySensor : public binary_sensor::BinarySensor {
 public:
  void publish_zone_state(bool open, bool bypassed) {
    const bool bypass_changed = bypassed != this->bypassed_;
    this->bypassed_ = bypassed;
    if (!this->has_state() || this->state != open || bypass_changed) {
      this->publish_state(open);
    }
  }

  bool get_bypassed() const { return this->bypassed_; }

 protected:
  bool bypassed_{false};
};

}  // namespace crow_alarm_panel
}  // namespace esphome
