#pragma once
#include "comfortzone_heatpump.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

uint32_t test_millis = 100;
using Kind = comfortzone_heatpump::WriteKind;
using Result = comfortzone_heatpump::WriteResult;

struct Bus : RS485Interface {
  std::deque<byte> input;
  std::vector<byte> sent;
  bool sending = false;
  int writes = 0;
  bool physical_idle_break = false;
  bool idle_break(uint32_t) override { return physical_idle_break; }
  void begin() override { sending = false; }
  int available() override { return input.size(); }
  int read_byte() override { auto b = input.front(); input.pop_front(); return b; }
  int write_bytes(const void *data, int size) override {
    assert(sending);
    auto p = static_cast<const byte *>(data);
    sent.assign(p, p + size);
    ++writes;
    return size;
  }
  void flush() override { assert(sending); }
  void enable_sender_mode() override { sending = true; }
  void enable_receiver_mode() override { sending = false; }
  void feed(const std::vector<byte> &data) { input.insert(input.end(), data.begin(), data.end()); }
};

void crc(std::vector<byte> &frame) {
  FastCRC8 c;
  frame[4] = c.maxim(frame.data(), 4);
  byte inv[4];
  for (int i = 0; i < 4; ++i) inv[i] = frame[i] ^ 255;
  frame[5] = c.maxim(inv, 4);
  frame.back() = c.maxim(frame.data(), frame.size() - 1);
}

// Actual register 05 2c 00 from the user's March captures.
std::vector<byte> pump_reply() {
  return {0x41,0x44,0x44,0x52,0x07,0x8a,0x65,0x6f,0xde,0x02,0x2a,0x72,
          1,2,3,4,0x0b,8,5,0x2c,0,0x62,0x47,0x6b,0,0x74,0xa8,9,0,
          0xe2,0xb9,0x12,0,0x7e,0x74,0x17,0,0xd4,0x5f,0x43,0,0xe2};
}

std::vector<byte> ack(const Bus &bus, int value = -1) {
  auto f = bus.sent;
  assert(f.size() >= 23);
  byte dest[4]; std::memcpy(dest, f.data(), 4);
  std::memcpy(f.data(), f.data() + 6, 4);
  std::memcpy(f.data() + 6, dest, 4);
  const byte echoed_value = f[f.size()-2];
  f.resize(23); f[10] = 23; f[11] = 'w'; f[21] = value < 0 ? echoed_value : value; crc(f);
  return f;
}

void ready(comfortzone_heatpump &hp, Bus &bus) {
  bus.feed(pump_reply());
  const auto frame = hp.process();
  assert(frame == comfortzone_heatpump::PFT_REPLY || frame == comfortzone_heatpump::PFT_UNKNOWN);
}
