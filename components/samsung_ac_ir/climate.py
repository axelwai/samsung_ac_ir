import esphome.codegen as cg
from esphome.components import climate_ir

AUTO_LOAD = ["climate_ir"]
CODEOWNERS = ["@axelwai"]

samsung_ac_ir_ns = cg.esphome_ns.namespace("samsung_ac_ir")
SamsungACIR = samsung_ac_ir_ns.class_("SamsungACIR", climate_ir.ClimateIR)

# with_receiver schema adds the optional `receiver_id:` so the entity can read state
# back from a remote_receiver (SamsungACIR::on_receive decodes the frames).
CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(SamsungACIR)


async def to_code(config):
    await climate_ir.new_climate_ir(config)
