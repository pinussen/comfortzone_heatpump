#pragma once

#include "comfortzone_status.h"
#include <cmath>
#include <cstdint>
#include <cstring>

// Correlate complete status and power replies. A heating/water request alone
// does not prove production. No generic bit interpretation for unknown states.
class ComfortzoneOperating170 {
public:
    static constexpr uint32_t MAX_AGE_MS = 15000;
    static constexpr uint32_t PAIR_WINDOW_MS = 2000;

    struct Result {
        COMFORTZONE_MODE mode = CZMD_UNKNOWN;
        COMFORTZONE_COMPRESSOR_ACTIVITY activity = CZCMP_UNKNOWN;
        bool production_known = false;
        float input_power = NAN;
        float heating_input = NAN, water_input = NAN;
        float heating_cop = NAN, water_cop = NAN;
    };

    void status(const byte *bytes, uint32_t now) {
        // An interruption or changed status invalidates the previous pairing.
        if (!status_seen_ || now-status_time_ > MAX_AGE_MS ||
            std::memcmp(bytes, status_, sizeof(status_)) != 0)
            paired_ = false;
        std::memcpy(status_, bytes, sizeof(status_));
        status_time_ = now;
        status_seen_ = true;
    }

    void power(uint16_t frequency, uint16_t output, uint16_t additional,
               uint16_t total, uint16_t input, uint32_t now) {
        frequency_ = frequency; output_ = output; additional_ = additional;
        total_ = total; input_ = input;
        power_time_ = now;
        power_seen_ = true;
        paired_ = status_seen_ && now-status_time_ <= PAIR_WINDOW_MS;
    }

    Result result(uint32_t now) const {
        Result r;
        if (!power_seen_ || now-power_time_ > MAX_AGE_MS) return r;
        // Total measured input remains useful when its destination is unknown.
        // Like the allocation, it must expire when complete power replies stop.
        if (input_ <= 32767) r.input_power = input_;
        // A complete zero-power snapshot proves zero allocation, even when the
        // operating mode is unknown (e.g. the observed defrost candidate 1E).
        if (frequency_ == 0 && input_ == 0 && output_ == 0 && additional_ == 0 && total_ == 0) {
            r.activity = CZCMP_STOPPED;
            r.production_known = true;
            r.heating_input = r.water_input = 0;
            if (status_seen_ && now-status_time_ <= MAX_AGE_MS && profile_mode() == CZMD_IDLE)
                r.mode = CZMD_IDLE;
            return r;
        }
        if (!paired_ || now-status_time_ > MAX_AGE_MS) return r;
        r.mode = profile_mode();
        if (r.mode != CZMD_ROOM_HEATING && r.mode != CZMD_HOT_WATER) {
            r.mode = CZMD_UNKNOWN;
            return r;
        }
        // Contradictory/invalid snapshots do not establish active production.
        if (frequency_ == 0 || frequency_ > 32767 || output_ == 0 || output_ > 32767 ||
            input_ == 0 || input_ > 32767 || total_ > 32767 || additional_ > 32767) {
            r.mode = CZMD_UNKNOWN;
            return r;
        }
        r.activity = CZCMP_RUNNING;
        r.production_known = true;
        // All panel comparisons used zero additional power. Do not assume
        // where the electric heater delivers its heat when it becomes active.
        if (additional_ != 0 || total_ != output_) return r;
        const float cop = static_cast<float>(output_) / input_;
        if (r.mode == CZMD_ROOM_HEATING) {
            r.heating_input = input_; r.water_input = 0; r.heating_cop = cop;
        } else {
            r.water_input = input_; r.heating_input = 0; r.water_cop = cop;
        }
        return r;
    }

private:
    COMFORTZONE_MODE profile_mode() const {
        // Observed 01:0017..001b profiles only. 2E is panel-confirmed water,
        // including after boost is switched off; it is not a 50/50 heat split.
        // 2C is supported by the older decoder and zero heating request in
        // captured water cycles. AE/BE/1E remain unknown, including defrost.
        if ((status_[0] != 0x28 && status_[0] != 0x2b) || status_[2] != 0 ||
            status_[3] != 0 || status_[4] != 0x10) return CZMD_UNKNOWN;
        switch (status_[1]) {
            case 0x08: return CZMD_IDLE;
            case 0x2a: return CZMD_ROOM_HEATING;
            case 0x2c: case 0x2e: return CZMD_HOT_WATER;
            default: return CZMD_UNKNOWN;
        }
    }

    byte status_[5]{};
    uint16_t frequency_{}, output_{}, additional_{}, total_{}, input_{};
    uint32_t status_time_{}, power_time_{};
    bool status_seen_ = false, power_seen_ = false, paired_ = false;
};
