import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_MAX_VALUE, CONF_MIN_VALUE, ENTITY_CATEGORY_CONFIG

from . import CONF_THETFORD_CI_ID, THETFORD_CI_CHILD_SCHEMA, thetford_ci_ns

ThetfordLevel = thetford_ci_ns.class_("ThetfordLevel", number.Number)
LevelType = thetford_ci_ns.enum("LevelType", is_class=True)

CONF_FRIDGE_LEVEL = "fridge_level"
CONF_FREEZER_LEVEL = "freezer_level"


def level_schema(icon, default_max):
    return number.number_schema(
        ThetfordLevel, icon=icon, entity_category=ENTITY_CATEGORY_CONFIG
    ).extend(
        {
            # Rohwerte des Nibbles (0..15). Bereich per Bedienfeld-Abgleich einstellen.
            cv.Optional(CONF_MIN_VALUE, default=0): cv.int_range(min=0, max=15),
            cv.Optional(CONF_MAX_VALUE, default=default_max): cv.int_range(min=0, max=15),
        }
    )


CONFIG_SCHEMA = THETFORD_CI_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_FRIDGE_LEVEL): level_schema("mdi:fridge-bottom", 5),
        cv.Optional(CONF_FREEZER_LEVEL): level_schema("mdi:snowflake", 4),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_THETFORD_CI_ID])
    for key, typ, setter in (
        (CONF_FRIDGE_LEVEL, LevelType.FRIDGE, "set_fridge_level_number"),
        (CONF_FREEZER_LEVEL, LevelType.FREEZER, "set_freezer_level_number"),
    ):
        if key in config:
            conf = config[key]
            n = await number.new_number(
                conf, min_value=conf[CONF_MIN_VALUE], max_value=conf[CONF_MAX_VALUE], step=1
            )
            await cg.register_parented(n, parent)
            cg.add(n.set_type(typ))
            cg.add(getattr(parent, setter)(n))
