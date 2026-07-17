#include "samsung_ac_ir.h"
#include <cmath>
#include "esphome/core/log.h"

namespace esphome {
namespace samsung_ac_ir {

static const char *const TAG = "samsung_ac_ir";

// Section 1 is constant for every frame. Power frames insert PWR_MARKER as section 2.
static const std::array<uint8_t, 7> SECTION1 = {0x02, 0x92, 0x0F, 0x00, 0x00, 0x00, 0xF0};
static const std::array<uint8_t, 7> PWR_MARKER = {0x01, 0xD2, 0x0F, 0x00, 0x00, 0x00, 0x00};

// byte6 high nibble encodes the power/enable state: 0xF0 = on, 0xC0 = off.
static const uint8_t BYTE6_ON = 0xF0;
static const uint8_t BYTE6_OFF = 0xC0;

static uint8_t count_bits(uint8_t v) {
  uint8_t c = 0;
  while (v) {
    c += v & 1U;
    v >>= 1U;
  }
  return c;
}

// Samsung per-section checksum (validated against 15 real captures).
static uint8_t compute_checksum(const std::array<uint8_t, 7> &s) {
  uint8_t sum = count_bits(s[0]) + count_bits(s[1] & 0x0F) + count_bits((s[2] >> 4) & 0x0F);
  for (int i = 3; i < 7; i++)
    sum += count_bits(s[i]);
  return sum ^ 0xFF;
}

static void set_checksum(std::array<uint8_t, 7> &s) {
  uint8_t ck = compute_checksum(s);
  s[1] = (s[1] & 0x0F) | ((ck & 0x0F) << 4);  // byte1 high nibble = checksum low
  s[2] = (s[2] & 0xF0) | ((ck >> 4) & 0x0F);  // byte2 low nibble  = checksum high
}

// Returns true if the section's stored checksum nibbles match the recomputed value.
static bool checksum_valid(const std::array<uint8_t, 7> &s) {
  uint8_t ck = compute_checksum(s);
  uint8_t stored = ((s[1] >> 4) & 0x0F) | ((s[2] & 0x0F) << 4);
  return stored == ck;
}

void SamsungACIR::setup() {
  // Advertise the 4th "turbo" speed (fan code 7) as a custom fan mode. Stored on the
  // Climate entity and picked up automatically by get_traits().
  this->set_supported_custom_fan_modes({FAN_TURBO});
  climate_ir::ClimateIR::setup();
}

std::array<uint8_t, 7> SamsungACIR::build_state_(uint8_t byte6) {
  float t = this->target_temperature;
  if (t < 16)
    t = 16;
  if (t > 30)
    t = 30;
  uint8_t temp = static_cast<uint8_t>(lroundf(t)) - 16;

  uint8_t mode_code = 0x1;  // cool
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL: mode_code = 0x1; break;
    case climate::CLIMATE_MODE_DRY: mode_code = 0x2; break;
    case climate::CLIMATE_MODE_FAN_ONLY: mode_code = 0x3; break;
    case climate::CLIMATE_MODE_HEAT: mode_code = 0x4; break;
    case climate::CLIMATE_MODE_HEAT_COOL: mode_code = 0x8; break;  // "auto"
    default: mode_code = 0x1; break;
  }

  uint8_t fan_code = 0x0;  // auto
  if (this->has_custom_fan_mode() && this->get_custom_fan_mode() == FAN_TURBO) {
    fan_code = 7;  // turbo / L4
  } else if (this->fan_mode.has_value()) {
    switch (this->fan_mode.value()) {
      case climate::CLIMATE_FAN_LOW: fan_code = 2; break;
      case climate::CLIMATE_FAN_MEDIUM: fan_code = 4; break;
      case climate::CLIMATE_FAN_HIGH: fan_code = 5; break;
      default: fan_code = 0; break;  // auto
    }
  }

