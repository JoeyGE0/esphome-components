import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID, CONF_TYPE
from .. import CrowAlarmPanel, CONF_CROW_ALARM_PANEL_ID

DEPENDENCIES = ["crow_alarm_panel"]

text_sensor_ns = cg.esphome_ns.namespace("text_sensor")
TextSensor = text_sensor_ns.class_("TextSensor", cg.EntityBase)

CONF_ARMED_STATE = "armed_state"
CONF_PANEL_TIME = "panel_time"
CONF_PANEL_DATE = "panel_date"
CONF_PANEL_HARDWARE = "panel_hardware"
CONF_PANEL_FIRMWARE = "panel_firmware"

TYPES = [
    CONF_ARMED_STATE,
    CONF_PANEL_TIME,
    CONF_PANEL_DATE,
    CONF_PANEL_HARDWARE,
    CONF_PANEL_FIRMWARE,
]

_BASE_SCHEMA = {
    cv.GenerateID(): cv.declare_id(TextSensor),
    cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
}

CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_ARMED_STATE: text_sensor.text_sensor_schema().extend(_BASE_SCHEMA).extend(cv.COMPONENT_SCHEMA),
        CONF_PANEL_TIME: text_sensor.text_sensor_schema().extend(_BASE_SCHEMA).extend(cv.COMPONENT_SCHEMA),
        CONF_PANEL_DATE: text_sensor.text_sensor_schema().extend(_BASE_SCHEMA).extend(cv.COMPONENT_SCHEMA),
        CONF_PANEL_HARDWARE: text_sensor.text_sensor_schema().extend(_BASE_SCHEMA).extend(cv.COMPONENT_SCHEMA),
        CONF_PANEL_FIRMWARE: text_sensor.text_sensor_schema().extend(_BASE_SCHEMA).extend(cv.COMPONENT_SCHEMA),
    },
    lower=True,
)


def to_code(config):
    paren = yield cg.get_variable(config[CONF_CROW_ALARM_PANEL_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    typ = config[CONF_TYPE]

    yield text_sensor.register_text_sensor(var, config)

    if typ == CONF_ARMED_STATE:
        cg.add(paren.register_armed_state(var))
    elif typ == CONF_PANEL_TIME:
        cg.add(paren.register_panel_time(var))
    elif typ == CONF_PANEL_DATE:
        cg.add(paren.register_panel_date(var))
    elif typ == CONF_PANEL_HARDWARE:
        cg.add(paren.register_hardware_version(var))
    elif typ == CONF_PANEL_FIRMWARE:
        cg.add(paren.register_firmware_version(var))
