# Upstream integration — 2026-11-14

Merged qix67/comfortzone_heatpump master through
317cfa6303ce18deacf75529f815034c2cd9ee11 (protocol 2.50 work by timdj).

Included: 2.50 register table and packet layouts, hot-water priority,
cooling/fireplace/boost flags, alarm masks and log decoding, filter controls,
heating-curve writes, bus freshness API and the MQTT example.

Compatibility decisions:

- Compiler-selected protocol remains authoritative; default remains 160.
- Protocol 170's byte-addressed decoder, observed operating-state rules,
  0315 heater register, async writes and bus-idle checks remain in place.
- Heating curve uses a single float setter: 170 accepts integers 1..10;
  250 accepts 0..25.5 and encodes tenths. Cached raw values retain each
  protocol's units. Nonfinite values are rejected before conversion.
- Async staging also applies to 250; staging never publishes a curve value.
  Its async bus scheduler remains conservative and can time out on a busy
  250 bus. The upstream synchronous 250 send/retry strategy remains separate.
- last_frame_age measures CRC-valid traffic; bad frames cannot refresh it.
- Subscribable retains explicit validity. Upstream's alarm mask field names
  coexist with the existing 170 names for source compatibility.

Validation: sanitizer register/length sweep for 160/170/180/221/250;
170 captured-fixture/write tests; synthetic 250 flag/alarm/staging tests.
No 250 hardware is available locally. New 250 synchronous controls are library
APIs, not newly exposed or hardware-validated ESPHome controls for our EX50.
The MQTT example is imported but not compiled in the host test suite.

## Leads for protocol 170 research

Upstream identifies priority in the low two bits of a shared flags word and
uses separate set/clear-bit addresses. It also names legionella parameters in
its 250 hot-water layout. These are useful comparison points, not evidence
that 170 uses the same addresses, offsets or masks. Match against our captures
and controlled panel changes before exposing additional writable settings.
