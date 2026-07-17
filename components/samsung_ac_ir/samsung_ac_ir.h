#pragma once

#include <array>
#include <vector>

#include "esphome/components/climate_ir/climate_ir.h"

namespace esphome {
namespace samsung_ac_ir {

// Timings (microseconds), measured from AR10TYHZCWKNST pronto captures (all 15 frames pooled, 38 kHz).
static const uint32_t CARRIER_HZ = 38000;
static const uint32_t HDR_MARK = 2990;
static const uint32_t HDR_SPACE = 9100;
static const uint32_t BIT_MARK = 470;
static const uint32_t ONE_SPACE = 1570;
static const uint32_t ZERO_SPACE = 540;
static const uint32_t SECTION_GAP = 3050;   // gap between sections
static const uint32_t STOP_SPACE = 10120;   // long idle gap after the final section

// Custom fan speed exposed to Home Assistant for fan code 7 (the remote's "turbo"/L4 speed).
// There is no standard climate::CLIMATE_FAN_* value for turbo, so it is advertised as a
// custom fan mode string.
static const char *const FAN_TURBO = "Turbo";

class SamsungACIR : public climate_ir::ClimateIR {
 public:
  SamsungACIR()
      : climate_ir::ClimateIR(16, 30, 1.0f, /*supports_dry=*/true, /*supports_fan_only=*/true,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                               climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH}) {}

  void setup() override;

 protected:
  void transmit_state() override;
  // Decode a received Samsung AC frame back into this entity's state (optional receiver).
  bool on_receive(remote_base::RemoteReceiveData data) override;

  std::array<uint8_t, 7> build_state_(uint8_t byte6);
  void send_sections_(const std::vector<std::array<uint8_t, 7>> &sections);
  bool decode_section_(remote_base::RemoteReceiveData &data, std::array<uint8_t, 7> &section);
  void apply_state_section_(const std::array<uint8_t, 7> &s);
};

}  // namespace samsung_ac_ir
}  // namespace esphome
