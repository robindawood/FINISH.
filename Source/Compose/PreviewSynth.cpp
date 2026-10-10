#if defined (__GNUC__) && ! defined (__clang__)
 #pragma GCC optimize ("O2")   // GCC 13 -O3 mis-dispatched the drum switch in renderVoice (verified with sanitizers); O2 is correct
#endif
#include <iostream>
#include "PreviewSynth.h"

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    inline float white (juce::uint32& s) noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) s * (2.0f / 4294967295.0f) - 1.0f;
    }
    inline float blep (double t, double dt) noexcept
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    inline float saw (double& ph, double inc) noexcept
    {
        const float v = (float) (2.0 * ph - 1.0) - blep (ph, inc);
        ph += inc; if (ph >= 1.0) ph -= 1.0;
        return v;
    }
    inline float sine (double& ph, double inc) noexcept
    {
        const float v = (float) std::sin (twoPi * ph);
        ph += inc; if (ph >= 1.0) ph -= 1.0;
        return v;
    }
    inline float gOf (double fc, double sr) noexcept { return (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, fc) / sr); }
    inline double mtof (double m) noexcept { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

    constexpr float laneGain[numLanes] = { 1.0f, 0.55f, 0.17f, 0.2f, 0.11f, 0.5f };
    constexpr float laneSend[numLanes] = { 0.05f, 0.0f, 0.2f, 0.3f, 0.45f, 0.35f };
}

// ======================================================================================
PreviewSynth::Sequence PreviewSynth::compile (const Arrangement& arr, int onlySection)
{
    Sequence s;
    for (int i = 0; i < (int) arr.sections.size(); ++i)
    {
        if (onlySection >= 0 && i != onlySection) continue;
        const auto& sec = arr.sections[(size_t) i];
        const double base = sec.startBar * 4.0;
        for (int l = 0; l < numLanes; ++l)
        {
            const auto& clip = sec.clips[(size_t) l];
            for (auto& n : clip.notes)
                s.notes.push_back ({ base + n.beat, n.length, (juce::uint8) l, (juce::uint8) juce::jlimit (0, 127, n.pitch), n.velocity });
            for (auto& c : clip.cc) s.cc[(size_t) l].push_back ({ base + c.beat, c.value });
            for (auto& f : clip.fx) s.fx.push_back ({ base + f.beat, f.length, f.kind, f.level });
            for (auto& n : sec.layers[(size_t) l].notes)
                s.notes.push_back ({ base + n.beat, n.length, (juce::uint8) l, (juce::uint8) juce::jlimit (0, 127, n.pitch), n.velocity });
        }
        s.lengthBeats = std::max (s.lengthBeats, base + sec.lengthBeats());
    }
    std::stable_sort (s.notes.begin(), s.notes.end(), [] (const SeqNote& a, const SeqNote& b) { return a.beat < b.beat; });
    std::stable_sort (s.fx.begin(), s.fx.end(), [] (const SeqFx& a, const SeqFx& b) { return a.beat < b.beat; });
    for (auto& c : s.cc) std::stable_sort (c.begin(), c.end(), [] (const CCPoint& a, const CCPoint& b) { return a.beat < b.beat; });
    return s;
}

void PreviewSynth::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    juce::ignoreUnused (maxBlock);
    sendL.assign (512, 0.0f);   // render() works in chunks of 512 samples
    sendR.assign (512, 0.0f);
    reverb.setSampleRate (sr);
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.82f; rp.damping = 0.5f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
    reverb.setParameters (rp);
    reverb.reset();
    for (auto& v : voices) v.active = false;
    // Reserve so the audio-thread copy of a new sequence normally reuses storage.
    active.notes.reserve (20000); active.fx.reserve (256);
    for (auto& c : active.cc) c.reserve (2048);
}

void PreviewSynth::setSequence (const Sequence& s)
{
    {
        const juce::SpinLock::ScopedLockType sl (seqLock);
        pending = s;
    }
    ++pendingVersion;
}

void PreviewSynth::play (double fromBeat, double loopFrom, double loopTo)
{
    loopStart.store (loopFrom);
    loopEnd.store (loopTo);
    jumpTo.store (fromBeat);
    position.store (fromBeat);
    ++jumpRequest;
    playing.store (true);
}

