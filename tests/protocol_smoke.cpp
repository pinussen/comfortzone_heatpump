#include "host_bus.h"
#include "register_fixture.h"
#include "comfortzone_frame.h"
int main() {
  Bus bus; comfortzone_heatpump hp(&bus); hp.begin();
#if HP_PROTOCOL == HP_PROTOCOL_2_50
  // Synthetic frames exercise the upstream 2.50 map independently of 170.
  assert(hp.last_frame_age() == (unsigned long)-1);
  auto status = pump_reply(); status.resize(0x88, 0); status[10]=0x88;
  const byte reg[] = {1,2,3,4,0x0b,0x10,1,4,0};
  std::copy(reg,reg+9,status.begin()+12);
  auto *q = reinterpret_cast<R_REPLY_STATUS_V221_x88 *>(status.data());
  q->unknown0[9]=0x20; q->unknown0[7]=0x20; q->unknown0[16]=3;
  q->unknown0[5]=0x0c; q->unknown0[20]=2; q->unknown0[21]=0;
  crc(status); bus.feed(status); while(bus.available()) hp.process();
  assert(hp.last_frame_age() == 0);
  test_millis += 10; auto bad=status; bad.back() ^= 1;
  bus.feed(bad); while(bus.available()) hp.process();
  assert(hp.last_frame_age() == 10);
  assert(hp.comfortzone_status.compressor_activity == CZCMP_RUNNING);
  assert(hp.comfortzone_status.extra_hot_water_setting);
  assert(hp.comfortzone_status.cooling_enabled);
  assert(hp.comfortzone_status.fireplace_mode_enabled);
  assert(hp.comfortzone_status.filter_alarm);
  assert(hp.comfortzone_status.alarm_pending_mask == 2);
  q->unknown0[28]=2; crc(status); bus.feed(status); while(bus.available()) hp.process();
  assert(!hp.comfortzone_status.filter_alarm);
  assert(!hp.begin_write(Kind::HEATING_CURVE, std::numeric_limits<float>::quiet_NaN()));
  assert(!hp.begin_write(Kind::HEATING_CURVE, 25.6f));
  assert(!hp.comfortzone_status.heating_curve.has_value());
  assert(hp.begin_write(Kind::HEATING_CURVE, 5.4f));
  // Staging a write must not publish success before an ACK.
  assert(!hp.comfortzone_status.heating_curve.has_value());
  test_millis += 15001; hp.poll_write();
  assert(hp.write_result() == Result::FAILED);
#endif
  // Use the actual register table (all versions), not a guessed protocol prefix.
  for (const auto &reg : registers) for (int size=22;size<=255;++size) {
    auto f=pump_reply(); f.resize(size,0x6a); f[10]=size;
    std::copy(reg,reg+9,f.begin()+12); crc(f);
    bus.feed(f); while(bus.available()) hp.process(); ++test_millis;
  }
  assert(bus.writes==0);
  std::cout << "Protocol " << HP_PROTOCOL << " sanitizer smoke passed; "
            << sizeof(registers)/9 << " register addresses x 234 lengths\n";
}
