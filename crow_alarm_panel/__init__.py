from esphome import pins, automation
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.components.text_sensor as text_sensor_comp
from esphome.components import alarm_control_panel as acp
from esphome.core import ID
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_NAME,
    CONF_CLOCK_PIN,
    CONF_DATA_PIN,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

AUTO_LOAD = ["binary_sensor", "text_sensor", "switch", "button", "alarm_control_panel"]
MULTI_CONF = True

CONF_CROW_ALARM_PANEL_ID = "crow_alarm_panel_id"
CONF_KEYPADS = "keypads"
CONF_ON_MESSAGE = "on_message"

crow_alarm_panel_ns = cg.esphome_ns.namespace("crow_alarm_panel")

CrowAlarmPanel = crow_alarm_panel_ns.class_("CrowAlarmPanel", cg.Component)
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


async def _register_diagnostic_text_sensor(config, parent_id, suffix: str, title: str):
    sch = text_sensor_comp.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC)
    child_id = ID(
        f"{parent_id.id}_{suffix}",
        is_declaration=True,
        type=text_sensor_comp.TextSensor,
    )
    sens_cfg = sch(
        {
            CONF_ID: child_id,
            CONF_NAME: _default_entity_name(config, title),
        }
    )
    sens = await text_sensor_comp.new_text_sensor(sens_cfg)
    await cg.register_component(sens, sens_cfg)
    return sens


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
    hw = await _register_diagnostic_text_sensor(config, pid, "hardware_version_ts", "Panel hardware")
    cg.add(var.register_hardware_version(hw))
    fw = await _register_diagnostic_text_sensor(config, pid, "firmware_version_ts", "Panel firmware")
    cg.add(var.register_firmware_version(fw))
    sus = await _register_diagnostic_text_sensor(
        config, pid, "suspected_temperature_ts", "Panel info tail (suspect)"
    )
    cg.add(var.register_suspected_temperature(sus))
    pt = await _register_diagnostic_text_sensor(config, pid, "panel_time_ts", "Panel time")
    cg.add(var.register_panel_time(pt))
    pd = await _register_diagnostic_text_sensor(config, pid, "panel_date_ts", "Panel date")
    cg.add(var.register_panel_date(pd))
    py = await _register_diagnostic_text_sensor(config, pid, "panel_year_ts", "Panel year")
    cg.add(var.register_panel_year(py))
