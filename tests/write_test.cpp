#include "host_bus.h"
#ifdef TEST_ESPHOME_PROBE
#include "../components/comfortzone/comfortzone_probe.h"
#endif

int main(int argc, char **argv) {
#ifdef TEST_ESPHOME_PROBE
  {
    const std::array<uint8_t,23> captured_read = {0x65,0x6f,0xde,2,0xd3,0x5e,0x41,0x44,0x44,0x52,
      0x17,0x52,1,2,3,4,0x0b,8,0,0xa6,2,2,0x3f};
    assert(esphome::comfortzone::heating_read_probe(true) == captured_read);
    auto alternate = esphome::comfortzone::heating_read_probe(false);
    FastCRC8 c;
    assert(alternate[11] == 'R' && alternate[21] == 2);
    assert(c.maxim(alternate.data(),22) == alternate[22]);
  }
#endif
  {
    // Real v1.70 panel acknowledgement at 27.0 C, from March PCAP.
    Bus bus; comfortzone_heatpump hp(&bus);
    const byte panel[] = {0x41,0x44,0x44,0x52}; hp.set_controller_addr(panel);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 27)); ready(hp, bus);
    test_millis += 40; hp.poll_write();
    const std::vector<byte> captured = {0x41,0x44,0x44,0x52,0x07,0x8a,0x65,0x6f,0xde,0x02,
      0x17,0x77,1,2,3,4,0x0b,8,0,0xa6,2,1,0x45};
    bus.feed(captured); hp.process(); assert(hp.write_result() == Result::SUCCESS);
    assert(static_cast<int16_t>(hp.comfortzone_status.room_heating_setting) == 270);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 30)); ready(hp, bus);
    test_millis += 40; hp.poll_write();
    bus.feed(ack(bus, 0)); hp.process(); assert(hp.write_result() == Result::FAILED);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus);
    const byte panel[] = {0x41,0x44,0x44,0x52}; hp.set_controller_addr(panel);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 30)); ready(hp, bus);
    test_millis += 40; hp.poll_write(); assert(bus.writes == 1);
    assert(std::memcmp(bus.sent.data()+6, panel, 4) == 0);
    const byte other[] = {1,2,3,4}; hp.set_controller_addr(other);
    bus.feed(ack(bus)); hp.process(); assert(hp.write_result() == Result::SUCCESS);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus); hp.begin();
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 30)); ready(hp, bus);
    test_millis += 120;
    bus.feed({0}); hp.process(); hp.poll_write();
    assert(bus.writes == 0); // Zero-valued bytes alone never prove an idle bus.
    bus.physical_idle_break = true;
    bus.feed({0}); hp.process(); hp.poll_write();
    assert(bus.writes == 1); // Ignore only the idle tail, not legitimate payload zeros.
    bus.feed(ack(bus)); hp.process(); assert(hp.write_result() == Result::SUCCESS);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 30)); ready(hp, bus);
    test_millis += 120; bus.physical_idle_break = true;
    bus.feed({0x65,0x6f,0xde,0x02}); hp.process(); hp.poll_write();
    assert(bus.writes == 0); // Never discard a possible controller request.
    test_millis += 100; hp.process(); hp.poll_write(); assert(bus.writes == 0);
  }
  {
    // Live bus regression: trailing zero bytes remained as a 20-byte invalid
    // header forever and prevented every write, even across multi-second gaps.
    Bus bus; comfortzone_heatpump hp(&bus); hp.begin();
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 30));
    ready(hp, bus);
    bus.feed(std::vector<byte>(20, 0)); hp.process();
    for (int i = 0; i < 3; ++i) {
      test_millis += 50;
      bus.feed(std::vector<byte>(1, 0)); hp.process(); hp.poll_write();
      assert(bus.writes == 0); // Ongoing raw traffic must never count as quiet.
    }
    test_millis += 99; hp.process(); hp.poll_write(); assert(bus.writes == 0);
    test_millis++; hp.process(); hp.poll_write(); assert(bus.writes == 1);
    bus.feed(ack(bus)); hp.process(); assert(hp.write_result() == Result::SUCCESS);
  }
  {
    // A stalled possible controller request is ambiguous, unlike invalid noise.
    // Discarding it must revoke the previous reply's permission to transmit.
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 30)); ready(hp, bus);
    auto query = pump_reply(); query.resize(23); query[10] = 23; query[11] = 'R'; crc(query);
    bus.feed(std::vector<byte>(query.begin(), query.begin()+15)); hp.process();
    test_millis += 100; hp.process(); hp.poll_write(); assert(bus.writes == 0);
    ready(hp, bus); test_millis += 40; hp.poll_write(); assert(bus.writes == 1);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus); hp.begin();
    int updates = 0;
    hp.comfortzone_status.room_heating_setting.subscribe([](int16_t, void *p) {
      ++*static_cast<int *>(p);
    }, &updates);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 21.5f));
    assert(updates == 0);
    assert(!hp.begin_write(Kind::ROOM_TEMPERATURE, 22));
    hp.poll_write(); assert(bus.writes == 0); // No traffic: never send blindly.
    ready(hp, bus);
    test_millis += 39; hp.poll_write(); assert(bus.writes == 0);
    test_millis++; hp.poll_write(); assert(bus.writes == 1 && !bus.sending);
    assert(bus.sent[21] == 215 && bus.sent[22] == 0);
    auto wrong = ack(bus); wrong[0] ^= 1; crc(wrong);
    bus.feed(wrong); hp.process(); assert(hp.write_busy());
    auto broken = ack(bus); broken.back() ^= 1;
    bus.feed(broken); hp.process(); assert(hp.write_busy());
    bus.feed(ack(bus)); hp.process();
    assert(hp.write_result() == Result::SUCCESS && updates == 1);
    assert(static_cast<int16_t>(hp.comfortzone_status.room_heating_setting) == 215);
    assert(!hp.begin_write(Kind::ROOM_TEMPERATURE, std::numeric_limits<float>::quiet_NaN()));
    assert(!hp.begin_write(Kind::ROOM_TEMPERATURE, 51));
    assert(!hp.begin_write(Kind::SENSOR_OFFSET, 0, 8));
    // Unmapped registers must fail without sending guessed addresses.
    assert(!hp.begin_write(Kind::SENSOR_OFFSET, -1.5f, 3));
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus);
    test_millis = 0xfffffff0U;
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 20));
    test_millis += 14999; hp.poll_write(); assert(hp.write_busy());
    test_millis++; hp.poll_write();
    assert(hp.write_result() == Result::FAILED && bus.writes == 0);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 20));
    ready(hp, bus); test_millis += 40; hp.poll_write();

    test_millis += 200; hp.poll_write();
    assert(hp.write_result() == Result::FAILED && !bus.sending);
    bus.feed(ack(bus)); hp.process();
    assert(hp.write_result() == Result::FAILED); // Late ack cannot revive request.
    assert(bus.writes == 1); // No automatic resend after an ambiguous timeout.
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 20));
    ready(hp, bus); test_millis += 40; hp.poll_write();
    bus.feed(ack(bus, 0xff)); hp.process();
    assert(hp.write_result() == Result::FAILED);
    assert(hp.begin_write(Kind::FIREPLACE, 1));
    ready(hp, bus); test_millis += 40; hp.poll_write();
    bus.feed(ack(bus, 4)); hp.process(); assert(hp.write_result() == Result::SUCCESS);
  }
  {
    // Regression: raw logging used to overflow its 256-byte stack buffer.
    Bus bus; comfortzone_heatpump hp(&bus);
    auto long_frame = pump_reply();
    long_frame.resize(255, 0xaa); long_frame[10] = 255; crc(long_frame);
    bus.feed(long_frame);
    assert(hp.process() == comfortzone_heatpump::PFT_REPLY);
  }
  {
    // A partial frame or a controller query must prevent transmission.
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::ROOM_TEMPERATURE, 20));
    ready(hp, bus);
    auto query = pump_reply(); query.resize(23); query[10] = 23; query[11] = 'R'; crc(query);
    bus.feed(std::vector<byte>(query.begin(), query.begin() + 5)); hp.process();
    test_millis += 50; hp.poll_write(); assert(bus.writes == 0);
    bus.feed(std::vector<byte>(query.begin() + 5, query.end())); hp.process();
    test_millis += 50; hp.poll_write(); assert(bus.writes == 0);
    ready(hp, bus); test_millis += 40; hp.poll_write(); assert(bus.writes == 1);
  }
  {
    // A short settings reply follows a larger frame in the same RX buffer.
    // Bytes beyond its advertised length must never become a temperature.
    Bus bus; comfortzone_heatpump hp(&bus);
    int hot_updates = 0, filter_updates = 0;
    hp.comfortzone_status.hot_water_setting.subscribe([](int16_t value, void *p) {
      assert(value == 531); ++*static_cast<int *>(p);
    }, &hot_updates);
    hp.comfortzone_status.fan_time_to_filter_change.subscribe([](uint16_t value, void *p) {
      assert(value == 4); ++*static_cast<int *>(p);
    }, &filter_updates);
    auto poison = pump_reply(); poison.resize(255, 0x6a); poison[10] = 255; crc(poison);
    bus.feed(poison); hp.process();
    auto settings = pump_reply(); settings.resize(121, 0);
    settings[10] = 121; settings[18] = 0; settings[19] = 0xd2; settings[20] = 3;
    settings[47] = 0xbc; settings[48] = 2; // Extra hot water: 70 C, not normal target.
    settings[92] = 4; settings[93] = 0; crc(settings);
    bus.feed(settings); hp.process();
    assert(filter_updates == 1 && hot_updates == 0);
    settings.resize(100); settings[10] = 100; crc(settings);
    bus.feed(settings); hp.process(); assert(filter_updates == 2 && hot_updates == 0);
    // The actual 193-byte v170 settings message is not the 200-byte v180 layout.
    settings.resize(193, 0); settings[10] = 193; settings[19] = 0x20;
    settings[104] = 0x13; settings[105] = 0x02; // Panel-confirmed HW setting: 53.1 C.
    settings[123] = 0xff; settings[124] = 0x6a; // Old decode: 2739.1 C.
    crc(settings); bus.feed(settings); hp.process(); assert(hot_updates == 1);
    settings[104] = 0xff; settings[105] = 0xff;
    crc(settings); bus.feed(settings); hp.process(); assert(hot_updates == 1);
    settings.resize(121); settings[10] = 121; settings[19] = 0xd2;
    crc(settings); bus.feed(settings); hp.process(); assert(hot_updates == 1);
    assert(static_cast<int16_t>(hp.comfortzone_status.hot_water_setting) == 531);
    settings.resize(192, 0); settings[10] = 192; settings[19] = 0x20;
    settings[104] = 0x13; settings[105] = 0x02;
    crc(settings); bus.feed(settings); hp.process(); assert(hot_updates == 2);
  }
  {
    // Captured 2026-10-24, idle-edge/capture.pcap: frequent 01 04 00
    // reply works without ever receiving a large 01 00 00 snapshot.
    Bus bus; comfortzone_heatpump hp(&bus);
    int updates = 0;
    hp.comfortzone_status.sensors_te24_hot_water_temp.subscribe([](int16_t v, void *p) {
      assert(v == 517); ++*static_cast<int *>(p);
    }, &updates);
    std::vector<byte> f = {
      0x41,0x44,0x44,0x52,0x07,0x8a,0x65,0x6f,0xde,0x02,0x76,0x72,0x01,0x02,0x03,0x04,
      0x0b,0x08,0x01,0x04,0x00,0x17,0x1e,0x1f,0x18,0x0a,0x1a,0x06,0x00,0x17,0x1e,0x1f,
      0x00,0x00,0x40,0x00,0x40,0x00,0x00,0x00,0x28,0x08,0x00,0x00,0x10,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x41,0x19,
      0xfc,0x19,0xfc,0x67,0x00,0x6a,0x01,0x0a,0x01,0xef,0x00,0xc9,0x01,0x6f,0x01,0x41,
      0x00,0x21,0x00,0x22,0xfc,0x22,0xfc,0x05,0x01,0x22,0xfc,0x22,0xfc,0x22,0xfc,0x22,
      0xfc,0x22,0xfc,0x22,0xfc,0x22,0xfc,0x22,0xfc,0x22,0xfc,0x22,0xfc,0x22,0xfc,0x22,
      0xfc,0x22,0xfc,0x05,0x02,0x1c};
    bus.feed(f); assert(hp.process() == comfortzone_heatpump::PFT_REPLY);
    auto &s = hp.comfortzone_status;
    assert(updates == 1);
    assert(static_cast<int16_t>(s.sensors_te0_outdoor_temp) == 103);
    assert(static_cast<int16_t>(s.sensors_te1_flow_water) == 362);
    assert(static_cast<int16_t>(s.sensors_te2_return_water) == 266);
    assert(static_cast<int16_t>(s.sensors_te3_indoor_temp) == 239);
    assert(static_cast<int16_t>(s.sensors_te4_hot_gas_temp) == 457);
    assert(static_cast<int16_t>(s.sensors_te5_exchanger_out) == 367);
    assert(static_cast<int16_t>(s.sensors_te6_evaporator_in) == 65);
    assert(static_cast<int16_t>(s.sensors_te7_exhaust_air) == 33);
    f.back() ^= 1; bus.feed(f); hp.process(); assert(updates == 1);
    f.resize(28); f[10] = 28; crc(f); // Clock-only reply at same address.
    bus.feed(f); hp.process(); assert(updates == 1);
    f.resize(117, 0x6a); f[10] = 117; crc(f); // TE24 cut in half.
    bus.feed(f); hp.process(); assert(updates == 1);
    f.resize(118); f[10] = 118; f[17] = 9; crc(f); // Other protocol.
    bus.feed(f); hp.process(); assert(updates == 1);
    assert(bus.writes == 0);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus);
    int updates = 0;
    hp.comfortzone_status.compressor_energy.subscribe([](uint32_t v, void *p) {
      assert(v == 7030626); ++*static_cast<int *>(p);
    }, &updates);
    auto f = pump_reply(); bus.feed(f); hp.process();
    assert(updates == 1);
    auto &s = hp.comfortzone_status;
    assert(static_cast<uint32_t>(s.add_energy) == 632948);
    assert(static_cast<uint32_t>(s.hot_water_energy) == 1227234);
    assert(static_cast<uint32_t>(s.compressor_runtime) == 1537150);
    assert(static_cast<uint32_t>(s.total_runtime) == 4415444);
    f.resize(41); f[10] = 41; crc(f);
    bus.feed(f); hp.process(); assert(updates == 2 && bus.writes == 0);
    assert(static_cast<uint32_t>(s.total_runtime) == 4415444);
  }

  {
    // Commands publish only after a matching ACK, never during staging.
    for (auto setting : std::vector<std::pair<Kind,float>>{
         {Kind::HOT_WATER_TEMPERATURE,53.1f}, {Kind::FAN_SPEED,2},
         {Kind::HEATING_CURVE,5}, {Kind::EXTRA_HOT_WATER,1}}) {
      Bus bus; comfortzone_heatpump hp(&bus);
      assert(hp.begin_write(setting.first, setting.second));
      assert(!hp.comfortzone_status.hot_water_setting.has_value());
      assert(!hp.comfortzone_status.fan_speed.has_value());
      assert(!hp.comfortzone_status.heating_curve.has_value());
      assert(!hp.comfortzone_status.extra_hot_water_setting.has_value());
      ready(hp,bus); test_millis+=40; hp.poll_write(); assert(bus.writes==1);
      if (setting.first==Kind::HOT_WATER_TEMPERATURE) {
        assert(bus.sent[19]==0x73 && bus.sent[20]==3);
        assert(bus.sent[21]==0x13 && bus.sent[22]==2);
      }
      // Extra-HW reply contains status bits rather than the write-mask byte.
      if (setting.first==Kind::HOT_WATER_TEMPERATURE) {
        // Actual EX50 reply to 53.1 C, 2026-10-25, controller address Eric.
        bus.feed({0x45,0x72,0x69,0x63,0xae,0x23,0x65,0x6f,0xde,2,0x17,0x77,
                  1,2,3,4,0x0b,8,0,0x73,3,2,0xda});
      } else bus.feed(ack(bus,setting.first==Kind::EXTRA_HOT_WATER ? 8 : -1));
      hp.process();
      assert(hp.write_result()==Result::SUCCESS);
      if (setting.first==Kind::HOT_WATER_TEMPERATURE) assert(hp.comfortzone_status.hot_water_setting==531);
      if (setting.first==Kind::FAN_SPEED) assert(hp.comfortzone_status.fan_speed==2);
      if (setting.first==Kind::HEATING_CURVE) assert(hp.comfortzone_status.heating_curve==5);
      if (setting.first==Kind::EXTRA_HOT_WATER) assert(!hp.comfortzone_status.extra_hot_water_setting.has_value());
    }
    Bus bus; comfortzone_heatpump hp(&bus);
    for (auto kind : {Kind::FAN_SPEED,Kind::HEATING_CURVE,Kind::EXTRA_HOT_WATER}) {
      assert(!hp.begin_write(kind,1.5f)); assert(!hp.begin_write(kind,-1));
      assert(!hp.begin_write(kind,std::numeric_limits<float>::infinity()));
    }
    assert(!hp.begin_write(Kind::FAN_SPEED,0)); assert(!hp.begin_write(Kind::FAN_SPEED,4));
    assert(!hp.begin_write(Kind::HEATING_CURVE,0));
    assert(!hp.begin_write(Kind::HEATING_CURVE,11)); assert(!hp.begin_write(Kind::EXTRA_HOT_WATER,2));
    assert(bus.writes==0);
  }
  {
    // A field may arrive in any overlapping range, but never from stale bytes.
    Bus bus; comfortzone_heatpump hp(&bus);
    auto reply=[&](int bank,int address,std::vector<byte> data) {
      auto f=pump_reply(); f.resize(22+data.size()); f[10]=f.size();
      f[18]=bank; f[19]=address; f[20]=address>>8;
      std::copy(data.begin(),data.end(),f.begin()+21); crc(f); return f;
    };
    auto feed=[&](std::vector<byte> f) { bus.feed(f); hp.process(); };
    auto &s=hp.comfortzone_status;
    assert(!s.hot_water_setting.has_value()); assert(static_cast<int16_t>(s.hot_water_setting)==0);
    feed(reply(0,0x373,{0x13})); feed(reply(0,0x374,{2}));
    assert(!s.hot_water_setting.has_value());
    feed(reply(0,0x372,{0,0x13,2})); assert(s.hot_water_setting==531);
    auto wrong=reply(0,0x373,{0xf4,1}); wrong[6]^=1; crc(wrong); feed(wrong);
    assert(s.hot_water_setting==531);
    feed(reply(0,0x2a5,{5,0x40,1})); assert(s.heating_curve==5 && s.room_heating_setting==320);
    feed(reply(0,0x3fa,{3,0x66,3})); assert(s.fan_speed==3 && s.normal_fan_speed==870);
    feed(reply(1,0x46,{0x22,0xfc})); assert(s.sensors_te10_temp==-990);
    feed(reply(1,4,{23,59,59,29,2,24})); assert(s.year==2024 && s.day==29);
    feed(reply(1,4,{23,59,59,29,2,25})); assert(s.year==2024); // Invalid non-leap date.
    feed(reply(1,4,{24,0,0,1,1,26})); assert(s.year==2024);
    feed(reply(5,0x2c,{0xff,0xff,0xff,0xff})); assert(s.compressor_energy==0xffffffffU);
    // Partial range fuzz: sanitizers verify every advertised payload length.
    for (int bank : {0,1,3,5,255}) for (int size=0;size<=233;++size)
      feed(reply(bank,bank==0 ? 0x2a0 : 0,std::vector<byte>(size,0x6a)));
    assert(bus.writes==0);
  }
  {
    // Actual ~7-second polling periods exceed the former five-second queue limit.
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::HOT_WATER_TEMPERATURE,53.1f));
    test_millis+=7000; hp.poll_write(); assert(hp.write_busy() && bus.writes==0);
    ready(hp,bus); test_millis+=40; hp.poll_write(); assert(bus.writes==1);
    bus.feed(ack(bus)); hp.process(); assert(hp.write_result()==Result::SUCCESS);
  }
  size_t replayed = 0;
  if (argc == 2) {
    Bus bus; comfortzone_heatpump hp(&bus);
    std::ifstream input(argv[1], std::ios::binary);
    uint16_t size;
    while (input.read(reinterpret_cast<char *>(&size), sizeof(size))) {
      std::vector<byte> f(size);
      assert(input.read(reinterpret_cast<char *>(f.data()), size));
      bus.feed(f); test_millis++;
      while (bus.available()) hp.process();
      hp.poll_write(); ++replayed;
    }
    assert(bus.writes == 0);
  }
  std::cout << "Write tests passed; replayed " << replayed << " captured frames\n";
}