void PreviewSynth::stop() { playing.store (false); }

float PreviewSynth::ccAt (int lane, double beat) const noexcept
{
    const auto& c = active.cc[(size_t) lane];
    if (c.empty()) return 0.8f;
    if (beat <= c.front().beat) return c.front().value;
    if (beat >= c.back().beat) return c.back().value;
    auto it = std::upper_bound (c.begin(), c.end(), beat, [] (double b, const CCPoint& p) { return b < p.beat; });
    const auto& b = *it; const auto& a = *(it - 1);
    const double u = (beat - a.beat) / std::max (1.0e-9, b.beat - a.beat);
    return a.value + (b.value - a.value) * (float) u;
}

void PreviewSynth::startNote (int lane, int pitch, float vel, double lengthSec) noexcept
{
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr)
    {
        double oldest = -1;
        for (auto& x : voices) if (x.t > oldest && x.fx == FxKind::None) { oldest = x.t; v = &x; }
        if (v == nullptr) return;
    }
    // Mono bass: release the previous bass note.
    if (lane == (int) Lane::Bass)
        for (auto& x : voices) if (x.active && x.lane == lane && ! x.released) { x.released = true; x.relLevel = x.env; x.relT = 0; }
    const juce::uint32 seed = v->noise;
    *v = Voice();
    v->noise = seed * 747796405u + 2891336453u + (juce::uint32) pitch;
    v->active = true; v->lane = lane; v->pitch = pitch; v->vel = vel; v->gateLen = lengthSec;
    v->pan = lane == (int) Lane::Drums ? ((pitch == Drum::closedHat || pitch == Drum::shaker) ? 0.25f : (pitch == Drum::openHat || pitch == Drum::rim ? -0.2f : 0.0f))
                                       : (lane == (int) Lane::Bass ? 0.0f : ((pitch % 5) - 2) * 0.12f);
}

void PreviewSynth::startFx (FxKind k, double lengthSec, float level) noexcept
{
    for (auto& x : voices)
        if (! x.active)
        {
            const juce::uint32 seed = x.noise;
            x = Voice();
            x.noise = seed * 2654435761u + 1u;
            x.active = true; x.lane = (int) Lane::Fx; x.fx = k; x.vel = level; x.fxLen = lengthSec; x.gateLen = lengthSec;
            return;
        }
}

