import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_THETFORD_CI_ID, THETFORD_CI_CHILD_SCHEMA, thetford_ci_ns

ThetfordSwitch = thetford_ci_ns.class_("ThetfordSwitch", switch.Switch)
SwitchType = thetford_ci_ns.enum("SwitchType", is_class=True)

CONF_POWER = "power"
CONF_NIGHT_MODE = "night_mode"

CONFIG_SCHEMA = THETFORD_CI_CHILD_SCHEMA.extend(
    {
        cv.Optional(CONF_POWER): switch.switch_schema(ThetfordSwitch, icon="mdi:power"),
        cv.Optional(CONF_NIGHT_MODE): switch.switch_schema(
            ThetfordSwitch, icon="mdi:weather-night"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_THETFORD_CI_ID])
    for key, typ, setter in (
        (CONF_POWER, SwitchType.POWER, "set_power_switch"),
        (CONF_NIGHT_MODE, SwitchType.NIGHT, "set_night_switch"),
    ):
        if key in config:
            s = await switch.new_switch(config[key])
            await cg.register_parented(s, parent)
            cg.add(s.set_type(typ))
            cg.add(getattr(parent, setter)(s))
