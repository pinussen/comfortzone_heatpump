# Protocol 170 and asynchronous writes

The ESPHome integration is https://github.com/pinussen/esphome-comfortzone.
Its `CODE-REVIEW.md` and `PROTOKOLL-170.md` document field evidence, live tests,
manual references and unresolved semantics. This repository owns the C++ sources;
ESPHome vendors a committed snapshot with per-file hashes.

`begin_write(kind, value)` validates and stages one command. Call `process()` and
`poll_write()` continuously; inspect `write_result()` / `last_message`. No blocking
API callback or automatic retransmission is required. The legacy synchronous
setters remain for existing callers.

The writer waits for a CRC-valid pump reply and a quiet bus. A 15-second
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

Actual compressor frequency is **01:02e9, LE16 / 10 Hz**, corrected after a
panel-observed heating -> extra hot water -> stop sequence on 2026-10-25.
The old 01:02d5 field remained at 29.3 Hz while the panel showed 30.0 Hz and
later zero. During ramp-up 01:02d9 already contained 30.0 while 01:02e9 was
29.7; stable operation alone cannot distinguish these fields.
Both the range decoder and legacy Status 06 structure now read 02e9.
Four captured frames in `tests/v170_frequency_fixtures.json` test heating,
ramp-up, hot water and zero output, plus the rebased legacy path and truncated
frequency replies. This change does not enable unverified operating-mode bits.

Heating register 00:02a6 has been written and read back at 32 C and 29.1 C.
The user confirmed that the later 29.1 C value was deliberately selected in
Home Assistant. It is not evidence of the pump reverting a setting.

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
