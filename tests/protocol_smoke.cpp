#include "host_bus.h"
#include "register_fixture.h"
int main() {
  Bus bus; comfortzone_heatpump hp(&bus); hp.begin();
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
