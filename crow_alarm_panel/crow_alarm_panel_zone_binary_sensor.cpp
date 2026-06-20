#include "crow_alarm_panel_zone_binary_sensor.h"

namespace esphome {
namespace crow_alarm_panel {

void CrowAlarmPanelZoneBinarySensor::publish_zone_state(bool open, bool bypassed) {
  const bool bypass_changed = bypassed != this->bypassed_;
  this->bypassed_ = bypassed;

  if (!this->has_state() || this->state != open || bypass_changed) {
    this->publish_state(open);
  }
}

}  // namespace crow_alarm_panel
}  // namespace esphome
