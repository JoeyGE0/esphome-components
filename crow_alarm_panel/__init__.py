from typing import Optional

from esphome import pins, automation
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.components.binary_sensor as binary_sensor_comp
import esphome.components.text_sensor as text_sensor_comp
from esphome.components import alarm_control_panel as acp
from esphome.core import ID
from esphome.const import (
    CONF_ADDRESS,
    CONF_CLOCK_PIN,
    CONF_DATA_PIN,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ENTITY_CATEGORY_NONE,
)

AUTO_LOAD = ["binary_sensor", "text_sensor", "switch", "button", "alarm_control_panel"]
MULTI_CONF = True

CONF_CROW_ALARM_PANEL_ID = "crow_alarm_panel_id"
CONF_KEYPADS = "keypads"
CONF_ON_MESSAGE = "on_message"

crow_alarm_panel_ns = cg.esphome_ns.namespace("crow_alarm_panel")

CrowAlarmPanel = crow_alarm_panel_ns.class_("CrowAlarmPanel", cg.Component)
CrowAlarmPanelZoneBinarySensor = crow_alarm_panel_ns.class_(
    "CrowAlarmPanelZoneBinarySensor", binary_sensor_comp.BinarySensor
)
CrowAlarmControlPanel = crow_alarm_panel_ns.class_(
    "CrowAlarmControlPanel", acp.AlarmControlPanel, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CrowAlarmPanel),
        cv.Required(CONF_CLOCK_PIN): pins.internal_gpio_input_pin_schema,
        cv.Required(CONF_DATA_PIN): pins.internal_gpio_input_pin_schema,
        cv.Optional(CONF_ADDRESS): cv.int_range(min=0, max=8),
        cv.Optional(CONF_KEYPADS, default=[]): cv.ensure_list(
            cv.Schema(
                {
                    cv.Required(CONF_NAME): cv.string,
                    cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=8),
                }
            )
        ),
        cv.Optional(CONF_ON_MESSAGE): automation.validate_automation(single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


def _default_entity_name(config, title: str) -> str:
    base = config.get(CONF_NAME)
    return f"{base} {title}" if base else title


async def _register_diagnostic_text_sensor(
    config, parent_id, suffix: str, title: str, *, icon: Optional[str] = None
):
    sch = text_sensor_comp.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC)
    child_id = ID(
        f"{parent_id.id}_{suffix}",
        is_declaration=True,
        type=text_sensor_comp.TextSensor,
    )
    body = {
        CONF_ID: child_id,
        CONF_NAME: _default_entity_name(config, title),
    }
    if icon is not None:
        body[CONF_ICON] = icon
    sens_cfg = sch(body)
    return await text_sensor_comp.new_text_sensor(sens_cfg)


async def _register_diagnostic_binary_sensor(
    config,
    parent_id,
    suffix: str,
    title: str,
    *,
    device_class: Optional[str] = None,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
):
    kwargs = {"entity_category": entity_category}
    if device_class is not None:
        kwargs["device_class"] = device_class
    sch = binary_sensor_comp.binary_sensor_schema(**kwargs)
    child_id = ID(
        f"{parent_id.id}_{suffix}",
        is_declaration=True,
        type=binary_sensor_comp.BinarySensor,
    )
    body = {
        CONF_ID: child_id,
        CONF_NAME: _default_entity_name(config, title),
    }
    sens_cfg = sch(body)
    return await binary_sensor_comp.new_binary_sensor(sens_cfg)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    clock_pin = await cg.gpio_pin_expression(config[CONF_CLOCK_PIN])
    cg.add(var.set_clock_pin(clock_pin))

    data_pin = await cg.gpio_pin_expression(config[CONF_DATA_PIN])
    cg.add(var.set_data_pin(data_pin))

    if CONF_ADDRESS in config:
        cg.add(var.set_keypad_address(config[CONF_ADDRESS]))

    for keypad in config[CONF_KEYPADS]:
        cg.add(var.add_keypad(keypad[CONF_NAME], keypad[CONF_ADDRESS]))

    if CONF_ON_MESSAGE in config:
        await automation.build_automation(
            var.get_on_message_trigger(),
            [(cg.uint8, "type"), (cg.std_vector.template(cg.uint8), "data")],
            config[CONF_ON_MESSAGE],
        )

    pid = config[CONF_ID]

    hw = await _register_diagnostic_text_sensor(
        config, pid, "panel_hardware_ts", "Panel hardware", icon="mdi:chip"
    )
    cg.add(var.register_hardware_version(hw))
    fw = await _register_diagnostic_text_sensor(
        config, pid, "panel_firmware_ts", "Panel firmware", icon="mdi:application"
    )
    cg.add(var.register_firmware_version(fw))

    ready = await _register_diagnostic_binary_sensor(
        config, pid, "panel_ready_bs", "Panel ready", entity_category=ENTITY_CATEGORY_NONE
    )
    cg.add(var.register_panel_ready(ready))

    mains = await _register_diagnostic_binary_sensor(
        config, pid, "mains_power_bs", "Mains power", device_class=DEVICE_CLASS_PROBLEM
    )
    cg.add(var.register_mains_power(mains))

    batt = await _register_diagnostic_binary_sensor(
        config, pid, "battery_state_bs", "Battery state", device_class=DEVICE_CLASS_BATTERY
    )
    cg.add(var.register_battery_state(batt))

    bus = await _register_diagnostic_binary_sensor(
        config,
        pid,
        "panel_bus_connected_bs",
        "Alarm bus connected",
        device_class=DEVICE_CLASS_CONNECTIVITY,
    )
    cg.add(var.register_panel_bus_connected(bus))
