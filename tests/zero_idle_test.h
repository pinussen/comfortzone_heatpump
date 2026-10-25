#pragma once
// Write arbitration regression tests: zero-idle is opt-in and anchored to a
// complete CRC-valid pump reply, never just the last 20 buffered zero bytes.
inline void test_zero_byte_idle() {
  {
    Bus bus; comfortzone_heatpump hp(&bus); hp.set_zero_byte_idle(true);
    assert(hp.begin_write(Kind::MINUTE, 12));
    bus.feed(std::vector<byte>(80,0)); hp.process();
    test_millis+=100; hp.poll_write(); assert(bus.writes==0); // no reply
    ready(hp,bus);
    for(int i=0;i<39;++i) {
      ++test_millis; bus.feed({0}); hp.process(); hp.poll_write();
      assert(bus.writes==0);
    }
    ++test_millis; bus.feed({0}); hp.process(); hp.poll_write();
    assert(bus.writes==1 && !bus.sending && !bus.physical_idle_break);
    bus.feed(ack(bus)); hp.process(); assert(hp.write_result()==Result::SUCCESS);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus); hp.set_zero_byte_idle(true);
    assert(hp.begin_write(Kind::MINUTE,12)); ready(hp,bus);
    // Observed idle runs have isolated 0x40 among zeros. Each one restarts
    // the quiet timer, and must have shifted out of the parser before TX.
    for(int i=0;i<4;++i) {
      test_millis+=20; bus.feed({0x40}); hp.process(); hp.poll_write();
      assert(bus.writes==0);
    }
    for(int i=0;i<39;++i) {
      ++test_millis; bus.feed({0}); hp.process(); hp.poll_write();
      assert(bus.writes==0);
    }
    ++test_millis; bus.feed({0}); hp.process(); hp.poll_write();
    assert(bus.writes==1);
  }
  for (const auto &tail : {std::vector<byte>{0x41}, std::vector<byte>{0x65,0x6f,0xde,2}}) {
    Bus bus; comfortzone_heatpump hp(&bus); hp.set_zero_byte_idle(true);
    assert(hp.begin_write(Kind::MINUTE,12)); ready(hp,bus);
    bus.feed(tail); hp.process();
    // A nonzero prefix cannot be forgotten when the parser shifts it out.
    for(int i=0;i<60;++i) {
      ++test_millis; bus.feed({0}); hp.process(); hp.poll_write();
      assert(bus.writes==0);
    }
    ready(hp,bus); test_millis+=40;
    bus.feed({0}); hp.process(); hp.poll_write(); assert(bus.writes==1);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus); hp.set_zero_byte_idle(true);
    assert(hp.begin_write(Kind::MINUTE,12)); ready(hp,bus);
    test_millis+=40; bus.feed({0}); hp.process();
    bus.feed({0x65}); hp.poll_write(); assert(bus.writes==0); // unread UART
    hp.process(); hp.poll_write(); assert(bus.writes==0); // partial header
  }
  for (int scenario=0;scenario<4;++scenario) {
    Bus bus; comfortzone_heatpump hp(&bus); hp.set_zero_byte_idle(true);
    assert(hp.begin_write(Kind::MINUTE,12)); ready(hp,bus);
    auto f=pump_reply();
    if(scenario==0) f.back()^=1; // bad CRC
    if(scenario==1) { f[6]^=1; crc(f); } // other sender
    if(scenario==2) { f[11]='R'; crc(f); } // request
    if(scenario<3) { bus.feed(f); hp.process(); }
    else test_millis+=1000; // reply permission expires
    test_millis+=40; bus.feed(std::vector<byte>(80,0)); hp.process(); hp.poll_write();
    assert(bus.writes==0);
  }
  {
    Bus bus; comfortzone_heatpump hp(&bus); hp.set_zero_byte_idle(true);
    assert(hp.begin_write(Kind::MINUTE,12)); ready(hp,bus);
    hp.set_zero_byte_idle(false); test_millis+=40;
    bus.feed({0}); hp.process(); hp.poll_write(); assert(bus.writes==0);
  }
  {
    test_millis=0xfffffff0U;
    Bus bus; comfortzone_heatpump hp(&bus); hp.set_zero_byte_idle(true);
    assert(hp.begin_write(Kind::MINUTE,12)); ready(hp,bus);
    test_millis+=40; bus.feed({0}); hp.process(); hp.poll_write();
    assert(bus.writes==1); // unsigned elapsed time across wrap
    test_millis=100;
  }
}
