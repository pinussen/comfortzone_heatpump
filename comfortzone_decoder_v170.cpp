#include "comfortzone_decoder.h"
#include "comfortzone_tools.h"
#include <cstring>

namespace {
// Every field must be wholly contained in THIS reply. No stale fragment cache.
struct Range {
    const byte *data;
    uint32_t start, size;
    bool decoded = false;
    const byte *at(uint32_t address, uint32_t count) {
        if (address < start || count > size || address - start > size - count) return nullptr;
        decoded = true;
        return data + address - start;
    }
    template<typename T> void u16(uint16_t address, Subscribable<T> &field) {
        if (auto p = at(address, 2)) field = static_cast<T>(get_uint16(p));
    }
    void temp(uint16_t address, Subscribable<int16_t> &field) {
        if (auto p = at(address, 2)) field = get_int16(p);
    }
    void counter(uint16_t address, Subscribable<uint32_t> &field) {
        if (auto p = at(address, 4)) field = get_uint32(p);
    }
};
bool valid_date(const byte *p, unsigned size) {
    if (size < 6 || p[0] > 23 || p[1] > 59 || p[2] > 59 || p[4] < 1 || p[4] > 12) return false;
    static const byte days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const unsigned year = 2000 + p[5];
    unsigned max = days[p[4]-1];
    if (p[4] == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ++max;
    return p[3] >= 1 && p[3] <= max;
}
}

bool czdec::decode_v170_ranges(comfortzone_heatpump *hp, CZ_PACKET_HEADER *frame) {
#if HP_PROTOCOL == HP_PROTOCOL_1_7
    static const byte prefix[] = {1,2,3,4,0x0b,8};
    if (frame->cmd != 'r' || frame->packet_size < sizeof(CZ_PACKET_HEADER)+1 ||
        std::memcmp(frame->reg_num, prefix, sizeof(prefix)) != 0) return false;
    Range r{reinterpret_cast<const byte *>(frame)+sizeof(CZ_PACKET_HEADER),
            static_cast<uint32_t>(frame->reg_num[7] | (frame->reg_num[8]<<8)),
            static_cast<uint32_t>(frame->packet_size-sizeof(CZ_PACKET_HEADER)-1)};
    if (r.start + r.size > 0x10000) return false;
    auto &s = hp->comfortzone_status;
    if (frame->reg_num[6] == 0) {
        if (auto p = r.at(0x2a5,1)) { if (*p <= 10) s.heating_curve = *p; }
        if (auto p = r.at(0x2a6,2)) { auto v=get_uint16(p); if (v>=100 && v<=500) s.room_heating_setting=v; }
        if (auto p = r.at(0x373,2)) { auto v=get_uint16(p); if (v<=1000) s.hot_water_setting=v; }
        if (auto p = r.at(0x3fa,1)) { if (*p>=1 && *p<=3) s.fan_speed=*p; }
        // Existing v170 X79 fields, also present in overlapping long replies.
        if (auto p = r.at(0x3d2,1)) s.hot_water_max_runtime=*p;
        if (auto p = r.at(0x3d3,1)) s.hot_water_pause_time=*p;
        r.u16(0x3e4,s.hot_water_compressor_min_frequency);
        r.u16(0x3e6,s.hot_water_compressor_max_frequency);
        r.temp(0x3ec,s.hot_water_extra_setting);
        r.u16(0x3ee,s.hot_water_extra_time);
        r.u16(0x3fb,s.normal_fan_speed);
        r.u16(0x419,s.fan_time_to_filter_change);
    } else if (frame->reg_num[6] == 1) {
        Subscribable<int16_t> *temps[] = {&s.sensors_te0_outdoor_temp,&s.sensors_te1_flow_water,
            &s.sensors_te2_return_water,&s.sensors_te3_indoor_temp,&s.sensors_te4_hot_gas_temp,
            &s.sensors_te5_exchanger_out,&s.sensors_te6_evaporator_in,&s.sensors_te7_exhaust_air};
        for (unsigned i=0;i<8;++i) r.temp(0x32+2*i,*temps[i]);
        r.temp(0x46,s.sensors_te10_temp); // Physical role unknown: keep TE10 name.
        r.temp(0x62,s.sensors_te24_hot_water_temp);
        // Both the short six-byte clock and larger temperature snapshots.
        if (auto p=r.at(4,6)) {
            if (valid_date(p,6)) {
                s.hour=p[0]; s.minute=p[1]; s.second=p[2]; s.day=p[3]; s.month=p[4]; s.year=2000+p[5];
                if (auto weekday=r.at(10,1)) { if (*weekday>=1 && *weekday<=7) s.day_of_week=*weekday; }
            }
        }
        // Raw masks retain all bits; do not invent names for undocumented alarms.
        r.u16(0x1f,s.pending_alarm_mask);
        r.u16(0x27,s.acknowledged_alarm_mask);
        if (r.at(0x1f,10)) {
            const auto active = static_cast<uint16_t>(s.pending_alarm_mask) &
                                ~static_cast<uint16_t>(s.acknowledged_alarm_mask);
            s.filter_alarm = (active & 2) != 0;
            s.general_alarm = (active & ~2) != 0;
        }
        // Already used by R_REPLY_STATUS_06_V170, now independent of block start.
        r.u16(0x2d5,s.heatpump_current_compressor_frequency);
        r.u16(0x313,s.heatpump_current_compressor_power);
        r.u16(0x317,s.heatpump_current_add_power);
        r.u16(0x319,s.heatpump_current_total_power);
        r.u16(0x321,s.heatpump_current_compressor_input_power);
        if (r.at(0x317,2)) s.additional_power_enabled = s.heatpump_current_add_power > 0;
    } else if (frame->reg_num[6] == 5) {
        r.counter(0x2c,s.compressor_energy); r.counter(0x30,s.add_energy); r.counter(0x34,s.hot_water_energy);
        r.counter(0x38,s.compressor_runtime); r.counter(0x3c,s.total_runtime);
    }
    return r.decoded;
#else
    return false;
#endif
}
