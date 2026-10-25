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

The 34 ordinary scalar fields are now defined in `registers_v170.json`.
`python3 tools/generate_registers.py` validates the schema and generates
`comfortzone_registers_v170.h` and [REGISTERS-170.md](REGISTERS-170.md).
Use `--check` to detect stale generated files. CI also checks the schema.
ESPHome vendors the schema together with the C++ sources; its offline analyzer
reads this same file. The table is read-only: write commands and validation
remain in the existing write engine. Clock/calendar validation, derived alarm
flags, history and experimental float arrays remain separate.

`divisor` and `invalid_raw` describe presentation, while C++ status subscribers
continue receiving raw integers including sensor sentinels. Evidence is marked
per field: `inherited` is the existing interpretation, `capture` is supported by
captured comparisons, and `panel` has supporting panel observations. This
migration does not promote inherited interpretations to verified meanings.

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

## Operating status and power correlation (2026-10-25)

`operating_170(now_ms)` returns a time-qualified result; callers must query it
periodically, including during bus silence. The legacy `comfortzone_status.mode`
and activity subscribers are not used for this 170 result, because they cannot
expire on their own. Ordinary scalar readbacks remain unchanged.

The complete block at bank 01:0017..001b is compared with observed profiles:

| Bytes | Interpretation | Evidence |
| --- | --- | --- |
| 28 2A 00 00 10 | House heating, when paired with positive compressor measurements | User's panel comparison, and later passive heating cycle |
| 2B/28 2E 00 00 10 | Hot water, including after boost is switched off | User's panel comparison during boost/on/off sequence |
| 28 2C 00 00 10 | Hot water | Earlier capture with zero heating request and positive hot-water request; agrees with 1.80 logic, no independent panel confirmation for this exact profile |
| 28 08 00 00 10 | Idle, only with a complete zero-power snapshot | Captured zero frequency and all measured powers zero |
| 28 AE/BE 00 00 10 | Unknown transition | Captured ramp-down sequence |
| 28 1E 00 00 16 | Unknown mode; stopped compressor if complete power snapshot is zero | User saw a flashing snowflake and later zero values; defrost is plausible but not established |

The older 1.80 mode named “heating and hot water” sets only the hot-water
production flag. The panel confirms hot-water priority for 2E. It must not be
interpreted as equal energy allocation to heating and water.

A complete power reply must contain 01:02e9..0322: actual frequency, output,
additional power, total power and input. It must follow the complete status by
at most 2 seconds. Changed status invalidates the previous nonzero pairing.
Both snapshots expire after 15 seconds (normal polling is about 7 seconds).
Unsigned timestamp subtraction supports millis wraparound. Partial replies,
CRC errors, other senders and other protocol prefixes cannot create a pair.

With a recognized active mode, positive measurements, zero additional power
and total output equal to compressor output, input goes entirely to that mode.
CoP is pump-reported output/input, not an independent efficiency measurement.
Inactive CoP is undefined. Unknown transitions, stale samples and additional
heater operation deliberately leave allocated power/CoP unknown. A fresh
complete snapshot containing zero frequency and zero powers establishes zero
allocation for both uses, without assigning an unproven operating mode.

`tests/v170_status_fixtures.json` records captured frame bytes and provenance.
The host tests cover panel transitions, captured idle/water profiles, incomplete
and corrupt replies, changed status, staleness, zero division, additional power,
and timestamp wraparound. Raw profile interpretation is intentionally narrow;
this is not a complete definition of all status bits or defrost states.

## Clock writes

Protocol 170 now maps the existing single-byte hour/minute commands at 01:0004
and 01:0005, matching observed clock readbacks and older-version write maps.
`WriteKind::HOUR` and `MINUTE` use the asynchronous engine and strict echoed
value ACK matching. Bounds, fractional inputs, and non-finite values are
rejected before narrowing conversions. An ACK does not update the clock cache;
only a later valid full clock reply advances `clock_read_sequence()` and
`clock_read_time()`. The ESPHome time-sync command uses that independent readback.
Date, seconds, automatic DST, display luminosity and sensor offset writes are
not newly mapped for 170. Existence of legacy setters is not evidence of a
working 170 register mapping.
