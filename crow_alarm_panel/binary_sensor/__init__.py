import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.core import ID
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_TYPE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ENTITY_CATEGORY_NONE,
)
from .. import CrowAlarmPanel, CONF_CROW_ALARM_PANEL_ID

DEPENDENCIES = ["crow_alarm_panel"]

binary_sensor_ns = cg.esphome_ns.namespace("binary_sensor")
BinarySensor = binary_sensor_ns.class_("BinarySensor", cg.EntityBase)

CONF_ZONE = "zone"
CONF_BYPASS = "bypass"
CONF_INCLUDE_BYPASS_SENSOR = "include_bypass_sensor"
CONF_PANEL_READY = "panel_ready"
CONF_MAINS_POWER = "mains_power"
CONF_BATTERY_STATE = "battery_state"
CONF_BATTERY_STATE_EXPERIMENTAL = "battery_state_experimental"  # deprecated alias
CONF_PANEL_BUS_CONNECTED = "panel_bus_connected"

ZONE_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.GenerateID(): cv.declare_id(BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
        cv.Optional(CONF_ZONE): cv.positive_int,
        cv.Optional(CONF_INCLUDE_BYPASS_SENSOR, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)

BYPASS_ONLY_SCHEMA = binary_sensor.binary_sensor_schema(
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
).extend(
    {
        cv.GenerateID(): cv.declare_id(BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
        cv.Optional(CONF_ZONE): cv.positive_int,
    }
).extend(cv.COMPONENT_SCHEMA)

PANEL_READY_SCHEMA = binary_sensor.binary_sensor_schema(
    entity_category=ENTITY_CATEGORY_NONE,
).extend(
    {
        cv.GenerateID(): cv.declare_id(BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
    }
).extend(cv.COMPONENT_SCHEMA)

MAINS_POWER_SCHEMA = binary_sensor.binary_sensor_schema(
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    device_class=DEVICE_CLASS_PROBLEM,
).extend(
    {
        cv.GenerateID(): cv.declare_id(BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
    }
).extend(cv.COMPONENT_SCHEMA)

BATTERY_STATE_SCHEMA = binary_sensor.binary_sensor_schema(
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    device_class=DEVICE_CLASS_BATTERY,
).extend(
    {
        cv.GenerateID(): cv.declare_id(BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
    }
).extend(cv.COMPONENT_SCHEMA)

PANEL_BUS_CONNECTED_SCHEMA = binary_sensor.binary_sensor_schema(
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    device_class=DEVICE_CLASS_CONNECTIVITY,
).extend(
    {
        cv.GenerateID(): cv.declare_id(BinarySensor),
        cv.GenerateID(CONF_CROW_ALARM_PANEL_ID): cv.use_id(CrowAlarmPanel),
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_ZONE: ZONE_SCHEMA,
        CONF_BYPASS: BYPASS_ONLY_SCHEMA,
        CONF_PANEL_READY: PANEL_READY_SCHEMA,
        CONF_MAINS_POWER: MAINS_POWER_SCHEMA,
        CONF_BATTERY_STATE: BATTERY_STATE_SCHEMA,
        CONF_BATTERY_STATE_EXPERIMENTAL: BATTERY_STATE_SCHEMA,
        CONF_PANEL_BUS_CONNECTED: PANEL_BUS_CONNECTED_SCHEMA,
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
        if config[CONF_INCLUDE_BYPASS_SENSOR]:
            bypass_id = ID(
                f"{config[CONF_ID].id}_bypass",
                is_declaration=True,
                type=BinarySensor,
            )
            bypass_schema = binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ).extend(
                {
                    cv.GenerateID(): cv.declare_id(BinarySensor),
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
    elif type == CONF_PANEL_READY:
        cg.add(paren.register_panel_ready(var))
    elif type == CONF_MAINS_POWER:
        cg.add(paren.register_mains_power(var))
    elif type in (CONF_BATTERY_STATE, CONF_BATTERY_STATE_EXPERIMENTAL):
        cg.add(paren.register_battery_state(var))
    elif type == CONF_PANEL_BUS_CONNECTED:
        cg.add(paren.register_panel_bus_connected(var))
