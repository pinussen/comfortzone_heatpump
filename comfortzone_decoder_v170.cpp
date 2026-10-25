#include "comfortzone_decoder.h"
#include "comfortzone_registers_v170.h"
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
    using namespace cz_registers;
    if (frame->cmd != 'r' || frame->packet_size < sizeof(CZ_PACKET_HEADER)+1 ||
        std::memcmp(frame->reg_num, prefix_v170, sizeof(prefix_v170)) != 0) return false;
    Range r{reinterpret_cast<const byte *>(frame)+sizeof(CZ_PACKET_HEADER),
            static_cast<uint32_t>(frame->reg_num[7] | (frame->reg_num[8]<<8)),
            static_cast<uint32_t>(frame->packet_size-sizeof(CZ_PACKET_HEADER)-1)};
    if (r.start + r.size > 0x10000) return false;
    auto &s = hp->comfortzone_status;
    for (const auto &field : fields_v170) {
        if (field.bank != frame->reg_num[6]) continue;
        if (const auto p = r.at(field.address, field.width)) {
            uint32_t raw = 0;
            for (unsigned i = 0; i < field.width; ++i)
                raw |= static_cast<uint32_t>(p[i]) << (8 * i);
            int64_t value = raw;
            if (field.is_signed && (raw & (uint32_t{1} << (8 * field.width - 1))))
                value -= int64_t{1} << (8 * field.width);
            // A recognized but invalid value still counts as a handled field.
            // Preserve sentinels/raw units for existing status subscribers.
            if (value >= field.minimum && value <= field.maximum)
                field.publish(s, value);
        }
    }
    if (frame->reg_num[6] == 1) {
        const auto now = static_cast<uint32_t>(millis());
        if (const auto status = r.at(0x17, 5)) hp->operating_170_.status(status, now);
        // One contiguous snapshot must cover frequency and every power field;
        // do not combine a partial reply with previously cached scalar values.
        if (const auto power = r.at(0x2e9, 0x323-0x2e9)) {
            hp->operating_170_.power(get_uint16(power), get_uint16(power+0x313-0x2e9),
                get_uint16(power+0x317-0x2e9), get_uint16(power+0x319-0x2e9),
                get_uint16(power+0x321-0x2e9), now);
        }
        // Both the short six-byte clock and larger temperature snapshots.
        if (auto p=r.at(4,6)) {
            if (valid_date(p,6)) {
                ++hp->clock_read_sequence_; hp->clock_read_time_ = now;
                s.hour=p[0]; s.minute=p[1]; s.second=p[2]; s.day=p[3]; s.month=p[4]; s.year=2000+p[5];
                if (auto weekday=r.at(10,1)) { if (*weekday>=1 && *weekday<=7) s.day_of_week=*weekday; }
            }
        }
        // Raw masks retain all bits; do not invent names for undocumented alarms.
        if (r.at(0x1f,10)) {
            const auto active = static_cast<uint16_t>(s.pending_alarm_mask) &
                                ~static_cast<uint16_t>(s.acknowledged_alarm_mask);
            s.filter_alarm = (active & 2) != 0;
            s.general_alarm = (active & ~2) != 0;
        }
        if (r.at(0x317,2)) s.additional_power_enabled = s.heatpump_current_add_power > 0;

    }
    return r.decoded;
#else
    return false;
#endif
}
