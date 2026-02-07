#pragma once

// Second-order delay-locked loop for USB clock rate estimation.
// Filters noisy mach_absolute_time timestamps to extract the true sample rate
// of an independent USB audio device. Feed the output ratio to libsamplerate
// for adaptive drift correction.
//
// Based on Fons Adriaensen's technique (JACK zita-a2j).
// Used by the helper daemon only — the plugin never touches this.

#include <cmath>
#include <cstdint>
#include <mach/mach_time.h>

namespace flux {

class DriftTracker {
public:
    explicit DriftTracker(double nominalRate = 44100.0, double bandwidth = 1.0)
        : nominalRate_(nominalRate)
        , bandwidth_(bandwidth)
        , rate_(nominalRate)
    {
    }

    void update(uint64_t hostTime, uint32_t bufferFrames)
    {
        double t = hostTimeToSeconds(hostTime);

        if (!initialized_) {
            predictedTime_ = t;
            rate_ = nominalRate_;
            initialized_ = true;
            stableCount_ = 0;
            return;
        }

        double period = static_cast<double>(bufferFrames) / rate_;
        double omega = 2.0 * M_PI * bandwidth_ * period;
        double b = omega * 1.4142135623731;   // sqrt(2) — critically damped
        double c = omega * omega;

        double error = t - predictedTime_;
        predictedTime_ += period + b * error;
        integral_ += c * error;
        rate_ = static_cast<double>(bufferFrames) / (period + integral_);

        if (stableCount_ < 200) {
            ++stableCount_;
        }
    }

    void reset()
    {
        initialized_ = false;
        rate_ = nominalRate_;
        predictedTime_ = 0.0;
        integral_ = 0.0;
        stableCount_ = 0;
    }

    double rate() const { return rate_; }
    double nominalRate() const { return nominalRate_; }

    // Stable after ~50 callbacks (~1-2 seconds at typical buffer sizes).
    bool isStable() const { return initialized_ && stableCount_ > 50; }

private:
    static double hostTimeToSeconds(uint64_t hostTime)
    {
        // Apple Silicon has a non-trivial timebase (not 1:1 like Intel).
        static mach_timebase_info_data_t info = {};
        if (info.denom == 0) {
            mach_timebase_info(&info);
        }
        return static_cast<double>(hostTime)
             * static_cast<double>(info.numer)
             / static_cast<double>(info.denom)
             / 1e9;
    }

    double nominalRate_;
    double bandwidth_;
    double rate_;
    double predictedTime_ = 0.0;
    double integral_ = 0.0;
    bool   initialized_ = false;
    int    stableCount_ = 0;
};

} // namespace flux