void PreviewSynth::renderVoice (Voice& v, float* L, float* R, float* SL, float* SR, int n, const float* cutoff) noexcept
{
    const double dt = 1.0 / sr;
    const float gain = laneGain[v.lane] * v.vel;
    const float send = laneSend[v.lane];
    const float pl = std::sqrt (0.5f * (1.0f - v.pan)), pr = std::sqrt (0.5f * (1.0f + v.pan));

    for (int i0 = 0; i0 < n && v.active; i0 += 32)
    {
        const int len = std::min (32, n - i0);
        const double t0 = v.t;
        // per-chunk filter coefficients
        float g1 = 0, k1 = 1.4f;
        double f0 = mtof (v.pitch);
        const float cc = cutoff[v.lane];
        if (v.fx == FxKind::None)
        {
            switch ((Lane) v.lane)
            {
                case Lane::Bass:   g1 = gOf ((120.0 + 2600.0 * cc * cc) * (0.6 + 0.8 * std::exp (-t0 / 0.12)), sr); k1 = 1.0f; break;
                case Lane::Chords: g1 = gOf ((600.0 + 5200.0 * cc * cc) * (0.5 + 0.9 * std::exp (-t0 / 0.18)), sr); k1 = 1.2f; break;
                case Lane::Lead:   g1 = gOf ((900.0 + 6500.0 * cc * cc) * (0.55 + 0.8 * std::exp (-t0 / 0.25)), sr); k1 = 1.1f; break;
                case Lane::Pad:    g1 = gOf (300.0 + 4200.0 * cc * cc, sr); k1 = 1.3f; break;
                case Lane::Drums:
                default:
                    switch (v.pitch)
                    {
                        case Drum::snare: g1 = gOf (1800, sr); break;
                        case Drum::clap: g1 = gOf (1150, sr); k1 = 1.6f; break;
                        case Drum::closedHat: case Drum::openHat: g1 = gOf (7500, sr); k1 = 1.0f; break;
                        case Drum::shaker: g1 = gOf (6000, sr); k1 = 1.8f; break;
                        case Drum::crash: case Drum::ride: g1 = gOf (4500, sr); k1 = 1.0f; break;
                        default: g1 = gOf (3000, sr); break;
                    }
                    break;
                case Lane::Fx: break;
            }
        }
        else
        {
            const double u = juce::jlimit (0.0, 1.0, t0 / std::max (0.01, v.fxLen));
            double fc = 1000;
            switch (v.fx)
            {
                case FxKind::Riser:         fc = 250.0 * std::pow (36.0, u); break;
                case FxKind::Sweep:         fc = 300.0 * std::pow (20.0, u); break;
                case FxKind::Downlifter:    fc = 9000.0 * std::pow (1.0 / 40.0, u); break;
                case FxKind::ReverseCymbal: fc = 6000.0; break;
                case FxKind::Impact:        fc = 2500.0 * std::exp (-t0 * 3.0) + 200.0; break;
                case FxKind::None: break;
            }
            g1 = gOf (fc, sr); k1 = v.fx == FxKind::ReverseCymbal ? 1.0f : 0.8f;
        }

        for (int i = i0; i < i0 + len; ++i)
        {
            const double t = v.t;
            float sL = 0, sR = 0, amp = 0;
            bool done = false;
            if (v.fx != FxKind::None)
            {
                const double u = t / std::max (0.01, v.fxLen);
                const float nz = white (v.noise), nz2 = white (v.noise);
                switch (v.fx)
                {
                    case FxKind::Riser:
                    {
                        amp = (float) (u * u);
                        const double tone = 180.0 * std::pow (4.0, u);
                        const float s = v.f1.bp (nz, 0, g1, k1) * 1.4f + 0.12f * saw (v.phase[0], tone / sr) * (float) u;
                        sL = s; sR = v.f2.bp (nz2, 0, g1, k1) * 1.4f + 0.12f * saw (v.phase[1], tone * 1.005 / sr) * (float) u;
                        done = u >= 1.0; break;
                    }
                    case FxKind::Sweep:
                        amp = (float) std::pow (u, 1.5);
                        sL = v.f1.bp (nz, 0, g1, k1) * 1.3f; sR = v.f2.bp (nz2, 0, g1, k1) * 1.3f;
                        done = u >= 1.0; break;
                    case FxKind::Downlifter:
                        amp = (float) ((1.0 - u) * (1.0 - u));
                        sL = v.f1.bp (nz, 0, g1, k1) * 1.3f + 0.15f * sine (v.phase[0], (60.0 + 200.0 * (1.0 - u)) / sr) * amp;
                        sR = v.f2.bp (nz2, 0, g1, k1) * 1.3f + 0.15f * sine (v.phase[1], (60.0 + 200.0 * (1.0 - u)) / sr) * amp;
                        done = u >= 1.0; break;
                    case FxKind::ReverseCymbal:
                        amp = (float) std::pow (juce::jlimit (0.0, 1.0, u), 3.0);
                        sL = v.f1.hp (nz, 0, g1, k1); sR = v.f2.hp (nz2, 0, g1, k1);
                        done = u >= 1.0; break;
                    case FxKind::Impact:
                    {
                        const float boom = sine (v.phase[0], (32.0 + 70.0 * std::exp (-t * 6.0)) / sr) * (float) std::exp (-t / 0.9);
                        const float crack = v.f1.lp (nz, 0, g1, 0.9f) * (float) std::exp (-t / 0.22);
                        sL = boom * 1.1f + crack * 0.8f; sR = boom * 1.1f + v.f2.lp (nz2, 0, g1, 0.9f) * (float) std::exp (-t / 0.22) * 0.8f;
                        amp = 1.0f;
                        done = t > std::min (v.fxLen, 4.0); break;
                    }
                    case FxKind::None: break;
                }
            }
            else if (v.lane == (int) Lane::Drums)
            {
                const float nz = white (v.noise);
                float s = 0;
                switch (v.pitch)
                {
                    case Drum::kick:
                    {
                        const double f = 46.0 + 120.0 * std::exp (-t / 0.035);
                        s = 1.1f * sine (v.phase[0], f / sr) * (float) std::exp (-t / 0.22) + nz * 0.25f * (float) std::exp (-t / 0.003);
                        s = std::tanh (1.6f * s);
                        done = t > 0.7; break;
                    }
                    case Drum::snare:
                        s = 0.5f * sine (v.phase[0], 185.0 / sr) * (float) std::exp (-t / 0.09) + 0.7f * v.f1.hp (nz, 0, g1, k1) * (float) std::exp (-t / 0.15);
                        done = t > 0.6; break;
                    case Drum::clap:
                    {
                        const double tb = t < 0.03 ? std::fmod (t, 0.01) : t - 0.02;
                        s = 1.2f * v.f1.bp (nz, 0, g1, k1) * (float) std::exp (-tb / (t < 0.03 ? 0.004 : 0.16));
                        done = t > 0.7; break;
                    }
                    case Drum::closedHat: s = 0.6f * v.f1.hp (nz, 0, g1, k1) * (float) std::exp (-t / 0.035); done = t > 0.2; break;
                    case Drum::openHat:   s = 0.55f * v.f1.hp (nz, 0, g1, k1) * (float) std::exp (-t / 0.22); done = t > 1.0; break;
                    case Drum::shaker:    s = 0.5f * v.f1.bp (nz, 0, g1, k1) * (float) (std::min (1.0, t / 0.006) * std::exp (-t / 0.05)); done = t > 0.3; break;
                    case Drum::crash:     s = 0.45f * v.f1.hp (nz, 0, g1, k1) * (float) std::exp (-t / 1.3); done = t > 4.0; break;
                    case Drum::ride:
                        s = 0.3f * v.f1.hp (nz, 0, g1, k1) * (float) std::exp (-t / 0.5)
                          + 0.12f * (sine (v.phase[0], 3150.0 / sr) + sine (v.phase[1], 4720.0 / sr)) * (float) std::exp (-t / 0.4);
                        done = t > 2.0; break;
                    case Drum::lowTom: case Drum::midTom: case Drum::highTom:
                    {
                        const double base = v.pitch == Drum::lowTom ? 85.0 : (v.pitch == Drum::midTom ? 115.0 : 160.0);
                        s = 0.9f * sine (v.phase[0], (base * (1.0 + 0.6 * std::exp (-t / 0.05))) / sr) * (float) std::exp (-t / 0.22);
                        done = t > 0.9; break;
                    }
                    case Drum::rim: default:
                        s = 0.5f * (sine (v.phase[0], 820.0 / sr) + 0.5f * sine (v.phase[1], 1640.0 / sr)) * (float) std::exp (-t / 0.035);
                        done = t > 0.25; break;
                }
                sL = sR = s; amp = 1.0f;
            }
            else
            {
                // Melodic lanes: ADSR
                float a = 0.003f, d = 0.2f, sus = 0.8f, rel = 0.06f;
                switch ((Lane) v.lane)
                {
                    case Lane::Bass:   a = 0.003f; d = 0.25f; sus = 0.75f; rel = 0.05f; break;
                    case Lane::Chords: a = 0.004f; d = 0.22f; sus = 0.35f; rel = 0.15f; break;
                    case Lane::Lead:   a = 0.004f; d = 0.3f; sus = 0.6f; rel = 0.12f; break;
                    case Lane::Pad:    a = 0.35f; d = 0.6f; sus = 0.85f; rel = 0.8f; break;
                    case Lane::Drums: case Lane::Fx: break;
                }
                if (! v.released && t >= v.gateLen) { v.released = true; v.relLevel = v.env; v.relT = 0; }
                if (! v.released)
                    v.env = t < a ? (float) (t / a) : (float) (sus + (1.0f - sus) * std::exp (-(t - a) / d));
                else
                {
                    v.env = v.relLevel * (float) std::exp (-v.relT / (rel * 0.35));
                    v.relT += dt;
                    if (v.relT > rel * 3.0) done = true;
                }
                const double inc = f0 / sr;
                float x0 = 0, x1 = 0;
                switch ((Lane) v.lane)
                {
                    case Lane::Bass:
                        x0 = 0.55f * saw (v.phase[0], inc) + 0.7f * sine (v.phase[1], inc);
                        x0 = v.f1.lp (x0, 0, g1, k1); x1 = x0; break;
                    case Lane::Chords:
                        x0 = v.f1.lp (saw (v.phase[0], inc * 1.004), 0, g1, k1);
                        x1 = v.f2.lp (saw (v.phase[1], inc * 0.996), 0, g1, k1); break;
                    case Lane::Lead:
                    {
                        const float sq = (v.phase[0] < 0.5 ? 1.0f : -1.0f) - blep (v.phase[0], inc) + blep (std::fmod (v.phase[0] + 0.5, 1.0), inc);
                        v.phase[0] += inc; if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
                        const float s2 = saw (v.phase[1], inc * 1.003);
                        x0 = v.f1.lp (0.45f * sq + 0.55f * s2, 0, g1, k1); x1 = x0; break;
                    }
                    case Lane::Pad:
                        x0 = v.f1.lp (saw (v.phase[0], inc * 1.007) + 0.6f * saw (v.phase[2], inc * 0.5), 0, g1, k1);
                        x1 = v.f2.lp (saw (v.phase[1], inc * 0.993) + 0.6f * saw (v.phase[2], 0.0), 0, g1, k1); break;
                    case Lane::Drums: case Lane::Fx: break;
                }
                sL = x0; sR = x1; amp = v.env;
            }
            const float o = gain * amp;
            L[i] += sL * o * pl * 1.414f; R[i] += sR * o * pr * 1.414f;
            SL[i] += sL * o * send; SR[i] += sR * o * send;
            v.t += dt;
            if (done) { v.active = false; break; }
        }
    }
}

