import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.core import ID
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_OUTPUT,
    CONF_TYPE,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from .. import (
    CrowAlarmPanel,
    CrowAlarmPanelZoneBinarySensor,
    CONF_CROW_ALARM_PANEL_ID,
)

DEPENDENCIES = ["crow_alarm_panel"]

CONF_ZONE = "zone"
CONF_BYPASS = "bypass"
CONF_OUTPUT = "output"
CONF_INCLUDE_BYPASS_SENSOR = "include_bypass_sensor"

ZONE_SCHEMA = binary_sensor.binary_sensor_schema(CrowAlarmPanelZoneBinarySensor).extend(
    {
        cv.GenerateID(): cv.declare_id(CrowAlarmPanelZoneBinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
        cv.Optional(CONF_ZONE): cv.positive_int,
        cv.Optional(CONF_INCLUDE_BYPASS_SENSOR, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)

BYPASS_ONLY_SCHEMA = binary_sensor.binary_sensor_schema(
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(
    {
        cv.GenerateID(): cv.declare_id(binary_sensor.BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
        cv.Optional(CONF_ZONE): cv.positive_int,
    }
).extend(cv.COMPONENT_SCHEMA)

OUTPUT_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.GenerateID(): cv.declare_id(binary_sensor.BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
        cv.Required(CONF_OUTPUT): cv.positive_int,
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_ZONE: ZONE_SCHEMA,
        CONF_BYPASS: BYPASS_ONLY_SCHEMA,
        CONF_OUTPUT: OUTPUT_SCHEMA,
    }
)


def _bypass_sensor_name(zone_config):
    if CONF_NAME in zone_config:
        return f"{zone_config[CONF_NAME]} Bypassed"
    return f"Zone {zone_config[CONF_ZONE]} Bypassed"


def to_code(config):
    paren = yield cg.get_variable(config[CONF_CROW_ALARM_PANEL_ID])
    type = config[CONF_TYPE]
    var = cg.new_Pvariable(config[CONF_ID])

    yield binary_sensor.register_binary_sensor(var, config)

    if type == CONF_ZONE:
        cg.add(paren.register_zone(var, config[CONF_ZONE]))
        bypass_id = ID(
            f"{config[CONF_ID].id}_bypass",
            is_declaration=True,
            type=binary_sensor.BinarySensor,
        )
        bypass_schema = binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ).extend(
            {
                cv.GenerateID(): cv.declare_id(binary_sensor.BinarySensor),
            }
        )
        bypass_config = bypass_schema(
            {
                CONF_ID: bypass_id,
                CONF_NAME: _bypass_sensor_name(config),
            }
        )
        bypass_var = cg.new_Pvariable(bypass_id)
        yield binary_sensor.register_binary_sensor(bypass_var, bypass_config)
        cg.add(paren.register_zone_bypass(bypass_var, config[CONF_ZONE]))
    elif type == CONF_BYPASS:
        cg.add(paren.register_zone_bypass(var, config[CONF_ZONE]))
    elif type == CONF_OUTPUT:
        cg.add(paren.register_output_binary_sensor(var, config[CONF_OUTPUT]))
