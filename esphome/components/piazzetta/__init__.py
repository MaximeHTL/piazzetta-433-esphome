# ESPHome external component for Piazzetta pellet stoves driven by the 433 MHz F0203-1480V3 remote.
# Protocol: see docs/protocol.md. Code comments are in French (original project language).
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, number, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_DECIBEL_MILLIWATT,
)

CODEOWNERS = ["@MaximeHTL"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "number", "button"]

piazzetta_ns = cg.esphome_ns.namespace("piazzetta")
PiazzettaComponent = piazzetta_ns.class_("PiazzettaComponent", cg.Component)
PiazzettaNumber = piazzetta_ns.class_("PiazzettaNumber", number.Number)
PiazzettaButton = piazzetta_ns.class_("PiazzettaButton", button.Button)
Reglage = piazzetta_ns.enum("Reglage", is_class=True)
Action = piazzetta_ns.enum("Action", is_class=True)

CONF_PIN_SCK = "pin_sck"
CONF_PIN_MISO = "pin_miso"
CONF_PIN_MOSI = "pin_mosi"
CONF_PIN_CS = "pin_cs"
CONF_PIN_RST = "pin_rst"
CONF_PIN_DIO0 = "pin_dio0"
CONF_PIN_DIO1 = "pin_dio1"
CONF_PIN_DIO2 = "pin_dio2"
CONF_FREQUENCE = "frequence"
CONF_REMOTE_ID = "remote_id"
CONF_PUISSANCE_EMISSION = "puissance_emission"
CONF_SEUIL_RSSI = "seuil_rssi"
CONF_INTERVALLE = "intervalle_interrogation"
CONF_TEMPERATURE = "temperature"
CONF_RSSI = "rssi"
CONF_PHASE = "phase"
CONF_HEURE = "heure"
CONF_ETAT = "etat"
CONF_LIGNE1 = "ecran_ligne1"
CONF_LIGNE2 = "ecran_ligne2"
CONF_BAS = "ecran_bas"
CONF_JOIGNABLE = "joignable"
CONF_PUISSANCE = "puissance"
CONF_VENTILATEUR = "ventilateur"
CONF_INTERROGER = "interroger"
CONF_MARCHE_ARRET = "marche_arret"

broche = cv.int_range(min=0, max=39)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PiazzettaComponent),
        # Brochage du banc de test (ESP32 DevKit + Ra-02)
        cv.Optional(CONF_PIN_SCK, default=18): broche,
        cv.Optional(CONF_PIN_MISO, default=19): broche,
        cv.Optional(CONF_PIN_MOSI, default=23): broche,
        cv.Optional(CONF_PIN_CS, default=5): broche,
        cv.Optional(CONF_PIN_RST, default=14): broche,
        cv.Optional(CONF_PIN_DIO0, default=26): broche,
        cv.Optional(CONF_PIN_DIO1, default=33): broche,
        cv.Optional(CONF_PIN_DIO2, default=32): broche,
        cv.Optional(CONF_FREQUENCE, default=433.92): cv.float_range(min=410, max=525),
        # First byte of every remote frame (0xC5 on the reference installation). It may be specific to
        # each remote/stove pairing: capture yours with the test bench (see docs/capture-guide.md).
        cv.Optional(CONF_REMOTE_ID, default=0xC5): cv.hex_uint8_t,
        cv.Optional(CONF_PUISSANCE_EMISSION, default=10): cv.int_range(min=2, max=17),
        cv.Optional(CONF_SEUIL_RSSI, default=-78): cv.int_range(min=-120, max=-30),
        cv.Optional(CONF_INTERVALLE, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_RSSI): sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PHASE): text_sensor.text_sensor_schema(icon="mdi:fireplace"),
        cv.Optional(CONF_ETAT): text_sensor.text_sensor_schema(icon="mdi:fire-circle"),
        cv.Optional(CONF_LIGNE1): text_sensor.text_sensor_schema(icon="mdi:remote"),
        cv.Optional(CONF_LIGNE2): text_sensor.text_sensor_schema(icon="mdi:remote"),
        cv.Optional(CONF_BAS): text_sensor.text_sensor_schema(icon="mdi:remote"),
        cv.Optional(CONF_HEURE): text_sensor.text_sensor_schema(
            icon="mdi:clock-outline", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_JOIGNABLE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PUISSANCE): number.number_schema(PiazzettaNumber, icon="mdi:fire"),
        cv.Optional(CONF_VENTILATEUR): number.number_schema(PiazzettaNumber, icon="mdi:fan"),
        cv.Optional(CONF_INTERROGER): button.button_schema(PiazzettaButton, icon="mdi:refresh"),
        cv.Optional(CONF_MARCHE_ARRET): button.button_schema(PiazzettaButton, icon="mdi:power"),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_library("SPI", None)
    cg.add_library("jgromes/RadioLib", "7.8.1")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(
        var.set_broches(
            config[CONF_PIN_SCK],
            config[CONF_PIN_MISO],
            config[CONF_PIN_MOSI],
            config[CONF_PIN_CS],
            config[CONF_PIN_RST],
            config[CONF_PIN_DIO0],
            config[CONF_PIN_DIO1],
            config[CONF_PIN_DIO2],
        )
    )
    cg.add(var.set_frequence(config[CONF_FREQUENCE]))
    cg.add(var.set_remote_id(config[CONF_REMOTE_ID]))
    cg.add(var.set_puissance_emission(config[CONF_PUISSANCE_EMISSION]))
    cg.add(var.set_seuil_rssi(config[CONF_SEUIL_RSSI]))
    cg.add(var.set_intervalle(config[CONF_INTERVALLE]))

    if CONF_TEMPERATURE in config:
        cg.add(var.set_temperature(await sensor.new_sensor(config[CONF_TEMPERATURE])))
    if CONF_RSSI in config:
        cg.add(var.set_rssi(await sensor.new_sensor(config[CONF_RSSI])))
    if CONF_PHASE in config:
        cg.add(var.set_phase(await text_sensor.new_text_sensor(config[CONF_PHASE])))
    for cle, setter in (
        (CONF_ETAT, "set_etat"),
        (CONF_LIGNE1, "set_ligne1"),
        (CONF_LIGNE2, "set_ligne2"),
        (CONF_BAS, "set_bas"),
    ):
        if cle in config:
            cg.add(getattr(var, setter)(await text_sensor.new_text_sensor(config[cle])))
    if CONF_HEURE in config:
        cg.add(var.set_heure(await text_sensor.new_text_sensor(config[CONF_HEURE])))
    if CONF_JOIGNABLE in config:
        cg.add(var.set_joignable(await binary_sensor.new_binary_sensor(config[CONF_JOIGNABLE])))

    for cle, reglage, setter in (
        (CONF_PUISSANCE, Reglage.PUISSANCE, "set_puissance"),
        (CONF_VENTILATEUR, Reglage.VENTILATEUR, "set_ventilateur"),
    ):
        if cle in config:
            n = await number.new_number(config[cle], min_value=1, max_value=4, step=1)
            cg.add(n.set_parent(var))
            cg.add(n.set_reglage(reglage))
            cg.add(getattr(var, setter)(n))

    for cle, action in (
        (CONF_INTERROGER, Action.INTERROGER),
        (CONF_MARCHE_ARRET, Action.MARCHE_ARRET),
    ):
        if cle in config:
            b = await button.new_button(config[cle])
            cg.add(b.set_parent(var))
            cg.add(b.set_action(action))
