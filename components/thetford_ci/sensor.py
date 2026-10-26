import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
)

from . import CONF_THETFORD_CI_ID, THETFORD_CI_CHILD_SCHEMA

CONF_SUPPLY_VOLTAGE = "supply_voltage"
CONF_FRIDGE_LEVEL = "fridge_level"
CONF_FREEZER_LEVEL = "freezer_level"
CONF_ERROR_CODE = "error_code"
CONF_MODE_RAW = "mode_raw"

CONFIG_SCHEMA = THETFORD_CI_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_SUPPLY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_FRIDGE_LEVEL): sensor.sensor_schema(
            accuracy_decimals=0, icon="mdi:fridge-bottom"
        ),
        cv.Optional(CONF_FREEZER_LEVEL): sensor.sensor_schema(
            accuracy_decimals=0, icon="mdi:snowflake"
        ),
        cv.Optional(CONF_ERROR_CODE): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:alert-circle-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_MODE_RAW): sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:numeric",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_THETFORD_CI_ID])
    for key, setter in (
        (CONF_SUPPLY_VOLTAGE, "set_supply_voltage_sensor"),
        (CONF_FRIDGE_LEVEL, "set_fridge_level_sensor"),
        (CONF_FREEZER_LEVEL, "set_freezer_level_sensor"),
        (CONF_ERROR_CODE, "set_error_code_sensor"),
        (CONF_MODE_RAW, "set_mode_raw_sensor"),
    ):
        if key in config:
            s = await sensor.new_sensor(config[key])
            cg.add(getattr(parent, setter)(s))
