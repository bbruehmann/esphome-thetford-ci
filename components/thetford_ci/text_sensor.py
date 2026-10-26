import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_THETFORD_CI_ID, THETFORD_CI_CHILD_SCHEMA

CONF_MODE = "mode"
CONF_STATUS = "status"
CONF_RAW_FRAME = "raw_frame"

CONFIG_SCHEMA = THETFORD_CI_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_MODE): text_sensor.text_sensor_schema(icon="mdi:fridge-outline"),
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
            icon="mdi:information-outline"
        ),
        cv.Optional(CONF_RAW_FRAME): text_sensor.text_sensor_schema(
            icon="mdi:code-brackets", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_THETFORD_CI_ID])
    for key, setter in (
        (CONF_MODE, "set_mode_text_sensor"),
        (CONF_STATUS, "set_status_text_sensor"),
        (CONF_RAW_FRAME, "set_raw_frame_text_sensor"),
    ):
        if key in config:
            s = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(parent, setter)(s))
