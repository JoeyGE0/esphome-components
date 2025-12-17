#pragma once

#include "esphome/core/automation.h"
#include "crow_alarm_panel.h"

namespace esphome {
namespace crow_alarm_panel {

template<typename... Ts> class ArmAwayAction : public Action<Ts...> {
 public:
  explicit ArmAwayAction(CrowAlarmPanel *parent) : parent_(parent) {}

  void play(Ts... x) override { this->parent_->arm_away(); }

 protected:
  CrowAlarmPanel *parent_;
};

template<typename... Ts> class ArmStayAction : public Action<Ts...> {
 public:
  explicit ArmStayAction(CrowAlarmPanel *parent) : parent_(parent) {}

  void play(Ts... x) override { this->parent_->arm_stay(); }

 protected:
  CrowAlarmPanel *parent_;
};

template<typename... Ts> class DisarmAction : public Action<Ts...> {
 public:
  explicit DisarmAction(CrowAlarmPanel *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(std::string, code)

  void play(Ts... x) override {
    auto code_val = this->code_.value(x...);
    this->parent_->disarm(code_val);
  }

 protected:
  CrowAlarmPanel *parent_;
};

template<typename... Ts> class KeypressAction : public Action<Ts...> {
 public:
  explicit KeypressAction(CrowAlarmPanel *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(uint8_t, key)

  void play(Ts... x) override {
    auto key_val = this->key_.value(x...);
    this->parent_->keypress(key_val);
  }

 protected:
  CrowAlarmPanel *parent_;
};

}  // namespace crow_alarm_panel
}  // namespace esphome
