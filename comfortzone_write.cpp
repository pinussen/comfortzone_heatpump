#include "comfortzone_heatpump.h"
#include "comfortzone_frame.h"
#include <cmath>
#include <cstdio>
#include <cstring>

bool comfortzone_heatpump::begin_write(WriteKind kind, float value, uint16_t sensor) {
  // Reject concurrent requests; never replace a command already on the wire.
  if (write_busy()) return false;
  write_result_ = WriteResult::IDLE;
  if (!std::isfinite(value)) {
    std::snprintf(last_message, sizeof(last_message), "Value must be finite");
    return false;
  }
  if ((kind == WriteKind::FAN_SPEED || kind == WriteKind::HEATING_CURVE ||
       kind == WriteKind::EXTRA_HOT_WATER || kind == WriteKind::FIREPLACE) &&
      (value != std::floor(value) || value < 0 || value > 255)) {
    std::snprintf(last_message, sizeof(last_message), "Expected a non-negative integer setting"); return false;
  }
  if ((kind == WriteKind::EXTRA_HOT_WATER || kind == WriteKind::FIREPLACE) && value > 1) {
    std::snprintf(last_message, sizeof(last_message), "Expected 0 or 1"); return false;
  }
  write_kind_ = kind;
  write_value_ = value;
  std::snprintf(write_wait_detail_, sizeof(write_wait_detail_), "No recent quiet reply observed");
  staging_write_ = true;
  bool accepted = false;
  // Reuse the protocol-specific validation and packet crafting, but copy the
  // packets in push_settings() instead of entering its legacy blocking loop.
  switch (kind) {
    case WriteKind::ROOM_TEMPERATURE: accepted = set_room_temperature(value); break;
    case WriteKind::HOT_WATER_TEMPERATURE: accepted = set_hot_water_temperature(value); break;
    case WriteKind::SENSOR_OFFSET: accepted = set_sensor_offset(sensor, value); break;
    case WriteKind::FAN_SPEED: accepted = set_fan_speed(static_cast<uint8_t>(value)); break;
    case WriteKind::EXTRA_HOT_WATER: accepted = set_extra_hot_water(value != 0); break;
    case WriteKind::HEATING_CURVE: accepted = set_heating_curve(static_cast<uint8_t>(value)); break;
    case WriteKind::FIREPLACE: accepted = set_fireplace_mode(value != 0); break;
  }
  staging_write_ = false;
  return accepted;
}

void comfortzone_heatpump::poll_write() {
  const uint32_t now = static_cast<uint32_t>(millis());
  if (write_result_ == WriteResult::WAITING_FOR_BUS) {
    if (now - write_started_ >= 5000) {
      write_result_ = WriteResult::FAILED;
      std::snprintf(last_message, sizeof(last_message),
          "No safe bus gap within 5 seconds; nothing sent. reply=%d frame_age=%lu rx_age=%lu partial=%u uart=%d idle=%d",
          bus_reply_seen_, static_cast<unsigned long>(now-bus_frame_time_),
          static_cast<unsigned long>(now-last_rx_byte_time_), static_cast<unsigned>(cz_size),
          rs485->available(), rs485->idle_break(100));
    } else if (bus_reply_seen_ && now - bus_frame_time_ >= 40 &&
               now - bus_frame_time_ < 1000 &&
               (now - last_rx_byte_time_ >= 40 || rs485->idle_break(100))) {
      const int unread = rs485->available();
      if (cz_size != 0 || unread != 0) {
        int used = std::snprintf(write_wait_detail_, sizeof(write_wait_detail_),
                                "Quiet reply age=%lu ms, partial=%u, uart=%d; bytes:",
                                static_cast<unsigned long>(now - bus_frame_time_),
                                static_cast<unsigned>(cz_size), unread);
        for (unsigned i = 0; i < cz_size && i < 16 && used > 0 &&
             used < static_cast<int>(sizeof(write_wait_detail_)) - 4; ++i)
          used += std::snprintf(write_wait_detail_ + used, sizeof(write_wait_detail_) - used, " %02X", cz_buf[i]);
        return;
      }
      rs485->enable_sender_mode();
      const int written = rs485->write_bytes(write_command_, write_command_size_);
      rs485->flush();
      rs485->enable_receiver_mode();
      write_sent_ = static_cast<uint32_t>(millis());
      write_result_ = written == write_command_size_ ? WriteResult::WAITING_FOR_REPLY : WriteResult::FAILED;
      std::snprintf(last_message, sizeof(last_message), "%s",
                    written == write_command_size_ ? "Sent; waiting for acknowledgement" : "UART write failed");
    }
  } else if (write_result_ == WriteResult::WAITING_FOR_REPLY && now - write_sent_ >= 200) {
    write_result_ = WriteResult::FAILED;
    // A lost acknowledgement does not prove that the pump ignored the command.
    // Do not retry automatically or publish an unconfirmed value.
    std::snprintf(last_message, sizeof(last_message), "No matching acknowledgement within 200 ms; outcome unknown");
  }
}

void comfortzone_heatpump::observe_write_frame_(bool valid_crc) {
  bus_frame_time_ = static_cast<uint32_t>(millis());
  const auto *header = reinterpret_cast<const CZ_PACKET_HEADER *>(cz_buf);
  bus_reply_seen_ = valid_crc && std::memcmp(header->source, heatpump_addr, 4) == 0 &&
                    (header->cmd == 'r' || header->cmd == 'w');
  if (!valid_crc || write_result_ != WriteResult::WAITING_FOR_REPLY ||
      bus_frame_time_ - write_sent_ >= 200 || cz_size != write_reply_size_ ||
      std::memcmp(cz_buf, write_reply_, sizeof(CZ_PACKET_HEADER)) != 0) return;

  if (!write_header_only_ && std::memcmp(cz_buf, write_reply_, write_reply_size_) != 0) {
    write_result_ = WriteResult::FAILED;
    std::snprintf(last_message, sizeof(last_message), "Unexpected acknowledgement value 0x%02X", cz_buf[21]);
    return;
  }
  write_result_ = WriteResult::SUCCESS;
  std::snprintf(last_message, sizeof(last_message), "Pump acknowledged write");
  if (write_kind_ == WriteKind::FAN_SPEED) comfortzone_status.fan_speed = static_cast<byte>(write_value_);
#if HP_PROTOCOL != HP_PROTOCOL_1_7
  if (write_kind_ == WriteKind::EXTRA_HOT_WATER) comfortzone_status.extra_hot_water_setting = write_value_ != 0;
#endif
  // Protocol 170 has no verified live boost-state bit. An ACK is not a lasting
  // state: publishing true here would remain stale after the pump's timer ends.
  if (write_kind_ == WriteKind::HEATING_CURVE) comfortzone_status.heating_curve = static_cast<int16_t>(write_value_);
  if (write_kind_ == WriteKind::ROOM_TEMPERATURE)
    comfortzone_status.room_heating_setting = static_cast<uint16_t>(write_value_ * 10.0f + 0.5f);
  if (write_kind_ == WriteKind::HOT_WATER_TEMPERATURE)
    comfortzone_status.hot_water_setting = static_cast<uint16_t>(write_value_ * 10.0f + 0.5f);
}
