"""Thetford T2000 / N4000 CI-Bus (LIN) – ESPHome external component.

Der ESP32 arbeitet als alleiniger LIN-Master auf einem eigenen Bus-Strang
zum Kühlschrank. Protokoll nach dem Reverse-Engineering von horchi
(linux-homectld) und skrebber/thetford (GPL-2.0).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["uart"]
MULTI_CONF = False

CONF_THETFORD_CI_ID = "thetford_ci_id"
CONF_ALLOW_WRITE = "allow_write"
CONF_CHECKSUM = "checksum"
CONF_STATUS_FRAME_ID = "status_frame_id"
CONF_CONTROL_FRAME_ID = "control_frame_id"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_OFFLINE_AFTER = "offline_after"
CONF_OFF_MODE_VALUE = "off_mode_value"
CONF_ON_MODE_VALUE = "on_mode_value"
CONF_WRITE_REPEAT = "write_repeat"

thetford_ci_ns = cg.esphome_ns.namespace("thetford_ci")
ThetfordCI = thetford_ci_ns.class_(
    "ThetfordCI", cg.PollingComponent, uart.UARTDevice
)

ChecksumMode = thetford_ci_ns.enum("ChecksumMode")
CHECKSUM_MODES = {
    "AUTO": ChecksumMode.CHECKSUM_AUTO,
    "CLASSIC": ChecksumMode.CHECKSUM_CLASSIC,
    "ENHANCED": ChecksumMode.CHECKSUM_ENHANCED,
}

frame_id = cv.All(cv.hex_int, cv.int_range(min=0x00, max=0x3F))
byte_value = cv.All(cv.hex_int, cv.int_range(min=0x00, max=0xFF))

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ThetfordCI),
            # Sicherheitsriegel: ohne allow_write: true wird NIE ein Steuerframe gesendet.
            cv.Optional(CONF_ALLOW_WRITE, default=False): cv.boolean,
            cv.Optional(CONF_CHECKSUM, default="AUTO"): cv.enum(
                CHECKSUM_MODES, upper=True
            ),
            cv.Optional(CONF_STATUS_FRAME_ID, default=0x0C): frame_id,
            cv.Optional(CONF_CONTROL_FRAME_ID, default=0x0B): frame_id,
            cv.Optional(
                CONF_RESPONSE_TIMEOUT, default="30ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_OFFLINE_AFTER, default=3): cv.int_range(min=1, max=100),
            cv.Optional(CONF_ON_MODE_VALUE, default=0x01): byte_value,
            cv.Optional(CONF_OFF_MODE_VALUE, default=0x08): byte_value,
            cv.Optional(CONF_WRITE_REPEAT, default=3): cv.int_range(min=1, max=10),
        }
    )
    .extend(cv.polling_component_schema("5s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "thetford_ci",
    baud_rate=19200,
    require_tx=True,
    require_rx=True,
    parity="NONE",
    stop_bits=1,
)

THETFORD_CI_CHILD_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_THETFORD_CI_ID): cv.use_id(ThetfordCI)}
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_allow_write(config[CONF_ALLOW_WRITE]))
    cg.add(var.set_checksum_mode(config[CONF_CHECKSUM]))
    cg.add(var.set_status_frame_id(config[CONF_STATUS_FRAME_ID]))
    cg.add(var.set_control_frame_id(config[CONF_CONTROL_FRAME_ID]))
    cg.add(var.set_response_timeout(config[CONF_RESPONSE_TIMEOUT]))
    cg.add(var.set_offline_after(config[CONF_OFFLINE_AFTER]))
    cg.add(var.set_on_mode_value(config[CONF_ON_MODE_VALUE]))
    cg.add(var.set_off_mode_value(config[CONF_OFF_MODE_VALUE]))
    cg.add(var.set_write_repeat(config[CONF_WRITE_REPEAT]))
