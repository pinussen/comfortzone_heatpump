# Protocol 170 and asynchronous writes

The ESPHome integration is https://github.com/pinussen/esphome-comfortzone.
Its `CODE-REVIEW.md` and `PROTOKOLL-170.md` document field evidence, live tests,
manual references and unresolved semantics. This repository owns the C++ sources;
ESPHome vendors a committed snapshot with per-file hashes.

`begin_write(kind, value)` validates and stages one command. Call `process()` and
`poll_write()` continuously; inspect `write_result()` / `last_message`. No blocking
API callback or automatic retransmission is required. The legacy synchronous
setters remain for existing callers.

The writer waits for a CRC-valid pump reply and a quiet bus. A five-second
bus timeout means nothing was sent. A missing matching acknowledgement within
200 ms after transmission means the outcome is unknown; do not blindly retry.

Supported asynchronous kinds: room/heating target, hot-water target, fan mode,
extra hot water, heating curve, fireplace and sensor offset. Register availability
is protocol dependent. Unsupported mappings fail without transmission. Protocol
170 sensor offsets are still unmapped. Fireplace keeps the previous header-only
ACK rule and has not been validated as a normal automation control.

For 170, field decoding uses bank/address ranges. Only complete fields in the
current CRC-valid reply from the configured pump are published. No partial-field
cache is used. `Subscribable::has_value()` distinguishes missing from zero.
Unverified mode bits are not borrowed from another protocol version.

Heating register 00:02a6 is called "Heating calculated setting" in the original
mapping. A previous write/readback of 32 C worked, but later passive traffic
reported 29.1 C. Persistent user-setpoint semantics are not yet established.
Do not equate register-write success with proven price-control behaviour.

Extra-hot-water buttons can send the captured bit commands, but there is no
verified 170 readback bit for the boost state. An ACK therefore does not publish
a lasting boost state that would become stale after automatic expiry.

Test with g++ and FastCRC c25258960e313a68237bf62db08770707955b82b:

```
python3 tests/run_host_tests.py --fastcrc /path/to/FastCRC
python3 tests/run_host_tests.py --fastcrc /path/to/FastCRC --protocol 160
python3 tests/run_host_tests.py --fastcrc /path/to/FastCRC --protocol 180
python3 tests/run_host_tests.py --fastcrc /path/to/FastCRC --protocol 221
```

Tests use a fake UART/clock plus ASan/UBSan. Other-protocol tests exercise register
lengths, not physical pumps or semantic accuracy. Capture replay and Python tests
live in the ESPHome repository. Tests do not contact a pump.