  std::array<uint8_t, 7> s = {0x01,
                              0x02,
                              0xB0,
                              0x71,
                              static_cast<uint8_t>(temp << 4),
                              static_cast<uint8_t>((mode_code << 4) | (fan_code << 1) | 0x01),
                              byte6};
  set_checksum(s);
  return s;
}

void SamsungACIR::send_sections_(const std::vector<std::array<uint8_t, 7>> &sections) {
  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();
  data->set_carrier_frequency(CARRIER_HZ);
  for (size_t i = 0; i < sections.size(); i++) {
    data->mark(HDR_MARK);
    data->space(HDR_SPACE);
    for (uint8_t b : sections[i]) {
      for (uint8_t bit = 0; bit < 8; bit++) {  // LSB first
        data->mark(BIT_MARK);
        data->space((b >> bit) & 0x01 ? ONE_SPACE : ZERO_SPACE);
      }
    }
    data->mark(BIT_MARK);
    // SECTION_GAP between sections; the longer STOP_SPACE after the final section (matches remote).
    data->space(i + 1 < sections.size() ? SECTION_GAP : STOP_SPACE);
  }
  transmit.perform();
}

void SamsungACIR::transmit_state() {
  std::vector<std::array<uint8_t, 7>> frame;
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    // 21-byte extended power-OFF frame. Section 3 encodes the last state with byte6=0xC0.
    // Samsung A/C needs the extended frame to power OFF (matches our captured OFF frame and
    // the hardware-tested ESPHome `samsung` component, PR #8004).
    frame = {SECTION1, PWR_MARKER, build_state_(BYTE6_OFF)};
    ESP_LOGD(TAG, "Samsung AC: POWER OFF (extended frame)");
  } else {
    // 14-byte command. byte6=0xF0 asserts the power-on bits, so this both applies the state
    // and powers the unit on from OFF -- no explicit extended power-on frame is required
    // (verified against PR #8004, which powers on the same way on real Samsung units).
    frame = {SECTION1, build_state_(BYTE6_ON)};
    ESP_LOGD(TAG, "Samsung AC: command mode=%d temp=%.0f", (int) this->mode, this->target_temperature);
  }
  this->send_sections_(frame);
}

bool SamsungACIR::decode_section_(remote_base::RemoteReceiveData &data, std::array<uint8_t, 7> &section) {
  if (!data.expect_mark(HDR_MARK) || !data.expect_space(HDR_SPACE))
    return false;
  section = {0, 0, 0, 0, 0, 0, 0};
  for (int byte = 0; byte < 7; byte++) {
    for (int bit = 0; bit < 8; bit++) {  // LSB first
      if (!data.expect_mark(BIT_MARK))
        return false;
      if (data.expect_space(ONE_SPACE)) {
        section[byte] |= (1U << bit);
      } else if (data.expect_space(ZERO_SPACE)) {
        // bit stays 0
      } else {
        return false;
      }
    }
  }
  // Trailing bit mark, then the inter-section gap (or the longer STOP_SPACE after the final
  // section). Best-effort: a missing/oversized trailing gap just ends the frame.
  if (!data.expect_mark(BIT_MARK))
    return false;
  if (!data.expect_space(SECTION_GAP))
    data.expect_space(STOP_SPACE);
  return true;
}

void SamsungACIR::apply_state_section_(const std::array<uint8_t, 7> &s) {
  // Power: byte6 high nibble. 0xC0 => off.
  if ((s[6] & 0xF0) == BYTE6_OFF) {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->publish_state();
    return;
  }

  // Temperature: byte4 high nibble + 16.
  this->target_temperature = ((s[4] >> 4) & 0x0F) + 16;

  // Mode: byte5 high nibble.
  switch ((s[5] >> 4) & 0x0F) {
    case 0x1: this->mode = climate::CLIMATE_MODE_COOL; break;
    case 0x2: this->mode = climate::CLIMATE_MODE_DRY; break;
    case 0x3: this->mode = climate::CLIMATE_MODE_FAN_ONLY; break;
    case 0x4: this->mode = climate::CLIMATE_MODE_HEAT; break;
    case 0x8: this->mode = climate::CLIMATE_MODE_HEAT_COOL; break;
    default: this->mode = climate::CLIMATE_MODE_COOL; break;
  }

  // Fan: byte5 bits 1-3.
  uint8_t fan_code = (s[5] >> 1) & 0x07;
  if (fan_code == 7) {
    this->set_custom_fan_mode_(FAN_TURBO);
  } else {
    this->clear_custom_fan_mode_();
    switch (fan_code) {
      case 2: this->fan_mode = climate::CLIMATE_FAN_LOW; break;
      case 4: this->fan_mode = climate::CLIMATE_FAN_MEDIUM; break;
      case 5: this->fan_mode = climate::CLIMATE_FAN_HIGH; break;
      default: this->fan_mode = climate::CLIMATE_FAN_AUTO; break;
    }
  }
  this->publish_state();
}

bool SamsungACIR::on_receive(remote_base::RemoteReceiveData data) {
  std::vector<std::array<uint8_t, 7>> sections;
  std::array<uint8_t, 7> sec;
  while (sections.size() < 3 && decode_section_(data, sec)) {
    sections.push_back(sec);
  }
  // A valid frame is 2 sections (14-byte command) or 3 sections (21-byte power frame).
  if (sections.size() < 2)
    return false;

  // The state lives in the last section (section 2 for 14-byte, section 3 for 21-byte).
  const auto &state = sections.back();
  if (!checksum_valid(state) || state[0] != 0x01 || state[3] != 0x71) {
    ESP_LOGV(TAG, "Samsung AC: frame decoded but checksum/const bytes invalid");
    return false;
  }

  this->apply_state_section_(state);
  ESP_LOGD(TAG, "Samsung AC: received state (%d sections)", (int) sections.size());
  return true;
}

}  // namespace samsung_ac_ir
}  // namespace esphome
