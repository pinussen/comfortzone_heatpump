# Protocol 170 read registers

Generated from `registers_v170.json`; do not edit this table manually.

These are read mappings, not write authorization. `inherited` means the existing
library interpretation still needs independent confirmation. Firmware keeps raw
integer units and sentinels; presentation divides by the stated divisor.

| Bank:address | Field | Type | Divisor | Unit | Raw bounds | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 00:02a5 | heating_curve | u8 | 1 |  | [1, 10] | capture |
| 00:02a6 | room_heating_setting | u16 | 10 | °C | [100, 500] | panel |
| 00:0373 | hot_water_setting | u16 | 10 | °C | [0, 1000] | panel |
| 00:03fa | fan_speed | u8 | 1 |  | [1, 3] | capture |
| 00:03d2 | hot_water_max_runtime | u8 | 1 | min | type range | inherited |
| 00:03d3 | hot_water_pause_time | u8 | 1 | min | type range | inherited |
| 00:03e4 | hot_water_compressor_min_frequency | u16 | 10 | Hz | type range | inherited |
| 00:03e6 | hot_water_compressor_max_frequency | u16 | 10 | Hz | type range | inherited |
| 00:03ec | hot_water_extra_setting | i16 | 10 | °C | type range | inherited |
| 00:03ee | hot_water_extra_time | u16 | 1 | min | type range | inherited |
| 00:03fb | normal_fan_speed | u16 | 10 | % | type range | inherited |
| 00:0419 | fan_time_to_filter_change | u16 | 1 | days | type range | inherited |
| 01:0032 | sensors_te0_outdoor_temp | i16 | 10 | °C | type range | inherited |
| 01:0034 | sensors_te1_flow_water | i16 | 10 | °C | type range | inherited |
| 01:0036 | sensors_te2_return_water | i16 | 10 | °C | type range | inherited |
| 01:0038 | sensors_te3_indoor_temp | i16 | 10 | °C | type range | inherited |
| 01:003a | sensors_te4_hot_gas_temp | i16 | 10 | °C | type range | inherited |
| 01:003c | sensors_te5_exchanger_out | i16 | 10 | °C | type range | inherited |
| 01:003e | sensors_te6_evaporator_in | i16 | 10 | °C | type range | inherited |
| 01:0040 | sensors_te7_exhaust_air | i16 | 10 | °C | type range | inherited |
| 01:0046 | sensors_te10_temp | i16 | 10 | °C | type range | inherited |
| 01:0062 | sensors_te24_hot_water_temp | i16 | 10 | °C | type range | inherited |
| 01:001f | pending_alarm_mask | u16 | 1 |  | type range | inherited |
| 01:0027 | acknowledged_alarm_mask | u16 | 1 |  | type range | inherited |
| 01:02e9 | heatpump_current_compressor_frequency | u16 | 10 | Hz | type range | panel |
| 01:0313 | heatpump_current_compressor_power | u16 | 1 | W | type range | panel |
| 01:0317 | heatpump_current_add_power | u16 | 1 | W | type range | panel |
| 01:0319 | heatpump_current_total_power | u16 | 1 | W | type range | inherited |
| 01:0321 | heatpump_current_compressor_input_power | u16 | 1 | W | type range | inherited |
| 05:002c | compressor_energy | u32 | 100 | kWh | type range | inherited |
| 05:0030 | add_energy | u32 | 100 | kWh | type range | inherited |
| 05:0034 | hot_water_energy | u32 | 100 | kWh | type range | inherited |
| 05:0038 | compressor_runtime | u32 | 1 | min | type range | inherited |
| 05:003c | total_runtime | u32 | 1 | min | type range | inherited |

Clock/calendar and derived flags are handled separately. No fragments are
combined between replies. See the schema for evidence notes and sentinel values.
