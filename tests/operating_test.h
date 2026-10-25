#pragma once
#include <memory>
#include <cmath>

__attribute__((noinline)) inline void test_operating() {
  auto raw = [](const char *hex) {
    std::vector<byte> bytes;
    for (size_t i=0; hex[i]; i+=2) {
      auto digit = [](char c) { return c <= '9' ? c-'0' : c-'a'+10; };
      bytes.push_back((digit(hex[i])<<4) | digit(hex[i+1]));
    }
    return bytes;
  };
  auto same = [](float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
  Bus bus;
  std::unique_ptr<comfortzone_heatpump> hp;
  for (const auto &sample : status_cases) {
    if (sample.reset) hp.reset(new comfortzone_heatpump(&bus));
    test_millis += 350;
    bus.feed(raw(sample.hex)); hp->process();
    auto r = hp->operating_170();
    assert(r.mode == sample.mode && r.activity == sample.activity);
    assert(same(r.heating_input, sample.heating) && same(r.water_input, sample.water));
    if (r.mode == CZMD_ROOM_HEATING) assert(r.heating_cop > 0 && std::isnan(r.water_cop));
    if (r.mode == CZMD_HOT_WATER) assert(r.water_cop > 0 && std::isnan(r.heating_cop));
    if (sample.reset) {
      // Partial or corrupted power replies must not establish production.
      auto partial = raw(status_cases[1].hex);
      partial.resize(partial.size()-1); partial[10] = partial.size(); crc(partial);
      // Force an actually incomplete measured range (ending before input power).
      partial.resize(40); partial[10] = 40; crc(partial);
      bus.feed(partial); hp->process();
      auto corrupt = raw(status_cases[1].hex); corrupt.back() ^= 1;
      bus.feed(corrupt); hp->process();
      assert(std::isnan(hp->operating_170().heating_input));
      auto other = raw(status_cases[1].hex); other[6] ^= 1; crc(other);
      bus.feed(other); hp->process();
      other = raw(status_cases[1].hex); other[17] = 9; crc(other);
      bus.feed(other); hp->process();
      assert(std::isnan(hp->operating_170().heating_input));
    }
  }
  assert(bus.writes == 0);
  const byte heat[] = {0x28,0x2a,0,0,0x10};
  const byte water[] = {0x28,0x2e,0,0,0x10};
  const byte transition[] = {0x28,0xae,0,0,0x10};
  const byte idle[] = {0x28,0x08,0,0,0x10};
  ComfortzoneOperating170 o;
  o.power(300,2400,0,2400,800,1000); assert(std::isnan(o.result(1000).heating_input));
  o.status(heat,1100); assert(std::isnan(o.result(1100).heating_input));
  o.power(300,2400,0,2400,800,1400);
  auto r=o.result(1400); assert(r.heating_input==800 && r.water_input==0 && r.heating_cop==3);
  o.status(water,2000); assert(std::isnan(o.result(2000).heating_input));
  o.power(300,2400,0,2400,800,2300); assert(o.result(2300).water_input==800);
  o.status(water,9000); assert(o.result(9000).water_input==800);
  assert(std::isnan(o.result(18000).water_input)); // power expired independently of status
  o.power(300,2400,0,2400,800,18001); assert(std::isnan(o.result(18001).water_input)); // unpaired
  o.status(water,18100); o.power(300,2400,500,2900,800,18400);
  assert(o.result(18400).mode==CZMD_HOT_WATER && std::isnan(o.result(18400).water_input));
  o.power(300,2400,0,2400,0,18500); assert(o.result(18500).mode==CZMD_UNKNOWN);
  o.status(transition,19000); o.power(300,2400,0,2400,800,19300);
  assert(o.result(19300).mode==CZMD_UNKNOWN && !o.result(19300).production_known);
  o.power(0,0,0,0,0,19400); r=o.result(19400);
  assert(r.mode==CZMD_UNKNOWN && r.activity==CZCMP_STOPPED && r.heating_input==0 && r.water_input==0);
  assert(std::isnan(r.heating_cop) && std::isnan(r.water_cop));
  o.status(idle,19500); assert(o.result(19500).mode==CZMD_IDLE);
  assert(!o.result(35000).production_known);
  // Unsigned clock arithmetic survives the ESP32 millis wraparound.
  o.status(heat,0xffffff00U); o.power(300,2400,0,2400,800,0x20U);
  assert(o.result(0x100U).heating_input==800);
  assert(std::isnan(o.result(0x5000U).heating_input));
}