void PreviewSynth::render (float* L, float* R, int n, double bpm) noexcept
{
    // Pick up a new sequence.
    if (pendingVersion.load() != activeVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (seqLock);
        if (tl.isLocked())
        {
            active = pending;
            activeVersion = pendingVersion.load();
            nextNote = (size_t) (std::lower_bound (active.notes.begin(), active.notes.end(), pos, [] (const SeqNote& a, double b) { return a.beat < b; }) - active.notes.begin());
            nextFx = (size_t) (std::lower_bound (active.fx.begin(), active.fx.end(), pos, [] (const SeqFx& a, double b) { return a.beat < b; }) - active.fx.begin());
        }
    }
    const bool isOn = playing.load();
    bool anyVoice = false;
    for (auto& v : voices) anyVoice |= v.active;
    if (! isOn && ! anyVoice) return;

    if (! isOn)
        for (auto& v : voices) if (v.active && ! v.released) { v.released = true; v.relLevel = v.env; v.relT = 0; if (v.lane == (int) Lane::Drums || v.fx != FxKind::None) v.active = false; }

    const double bps = std::max (20.0, bpm) / 60.0 / sr;   // beats per sample
    for (int c0 = 0; c0 < n; c0 += (int) sendL.size())
    {
        const int len = std::min ((int) sendL.size(), n - c0);
        float* l = L + c0; float* r = R != nullptr ? R + c0 : nullptr;
        std::fill (sendL.begin(), sendL.begin() + len, 0.0f);
        std::fill (sendR.begin(), sendR.begin() + len, 0.0f);
        // Mono destination support: render into L and a scratch R.
        float scratchR[512];
        float* rr = r != nullptr ? r : scratchR;
        if (r == nullptr) std::fill (scratchR, scratchR + len, 0.0f);

        if (isOn && jumpRequest.load() != jumpSeen)
        {
            jumpSeen = jumpRequest.load();
            pos = jumpTo.load();
            for (auto& v : voices) v.active = false;
            nextNote = (size_t) (std::lower_bound (active.notes.begin(), active.notes.end(), pos, [] (const SeqNote& a, double b) { return a.beat < b; }) - active.notes.begin());
            nextFx = (size_t) (std::lower_bound (active.fx.begin(), active.fx.end(), pos, [] (const SeqFx& a, double b) { return a.beat < b; }) - active.fx.begin());
        }

        std::array<float, numLanes> cutoff {};
        for (int ln = 0; ln < numLanes; ++ln) cutoff[(size_t) ln] = ccAt (ln, pos);

        int done = 0;
        auto renderSegment = [&] (int upTo)
        {
            if (upTo <= done) return;
            for (auto& v : voices)
                if (v.active) renderVoice (v, l + done, rr + done, sendL.data() + done, sendR.data() + done, upTo - done, cutoff.data());
            done = upTo;
        };

        if (isOn)
        {
            const double lf = loopStart.load(), lt = loopEnd.load();
            const bool looping = lt > lf + 1.0e-6;
            int s = 0;
            while (s < len)
            {
                double segEndBeat = pos + (len - s) * bps;
                bool wrap = false;
                if (looping && segEndBeat >= lt) { segEndBeat = lt; wrap = true; }
                else if (! looping && active.lengthBeats > 0 && pos >= active.lengthBeats + 2.0) { playing.store (false); break; }
                // events in [pos, segEndBeat)
                for (;;)
                {
                    const bool hasNote = nextNote < active.notes.size() && active.notes[nextNote].beat < segEndBeat;
                    const bool hasFx = nextFx < active.fx.size() && active.fx[nextFx].beat < segEndBeat;
                    if (! hasNote && ! hasFx) break;
                    const double eb = hasNote && (! hasFx || active.notes[nextNote].beat <= active.fx[nextFx].beat) ? active.notes[nextNote].beat : active.fx[nextFx].beat;
                    const int at = s + juce::jlimit (0, len - s, (int) std::floor ((eb - pos) / bps));
                    renderSegment (at);
                    if (hasNote && active.notes[nextNote].beat <= eb + 1.0e-12)
                    {
                        const auto& nt = active.notes[nextNote++];
                        if (! laneMute[nt.lane].load()) startNote (nt.lane, nt.pitch, nt.velocity, nt.length / (bps * sr));
                    }
                    else
                    {
                        const auto& f = active.fx[nextFx++];
                        if (! laneMute[(size_t) Lane::Fx].load()) startFx (f.kind, f.length / (bps * sr), f.level);
                    }
                }
                const int segSamples = wrap ? juce::jlimit (0, len - s, (int) std::ceil ((lt - pos) / bps)) : len - s;
                renderSegment (s + segSamples);
                pos += segSamples * bps;
                s += segSamples;
                if (wrap)
                {
                    pos = lf;
                    nextNote = (size_t) (std::lower_bound (active.notes.begin(), active.notes.end(), pos, [] (const SeqNote& a, double b) { return a.beat < b; }) - active.notes.begin());
                    nextFx = (size_t) (std::lower_bound (active.fx.begin(), active.fx.end(), pos, [] (const SeqFx& a, double b) { return a.beat < b; }) - active.fx.begin());
                }
            }
            renderSegment (len);
        }
        else renderSegment (len);

        reverb.processStereo (sendL.data(), sendR.data(), len);
        for (int i = 0; i < len; ++i)
        {
            // master: -7 dB headroom, gentle soft clip only on peaks
            l[i] = std::tanh (0.45f * (l[i] + 0.6f * sendL[(size_t) i]));
            rr[i] = std::tanh (0.45f * (rr[i] + 0.6f * sendR[(size_t) i]));
        }
        if (r == nullptr) for (int i = 0; i < len; ++i) l[i] = 0.5f * (l[i] + rr[i]);
    }
    position.store (pos);
}

juce::AudioBuffer<float> PreviewSynth::renderOffline (const Sequence& s, double bpm, double sampleRate, double fromBeat, double toBeat)
{
    PreviewSynth synth;
    synth.prepare (sampleRate, 512);
    synth.setSequence (s);
    synth.play (fromBeat, 0.0, 0.0);
    const int total = (int) std::ceil ((toBeat - fromBeat) * 60.0 / bpm * sampleRate);
    juce::AudioBuffer<float> out (2, std::max (1, total));
    out.clear();
    for (int p = 0; p < total; p += 512)
    {
        const int len = std::min (512, total - p);
        synth.render (out.getWritePointer (0) + p, out.getWritePointer (1) + p, len, bpm);
    }
    return out;
}

juce::AudioBuffer<float> PreviewSynth::renderFx (FxKind kind, double lengthBeats, double bpm, double sampleRate, float level)
{
    Sequence s;
    s.fx.push_back ({ 0.0, lengthBeats, kind, level });
    s.lengthBeats = lengthBeats;
    const double tail = kind == FxKind::Impact ? std::max (lengthBeats, 4.0 * bpm / 60.0) : lengthBeats + 0.5;
    return renderOffline (s, bpm, sampleRate, 0.0, tail);
}
