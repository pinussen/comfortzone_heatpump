#pragma once
#include "panel_fixture.h"
inline void test_panel_controls() {
  for (const auto &sample : panel_cases) {
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(!hp.priority_fresh() && !hp.fireplace_fresh() && !hp.extra_water_fresh());
    auto corrupt=sample.raw; corrupt.back()^=1; bus.feed(corrupt); hp.process();
    assert(!hp.priority_fresh() && !hp.fireplace_fresh() && !hp.extra_water_fresh());
    bus.feed(sample.raw); hp.process();
    auto &s=hp.comfortzone_status;
    if (std::string(sample.field)=="priority") {
      assert(hp.priority_fresh()); assert(s.hot_water_priority_setting==sample.value);
    } else if (std::string(sample.field)=="fireplace") {
      assert(hp.fireplace_fresh()); assert(s.fireplace_mode_enabled==bool(sample.value));
    } else { assert(hp.extra_water_fresh()); assert(s.extra_hot_water_setting==bool(sample.value)); }
    test_millis+=15001;
    assert(!hp.priority_fresh() && !hp.fireplace_fresh() && !hp.extra_water_fresh());
    // A one-byte fragment does not refresh a two-byte flag word.
    auto short_frame=sample.raw;
    short_frame.resize(23); short_frame[10]=23; crc(short_frame);
    bus.feed(short_frame); hp.process();
    assert(!hp.priority_fresh() && !hp.fireplace_fresh() && !hp.extra_water_fresh());
  }
  for (int priority=1;priority<=3;++priority) {
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::HOT_WATER_PRIORITY,priority));
    ready(hp,bus); test_millis+=40; hp.poll_write();
    assert(bus.sent[18]==0x40 && bus.sent[19]==0x0e && bus.sent[20]==0);
    assert(bus.sent[21]==0xf8 && bus.sent[22]==0xff);
    assert(!hp.begin_write(Kind::FAN_SPEED,1));
    bus.feed(ack(bus,0x41)); hp.process();
    assert(hp.write_busy() && hp.write_result()==Result::WAITING_FOR_BUS);
    assert(hp.write_transmitted() && !hp.comfortzone_status.hot_water_priority_setting.has_value());
    test_millis+=40; hp.poll_write(); assert(bus.writes==2);
    assert(bus.sent[18]==0x80 && bus.sent[19]==0x0e && bus.sent[21]==priority && bus.sent[22]==0);
    bus.feed(ack(bus,0x41));hp.process();assert(hp.write_result()==Result::SUCCESS);
    assert(!hp.comfortzone_status.hot_water_priority_setting.has_value());
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus);
    for(float v : {0.f,4.f,1.5f,std::numeric_limits<float>::quiet_NaN()})
      assert(!hp.begin_write(Kind::HOT_WATER_PRIORITY,v));
    assert(hp.begin_write(Kind::HOT_WATER_PRIORITY,1));
    ready(hp,bus);test_millis+=40;hp.poll_write();
    bus.feed(ack(bus,0x41));hp.process();
    test_millis+=15001;hp.poll_write();
    assert(hp.write_result()==Result::FAILED && hp.write_transmitted() && bus.writes==1);
    assert(std::string(hp.last_message).find("partially changed")!=std::string::npos);
  }
  for (int fail_phase : {1,2}) {
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::HOT_WATER_PRIORITY,2));
    ready(hp,bus);test_millis+=40;hp.poll_write();
    if (fail_phase==2) { bus.feed(ack(bus,0x41));hp.process();test_millis+=40;hp.poll_write(); }
    auto bad=ack(bus,0x41);bad.back()^=1;bus.feed(bad);hp.process();
    test_millis+=201;hp.poll_write();
    assert(hp.write_result()==Result::FAILED && bus.writes==fail_phase);
    ready(hp,bus);test_millis+=40;hp.poll_write();assert(bus.writes==fail_phase);
    assert(!hp.comfortzone_status.hot_water_priority_setting.has_value());
  }
  for (int ack_value : {8,12}) for (int enable : {0,1}) {
    Bus bus; comfortzone_heatpump hp(&bus);
    assert(hp.begin_write(Kind::EXTRA_HOT_WATER,enable));
    ready(hp,bus);test_millis+=40;hp.poll_write();
    bus.feed(ack(bus,ack_value));hp.process();
    assert(hp.write_result()==Result::SUCCESS);
    assert(!hp.extra_water_fresh()); // ACK alone does not fabricate readback.
  }
}
