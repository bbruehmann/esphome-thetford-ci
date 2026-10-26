import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_THETFORD_CI_ID, THETFORD_CI_CHILD_SCHEMA

CONF_ONLINE = "online"
CONF_RUNNING = "running"
CONF_NIGHT_MODE = "night_mode"
CONF_FAULT = "fault"

CONFIG_SCHEMA = THETFORD_CI_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_ONLINE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RUNNING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING, icon="mdi:fridge"
        ),
        cv.Optional(CONF_NIGHT_MODE): binary_sensor.binary_sensor_schema(
            icon="mdi:weather-night"
        ),
        cv.Optional(CONF_FAULT): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_THETFORD_CI_ID])
    for key, setter in (
        (CONF_ONLINE, "set_online_binary_sensor"),
        (CONF_RUNNING, "set_running_binary_sensor"),
        (CONF_NIGHT_MODE, "set_night_binary_sensor"),
        (CONF_FAULT, "set_fault_binary_sensor"),
    ):
        if key in config:
            s = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(parent, setter)(s))
