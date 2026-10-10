#include "ArrangementGenerator.h"
#include "MusicAnalyzer.h"

namespace ArrangementGenerator
{
namespace
{
    using Rng = juce::Random;

    float rnd (Rng& r) { return r.nextFloat(); }
    bool chance (Rng& r, float p) { return r.nextFloat() < p; }
    int pick (Rng& r, int n) { return n <= 1 ? 0 : r.nextInt (n); }

    bool isFourOnFloorStyle (Style s) { return s == Style::House || s == Style::Techno || s == Style::Melodic || s == Style::Trance; }

    // ------------------------------------------------------------------ pitch helpers
    int nearestOctave (int pc, int centre)
    {
        int p = centre - ((centre % 12) - pc);
        while (p - centre > 6) p -= 12;
        while (centre - p > 6) p += 12;
        return p;
    }

    int snapToScale (int pitch, const std::array<bool, 12>& scale, int dir = 0)
    {
        for (int d = 0; d < 7; ++d)
        {
            if (dir >= 0 && scale[(size_t) (((pitch + d) % 12 + 12) % 12)]) return pitch + d;
            if (dir <= 0 && scale[(size_t) (((pitch - d) % 12 + 12) % 12)]) return pitch - d;
        }
        return pitch;
    }

    // Move along the scale by `steps` degrees.
    int scaleStep (int pitch, int steps, const std::array<bool, 12>& scale)
    {
        int p = snapToScale (pitch, scale);
        const int dir = steps > 0 ? 1 : -1;
        for (int i = 0; i < std::abs (steps); ++i)
        {
            p += dir;
            while (! scale[(size_t) ((p % 12 + 12) % 12)]) p += dir;
        }
        return p;
    }

    std::vector<int> chordPcs (int root, bool minor, bool seventh, bool ninth, const std::array<bool, 12>& scale)
    {
        std::vector<int> pcs { root, (root + (minor ? 3 : 4)) % 12, (root + 7) % 12 };
        if (seventh) pcs.push_back ((root + (scale[(size_t) ((root + 10) % 12)] ? 10 : 11)) % 12);
        if (ninth) pcs.push_back ((root + 2) % 12);
        return pcs;
    }

    // Voice-led chord around `centre`.
    std::vector<int> voice (const std::vector<int>& pcs, int centre)
    {
        std::vector<int> v;
        for (int pc : pcs) v.push_back (nearestOctave (pc, centre));
        std::sort (v.begin(), v.end());
        v.erase (std::unique (v.begin(), v.end()), v.end());
        return v;
    }

    int bassPitch (int pc, int base) { int p = base + ((pc - base % 12) % 12 + 12) % 12; return p; }

    void add (Clip& c, double beat, double len, int pitch, float vel, double limit)
    {
        if (beat < -1.0e-9 || beat >= limit - 1.0e-9) return;
        len = std::min (len, limit - beat);
        if (len <= 0.01) return;
        c.notes.push_back ({ beat, len, juce::jlimit (0, 127, pitch), juce::jlimit (0.05f, 1.0f, vel) });
    }

    void ccRamp (Clip& c, double from, double to, float v0, float v1, int points = 9)
    {
        for (int i = 0; i < points; ++i)
        {
            const double t = (double) i / (points - 1);
            c.cc.push_back ({ from + (to - from) * t, juce::jlimit (0.0f, 1.0f, v0 + (v1 - v0) * (float) t) });
        }
    }

    double swingOffset (int step, float swing) { return (step % 2 == 1) ? swing * 0.25 : 0.0; }

    // ------------------------------------------------------------------ drums
    struct DrumMix
    {
        float kick = 0, snare = 0, clap = 0, chh = 0, ohh = 0, perc = 0, shaker = 0, ride = 0;
    };

    void drumBar (Clip& c, const Palette& p, int bar, const DrumMix& m, Rng& r, float humanise, double limit)
    {
        const double b0 = bar * 4.0;
        auto lay = [&] (const std::array<float, 16>& pat, int note, float level)
        {
            if (level <= 0.001f) return;
            for (int s = 0; s < 16; ++s)
                if (pat[(size_t) s] > 0.01f)
                    add (c, b0 + s * 0.25 + swingOffset (s, p.swing), note == Drum::openHat ? 0.24 : 0.12, note,
                         pat[(size_t) s] * level * (1.0f - humanise * 0.5f + humanise * rnd (r)), limit);
        };
        lay (p.kick, Drum::kick, m.kick);
        lay (p.snare, Drum::snare, m.snare);
        lay (p.clap, Drum::clap, m.clap);
        lay (p.chh, Drum::closedHat, m.chh);
        lay (p.ohh, Drum::openHat, m.ohh);
        lay (p.perc, Drum::rim, m.perc);
        if (m.shaker > 0.001f)
            for (int s = 0; s < 16; ++s) add (c, b0 + s * 0.25 + swingOffset (s, p.swing), 0.1, Drum::shaker, m.shaker * (s % 2 ? 0.75f : 0.45f), limit);
        if (m.ride > 0.001f)
            for (int s = 2; s < 16; s += 4) add (c, b0 + s * 0.25, 0.3, Drum::ride, m.ride * 0.7f, limit);
    }

    // Fill on the last `beats` beats of `bar`.
    void fill (Clip& c, int bar, double beats, int kind, float level, double limit, Rng& r)
    {
        const double start = bar * 4.0 + 4.0 - beats;
        const int n = (int) std::round (beats * 4.0);
        for (int i = 0; i < n; ++i)
        {
            const double t = start + i * 0.25;
            const float v = level * (0.55f + 0.45f * (float) i / std::max (1, n - 1));
            switch (kind % 4)
            {
                case 0: add (c, t, 0.12, Drum::snare, v, limit); break;                                   // snare run
                case 1: add (c, t, 0.15, i < n / 3 ? Drum::highTom : (i < 2 * n / 3 ? Drum::midTom : Drum::lowTom), v, limit); break;
                case 2: add (c, t, 0.12, i % 2 ? Drum::snare : Drum::clap, v, limit);
                        if (i % 4 == 0) add (c, t, 0.12, Drum::kick, v, limit);
                        break;
                default: if (i % 2 == 0 || chance (r, 0.5f)) add (c, t, 0.12, Drum::snare, v * (i % 2 ? 0.7f : 1.0f), limit); break;
            }
        }
    }

    // Snare roll accelerating over [fromBar, toBar).
    void roll (Clip& c, int fromBar, int toBar, float v0, float v1, int note, double limit, bool toThirtySeconds)
    {
        const int bars = std::max (1, toBar - fromBar);
        for (int b = fromBar; b < toBar; ++b)
        {
            const float u = (float) (b - fromBar) / bars;
            double div = u < 0.25f ? 1.0 : (u < 0.5f ? 0.5 : (u < 0.75f || ! toThirtySeconds ? 0.25 : 0.125));
            if (bars <= 2) div = b == toBar - 1 ? (toThirtySeconds ? 0.125 : 0.25) : 0.5;
            for (double t = 0; t < 4.0 - 1.0e-9; t += div)
            {
                const float w = (float) ((b - fromBar) * 4.0 + t) / (bars * 4.0f);
                add (c, b * 4.0 + t, std::min (div, 0.12), note, v0 + (v1 - v0) * w, limit);
            }
        }
    }

    // ------------------------------------------------------------------ bass
    void bassBar (Clip& c, const Palette& p, int bar, int chordRoot, int origRoot, float level, double limit, int variation, Rng& r, float creativity)
    {
        const double b0 = bar * 4.0;
        if (p.bassFromUser && ! p.bass.empty())
        {
            // Replay the user's line, transposed when the harmony moves away from the original loop.
            int shift = ((chordRoot - origRoot) % 12 + 12) % 12;
            if (shift > 6) shift -= 12;
            const double loopLen = p.bassBars * 4.0;
            const double offset = std::fmod (bar * 4.0, loopLen);
            for (auto& n : p.bass)
                if (n.beat >= offset - 1.0e-9 && n.beat < offset + 4.0 - 1.0e-9)
                {
                    int pitch = n.pitch + shift;
                    if (variation > 0 && chance (r, 0.15f * creativity + 0.1f)) pitch += chance (r, 0.5f) ? 12 : 7;
                    add (c, b0 + n.beat - offset, n.length, pitch, n.velocity * level, limit);
                }
            return;
        }
        const int root = bassPitch (chordRoot, p.bassOctaveBase);
        struct Hit { int step, len, interval; float vel; };
        std::vector<Hit> pat;
        switch (p.style)
        {
            case Style::House:   pat = { { 2, 2, 0, 0.9f }, { 6, 2, 0, 0.8f }, { 10, 2, 0, 0.9f }, { 13, 1, 12, 0.7f }, { 14, 2, 0, 0.8f } }; break;
            case Style::Techno:  for (int s = 0; s < 16; ++s) if (s % 4 != 0) pat.push_back ({ s, 1, (s % 8 == 3 && variation) ? 12 : 0, s % 2 ? 0.75f : 0.9f }); break;
            case Style::Melodic: pat = { { 2, 1, 0, 0.9f }, { 3, 1, 0, 0.7f }, { 6, 1, 0, 0.9f }, { 7, 1, 7, 0.7f }, { 10, 1, 0, 0.9f }, { 11, 1, 0, 0.7f }, { 14, 1, 0, 0.9f }, { 15, 1, 12, 0.7f } }; break;
            case Style::Trance:  for (int s = 2; s < 16; s += 4) { pat.push_back ({ s, 1, 0, 0.95f }); pat.push_back ({ s + 1, 1, 0, 0.75f }); } break;
            case Style::DnB:     pat = { { 0, 6, 0, 1.0f }, { 10, 6, variation ? 7 : 0, 0.9f } }; break;
            case Style::Trap:    pat = { { 0, 6, 0, 1.0f }, { 7, 3, 0, 0.85f }, { 10, 6, variation ? -2 : 0, 0.9f } }; break;
            case Style::HipHop:  pat = { { 0, 6, 0, 1.0f }, { 7, 2, 0, 0.8f }, { 10, 5, 7, 0.85f } }; break;
            case Style::Ambient: pat = { { 0, 16, 0, 0.8f } }; break;
        }
        for (auto& h : pat)
        {
            int pitch = root + h.interval;
            if (h.interval == -2) pitch = snapToScale (root - 2, p.scale, -1);
            if (variation > 1 && chance (r, 0.2f * creativity)) pitch = root + (chance (r, 0.5f) ? 12 : 7);
            add (c, b0 + h.step * 0.25 + swingOffset (h.step, p.swing), h.len * 0.25 - 0.02, pitch, h.vel * level, limit);
        }
    }

    // ------------------------------------------------------------------ motif
    std::vector<NoteEvent> generateMotif (const Palette& p, Rng& r, float creativity)
    {
        // Two bars, call and answer. Strong steps land on chord tones, the rest walks the scale.
        std::vector<NoteEvent> m;
        static const std::vector<std::vector<int>> rhythms = {
            { 0, 3, 6, 8, 10, 14 },            // syncopated
            { 0, 2, 4, 7, 10, 12, 14 },        // driving
            { 0, 3, 6, 10, 12 },               // sparse
            { 0, 2, 3, 6, 8, 11, 14 },         // rolling
            { 0, 4, 6, 8, 12 },                // straight
        };
        const auto& rhythm = rhythms[(size_t) pick (r, (int) rhythms.size())];
        const int centre = 69;
        int prev = nearestOctave (p.prog.empty() ? p.keyRoot : p.prog[0], centre);
        prev = snapToScale (prev, p.scale);
        std::vector<int> firstBarPitches;
        for (int bar = 0; bar < 2; ++bar)
        {
            const int chordRoot = p.prog.empty() ? p.keyRoot : p.prog[(size_t) bar % p.prog.size()];
            const bool chordMinor = p.progMinor.empty() ? p.minor : (bool) p.progMinor[(size_t) bar % p.progMinor.size()];
            const auto tones = chordPcs (chordRoot, chordMinor, false, false, p.scale);
            for (size_t i = 0; i < rhythm.size(); ++i)
            {
                const int s = rhythm[i];
                const int next = i + 1 < rhythm.size() ? rhythm[i + 1] : 16;
                int pitch;
                if (bar == 1 && i < firstBarPitches.size() && i + 2 < rhythm.size() && ! chance (r, 0.35f + 0.4f * creativity))
                    pitch = firstBarPitches[i];                                  // answer repeats the call...
                else if (s % 4 == 0)
                    pitch = nearestOctave (tones[(size_t) pick (r, (int) tones.size())], prev);
                else
                    pitch = scaleStep (prev, (chance (r, 0.5f) ? 1 : -1) * (1 + (chance (r, 0.3f) ? 1 : 0)), p.scale);
                if (bar == 1 && i + 1 == rhythm.size()) pitch = nearestOctave (p.keyRoot, prev);   // ...and resolves home
                pitch = juce::jlimit (60, 84, pitch);
                if (bar == 0) firstBarPitches.push_back (pitch);
                const double len = std::max (0.2, (next - s) * 0.25 - 0.05);
                m.push_back ({ bar * 4.0 + s * 0.25, len, pitch, (s % 4 == 0) ? 0.9f : 0.75f });
                prev = pitch;
            }
        }
        return m;
    }

    std::vector<NoteEvent> varyMotif (const std::vector<NoteEvent>& motif, const Palette& p, Rng& r, float amount)
    {
        std::vector<NoteEvent> out;
        for (auto n : motif)
        {
            if (chance (r, amount * 0.25f)) continue;                                   // drop a note
            if (chance (r, amount * 0.5f)) n.pitch = scaleStep (n.pitch, chance (r, 0.5f) ? 1 : -1, p.scale);
            out.push_back (n);
            if (chance (r, amount * 0.15f) && n.length >= 0.5)                          // split into two
            {
                out.back().length = n.length * 0.5 - 0.02;
                auto m = n; m.beat += n.length * 0.5; m.length = n.length * 0.5; m.pitch = scaleStep (n.pitch, 1, p.scale);
                out.push_back (m);
            }
        }
        return out.empty() ? motif : out;
    }

    // Fit a motif to a different chord: strong-beat notes move to the nearest chord tone.
    int fitToChord (int pitch, double beatInBar, int root, bool minor, const Palette& p)
    {
        const bool strong = std::fmod (beatInBar, 1.0) < 0.01;
        int out = snapToScale (pitch, p.scale);
        if (strong)
        {
            const auto tones = chordPcs (root, minor, false, false, p.scale);
            int best = out, bestD = 99;
            for (int t : tones) { const int c = nearestOctave (t, out); if (std::abs (c - out) < bestD) { bestD = std::abs (c - out); best = c; } }
            out = best;
        }
        return out;
    }

    // Lay the motif over [fromBar, toBar), following `chords` (one per bar).
    void motifBars (Clip& c, const Palette& p, const std::vector<NoteEvent>& motif, int motifBarsLen, int fromBar, int toBar,
                    const std::vector<int>& roots, const std::vector<bool>& minors, bool fit, float level, int transpose, double stretch, double limit)
    {
        if (motif.empty()) return;
        const double loopLen = motifBarsLen * 4.0 * stretch;
        for (double start = fromBar * 4.0; start < toBar * 4.0 - 1.0e-9; start += loopLen)
            for (auto& n : motif)
            {
                const double t = start + n.beat * stretch;
                if (t >= toBar * 4.0 - 1.0e-9) break;
                const int bar = (int) (t / 4.0);
                int pitch = n.pitch + transpose;
                if (fit && ! roots.empty())
                    pitch = fitToChord (pitch, std::fmod (t, 4.0), roots[(size_t) bar % roots.size()], minors[(size_t) bar % minors.size()], p) ;
                add (c, t, n.length * stretch, pitch, n.velocity * level, std::min (limit, toBar * 4.0));
            }
    }

    // ------------------------------------------------------------------ harmony
    void padBars (Clip& c, const Palette& p, int fromBar, int toBar, const std::vector<int>& roots, const std::vector<bool>& minors,
                  bool lush, float level, double limit, int centre = 60)
    {
        int ctr = centre;
        for (int b = fromBar; b < toBar; ++b)
        {
            const int root = roots[(size_t) b % roots.size()];
            const bool mn = minors[(size_t) b % minors.size()];
            auto v = voice (chordPcs (root, mn, lush, lush && (b % 2 == 1), p.scale), ctr);
            for (int pitch : v) add (c, b * 4.0, 4.0 - 0.02, pitch, level * 0.7f, limit);
            add (c, b * 4.0, 4.0 - 0.02, bassPitch (root, 36) + 12, level * 0.55f, limit);   // low root for weight
            int sum = 0; for (int x : v) sum += x;
            ctr = (sum / (int) v.size() + centre) / 2;
        }
    }

    void stabBars (Clip& c, const Palette& p, int fromBar, int toBar, const std::vector<int>& roots, const std::vector<bool>& minors,
                   int pattern, float level, double limit)
    {
        static const std::vector<std::vector<int>> pats = { { 2, 6, 10, 14 }, { 0, 3, 6, 10, 12 }, { 2, 5, 8, 11, 14 }, { 0, 6, 12 } };
        const auto& steps = pats[(size_t) (pattern % (int) pats.size())];
        int ctr = 64;
        for (int b = fromBar; b < toBar; ++b)
        {
            const auto v = voice (chordPcs (roots[(size_t) b % roots.size()], minors[(size_t) b % minors.size()], true, false, p.scale), ctr);
            for (int s : steps)
                for (int pitch : v) add (c, b * 4.0 + s * 0.25 + swingOffset (s, p.swing), 0.2, pitch, level * (s % 4 == 2 ? 0.9f : 0.75f), limit);
        }
    }

    void arpBars (Clip& c, const Palette& p, int fromBar, int toBar, const std::vector<int>& roots, const std::vector<bool>& minors,
                  float level, double limit, int shape, bool rising)
    {
        for (int b = fromBar; b < toBar; ++b)
        {
            const auto pcs = chordPcs (roots[(size_t) b % roots.size()], minors[(size_t) b % minors.size()], true, false, p.scale);
            std::vector<int> v;
            for (int o = 0; o < 2; ++o) for (int pc : pcs) v.push_back (nearestOctave (pc, 67) + 12 * o);
            std::sort (v.begin(), v.end());
            const int up = rising ? (b - fromBar) * 12 / std::max (1, toBar - fromBar) : 0;
            for (int s = 0; s < 16; ++s)
            {
                int idx;
                switch (shape % 3)
                {
                    case 0:  idx = s % (int) v.size(); break;                                          // up
                    case 1:  { const int n = (int) v.size(); const int k = s % (2 * n - 2); idx = k < n ? k : 2 * n - 2 - k; } break; // up-down
                    default: idx = (s * 3) % (int) v.size(); break;                                    // skipping
                }
                add (c, b * 4.0 + s * 0.25, 0.22, v[(size_t) idx] + (up >= 12 ? 12 : 0), level * (s % 4 == 0 ? 0.85f : 0.6f), limit);
            }
        }
    }

    // ------------------------------------------------------------------ default material per style
    void defaultDrums (Palette& p)
    {
        auto set = [] (std::array<float, 16>& a, std::initializer_list<int> steps, float v) { for (int s : steps) a[(size_t) s] = v; };
        p.kick.fill (0); p.snare.fill (0); p.clap.fill (0); p.chh.fill (0); p.ohh.fill (0); p.perc.fill (0);
        switch (p.style)
        {
            case Style::House:
                set (p.kick, { 0, 4, 8, 12 }, 1.0f); set (p.clap, { 4, 12 }, 0.9f); set (p.ohh, { 2, 6, 10, 14 }, 0.7f);
                set (p.chh, { 1, 3, 5, 7, 9, 11, 13, 15 }, 0.45f); set (p.perc, { 7, 15 }, 0.4f); break;
            case Style::Techno:
                set (p.kick, { 0, 4, 8, 12 }, 1.0f); set (p.clap, { 4, 12 }, 0.6f); set (p.ohh, { 2, 6, 10, 14 }, 0.75f);
                for (int s = 0; s < 16; ++s) p.chh[(size_t) s] = s % 2 ? 0.5f : 0.3f;
                set (p.perc, { 3, 11, 14 }, 0.5f); break;
            case Style::Melodic:
                set (p.kick, { 0, 4, 8, 12 }, 1.0f); set (p.clap, { 4, 12 }, 0.8f); set (p.ohh, { 2, 6, 10, 14 }, 0.6f);
                for (int s = 1; s < 16; s += 2) p.chh[(size_t) s] = 0.35f;
                set (p.perc, { 6, 14 }, 0.35f); break;
            case Style::Trance:
                set (p.kick, { 0, 4, 8, 12 }, 1.0f); set (p.clap, { 4, 12 }, 0.85f); set (p.ohh, { 2, 6, 10, 14 }, 0.8f);
                for (int s = 0; s < 16; ++s) p.chh[(size_t) s] = s % 2 ? 0.45f : 0.25f;
                break;
            case Style::DnB:
                set (p.kick, { 0, 10 }, 1.0f); set (p.snare, { 4, 12 }, 1.0f);
                for (int s = 0; s < 16; s += 2) p.chh[(size_t) s] = s % 4 ? 0.55f : 0.35f;
                set (p.perc, { 7, 14 }, 0.35f); break;
            case Style::Trap:
                set (p.kick, { 0, 7, 10 }, 1.0f); set (p.snare, { 8 }, 1.0f); set (p.clap, { 8 }, 0.6f);
                for (int s = 0; s < 16; s += 2) p.chh[(size_t) s] = 0.55f;
                set (p.chh, { 13, 15 }, 0.4f); break;
            case Style::HipHop:
                set (p.kick, { 0, 7, 10 }, 1.0f); set (p.snare, { 4, 12 }, 1.0f);
                for (int s = 0; s < 16; s += 2) p.chh[(size_t) s] = s % 4 ? 0.45f : 0.6f;
                break;
            case Style::Ambient:
                set (p.kick, { 0, 8 }, 0.7f); set (p.perc, { 6, 14 }, 0.3f);
                for (int s = 2; s < 16; s += 4) p.chh[(size_t) s] = 0.3f;
                break;
        }
    }

    void defaultProgression (Palette& p, Rng& r)
    {
        int roots[7]; bool qual[7];
        MusicAnalyzer::diatonicTriads (p.keyRoot, p.minor, roots, qual);
        // Degrees (0-based) of common loops.
        static const std::vector<std::vector<int>> minorLoops = { { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 0, 5, 3, 4 } };
        static const std::vector<std::vector<int>> majorLoops = { { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 0, 3, 4, 3 }, { 0, 5, 3, 4 } };
        const auto& loops = p.minor ? minorLoops : majorLoops;
        const auto& loop = loops[(size_t) pick (r, (int) loops.size())];
        p.prog.clear(); p.progMinor.clear();
        for (int d : loop) { p.prog.push_back (roots[d]); p.progMinor.push_back (qual[d]); }
    }

    void alternativeProgression (Palette& p, Rng& r, float creativity)
    {
        int roots[7]; bool qual[7];
        MusicAnalyzer::diatonicTriads (p.keyRoot, p.minor, roots, qual);
        auto degreeOf = [&] (int pc) { for (int d = 0; d < 7; ++d) if (roots[d] == pc) return d; return 0; };
        p.altProg.clear(); p.altMinor.clear();
        const int mode = pick (r, creativity > 0.6f ? 4 : 3);
        for (size_t i = 0; i < p.prog.size(); ++i)
        {
            int d = degreeOf (p.prog[i]);
            switch (mode)
            {
                case 0:  d = (int) ((d + 5) % 7); break;                  // shift the loop down a third (relative colour)
                case 1:  d = i % 2 == 0 ? (p.minor ? 5 : 3) : d; break;    // lean on VI / IV
                case 2:  d = (int) ((d + 3) % 7); break;                  // up a fourth
                default: d = std::vector<int> { 3, 5, 0, 6 } [i % 4]; break; // iv VI i VII (or IV vi I vii)
            }
            if (! p.minor && d == 6) d = 4;   // avoid vii° in major
            if (p.minor && d == 1) d = 3;     // avoid ii° in minor
            p.altProg.push_back (roots[d]); p.altMinor.push_back (qual[d]);
        }
        if (p.altProg == p.prog) { std::rotate (p.altProg.begin(), p.altProg.begin() + 1, p.altProg.end()); std::rotate (p.altMinor.begin(), p.altMinor.begin() + 1, p.altMinor.end()); }
    }

    // ------------------------------------------------------------------ section builders
    struct Ctx
    {
        const Palette& p;
        const MusicalDNA& dna;
        Flavour fl;
        float cr;          // creativity
        SectionMood mood;
        Rng& r;
        int bars;
        double limit;
        int index;         // position in the arrangement
        bool beforeDrop;   // next section is a drop
        bool afterDrop;
    };

    DrumMix fullDrums (const Palette& p, float level = 1.0f)
    {
        DrumMix m; m.kick = level; m.snare = level; m.clap = level; m.chh = level; m.ohh = level; m.perc = level * 0.9f;
        if (p.style == Style::Ambient) m.shaker = 0.4f * level;
        return m;
    }

    void buildIntro (GenSection& s, Ctx& c)
    {
        auto& D = s.clips[(size_t) Lane::Drums]; auto& B = s.clips[(size_t) Lane::Bass];
        auto& P = s.clips[(size_t) Lane::Pad];   auto& L = s.clips[(size_t) Lane::Lead]; auto& C = s.clips[(size_t) Lane::Chords];
        const auto& p = c.p; const int N = c.bars; const int half = N / 2;
        const bool atmo = c.fl == Flavour::Atmospheric || c.mood.atmos > 0 || c.mood.progressive > 0;
        const bool intense = c.fl == Flavour::Intense || c.mood.intense > 0;
        const int order = pick (c.r, 3);   // which element enters first (varies per seed)
        for (int b = 0; b < N; ++b)
        {
            const float rise = (float) b / std::max (1, N - 1);
            DrumMix m;
            if (atmo)
            {
                m.perc = 0.5f + 0.3f * rise; m.shaker = b >= N / 4 ? 0.35f : 0.0f; m.chh = b >= half ? 0.6f : 0.0f;
                m.kick = b >= half ? 0.9f : 0.0f; m.clap = b >= N - 4 ? 0.7f : 0.0f;
            }
            else if (intense)
            {
                m.kick = 1.0f; m.chh = b >= 2 ? 0.8f : 0.0f; m.ohh = b >= half / 2 ? 0.8f : 0.0f; m.clap = b >= half ? 0.9f : 0.0f; m.perc = b >= half ? 0.8f : 0.0f;
            }
            else
            {
                m.kick = order == 1 && b < 4 ? 0.0f : 1.0f;
                m.chh = (order == 0 ? b >= 4 : b >= 0) ? 0.5f + 0.3f * rise : 0.0f;
                m.ohh = b >= half ? 0.75f : 0.0f; m.clap = b >= half ? 0.85f : 0.0f; m.perc = b >= half + 2 ? 0.7f : 0.0f;
            }
            if (c.mood.minimal > 0) { m.perc = 0; m.ohh *= 0.5f; m.clap *= b >= half ? 1.0f : 0.0f; }
            drumBar (D, p, b, m, c.r, 0.08f, c.limit);
            if (b == N - 1 && (m.kick > 0 || m.clap > 0))
                fill (D, b, intense ? 2.0 : 1.0, pick (c.r, 4), 0.8f, c.limit, c.r);
        }
        if (intense) roll (D, N - 2, N, 0.4f, 0.95f, Drum::snare, c.limit, false);
        D.notes.erase (std::remove_if (D.notes.begin(), D.notes.end(), [&] (const NoteEvent& n) { return intense && n.beat >= (N - 1) * 4.0 + 3.5 && n.pitch == Drum::kick; }), D.notes.end());

        // Bass enters half-way (filtered), earlier when intense.
        const int bassFrom = intense ? N / 4 : (atmo ? N - 4 : half);
        for (int b = bassFrom; b < N; ++b)
        {
            const int root = p.prog[(size_t) b % p.prog.size()];
            if (atmo) add (B, b * 4.0, 4.0 - 0.05, bassPitch (root, p.bassOctaveBase), 0.7f, c.limit);
            else bassBar (B, p, b, root, p.prog[(size_t) b % p.prog.size()], 0.85f, c.limit, 0, c.r, c.cr);
        }
        if (! B.notes.empty()) ccRamp (B, bassFrom * 4.0, N * 4.0, 0.25f, 0.75f);

        // Pad identity from bar 0 (closed filter opening up), lush when atmospheric.
        if (! intense || c.mood.atmos > 0)
        {
            padBars (P, p, 0, N, p.prog, p.progMinor, atmo || p.style == Style::Melodic, atmo ? 0.75f : 0.55f, c.limit);
            ccRamp (P, 0.0, N * 4.0, 0.1f, atmo ? 0.7f : 0.55f);
        }
        if (intense) { stabBars (C, p, half, N, p.prog, p.progMinor, pick (c.r, 4), 0.65f, c.limit); ccRamp (C, half * 4.0, N * 4.0, 0.3f, 0.8f); }

        // The motif is teased before the drop.
        if (atmo)
        {
            motifBars (L, p, p.motif, p.motifBars, half, N, p.prog, p.progMinor, false, 0.6f, 0, 2.0, c.limit);
            ccRamp (L, half * 4.0, N * 4.0, 0.25f, 0.6f);
        }
        else if (! intense && p.motifBars > 0)
        {
            motifBars (L, p, p.motif, p.motifBars, std::max (half, N - 4), N, p.prog, p.progMinor, false, 0.55f, 0, 1.0, c.limit);
            ccRamp (L, std::max (half, N - 4) * 4.0, N * 4.0, 0.15f, 0.5f);
        }
        s.recipe = atmo ? juce::String (utf8 ("Ambiance d'abord : percussions et nappe, kick à mi-parcours, motif ralenti."))
                        : (intense ? juce::String (utf8 ("Démarre direct : kick dès la 1re mesure, stabs, roulement final."))
                                   : juce::String (utf8 ("Entrée progressive : kick, hats puis clap, basse filtrée, motif annoncé.")));
    }

    void buildBuild (GenSection& s, Ctx& c)
    {
        auto& D = s.clips[(size_t) Lane::Drums]; auto& B = s.clips[(size_t) Lane::Bass];
        auto& P = s.clips[(size_t) Lane::Pad];   auto& L = s.clips[(size_t) Lane::Lead]; auto& C = s.clips[(size_t) Lane::Chords];
        const auto& p = c.p; const int N = c.bars;
        const bool atmo = c.fl == Flavour::Atmospheric || c.mood.atmos > 0;
        const bool intense = c.fl == Flavour::Intense || c.mood.intense > 0;
        const int rollFrom = intense ? 0 : (atmo ? std::max (0, N - 4) : N / 2);
        const int leadMode = pick (c.r, 3);
        for (int b = 0; b < N; ++b)
        {
            DrumMix m;
            if (b < N - 1)
            {
                m.kick = atmo && b < N / 2 ? 0.0f : 1.0f;
                m.chh = 0.7f; m.ohh = b < N - 2 ? 0.7f : 0.0f; m.perc = intense ? 0.8f : 0.5f;
                if (intense && b >= N - 3)    // kick doubles up before the drop
                    for (int st = 2; st < 16; st += 4) add (D, b * 4.0 + st * 0.25, 0.1, Drum::kick, 0.8f, c.limit);
            }
            drumBar (D, p, b, m, c.r, 0.05f, c.limit);
        }
        roll (D, rollFrom, N, atmo ? 0.25f : 0.35f, 1.0f, atmo ? Drum::clap : Drum::snare, c.limit, ! atmo);
        if (! atmo) add (D, (N - 1) * 4.0 + 3.0, 0.5, Drum::crash, 0.0f, c.limit);   // placeholder removed below
        D.notes.erase (std::remove_if (D.notes.begin(), D.notes.end(), [] (const NoteEvent& n) { return n.velocity < 0.06f && n.pitch == Drum::crash; }), D.notes.end());

        // Bass: the groove under an opening filter, then silence on the last beat (the "breath" before the drop).
        for (int b = 0; b < N; ++b)
        {
            const int root = p.prog[(size_t) b % p.prog.size()];
            if (atmo) add (B, b * 4.0, 4.0 - 0.05, bassPitch (root, p.bassOctaveBase), 0.7f, c.limit);
            else if (intense && b >= N - 2)
                for (int st = 0; st < 16; st += 2) add (B, b * 4.0 + st * 0.25, 0.4, bassPitch (root, p.bassOctaveBase) + (b == N - 1 ? 12 : 0), 0.85f, c.limit);
            else bassBar (B, p, b, root, root, 0.85f, c.limit, 0, c.r, c.cr);
        }
        B.notes.erase (std::remove_if (B.notes.begin(), B.notes.end(), [&] (const NoteEvent& n) { return n.beat >= N * 4.0 - 1.0; }), B.notes.end());
        ccRamp (B, 0.0, N * 4.0, 0.3f, 1.0f);

        padBars (P, p, 0, N, p.prog, p.progMinor, true, 0.6f, c.limit);
        ccRamp (P, 0.0, N * 4.0, 0.25f, 1.0f);
        if (intense)
        {
            for (int b = 0; b < N; ++b)   // stabs climbing to 8ths
            {
                const auto v = voice (chordPcs (p.prog[(size_t) b % p.prog.size()], p.progMinor[(size_t) b % p.progMinor.size()], false, false, p.scale), 66);
                const double div = b < N / 2 ? 1.0 : 0.5;
                for (double t = 0; t < 4.0 - 1.0e-9; t += div)
                    for (int pitch : v) add (C, b * 4.0 + t, std::min (div, 0.3), pitch, 0.5f + 0.45f * (float) b / N, c.limit);
            }
        }

        // Lead tension: motif with the filter opening, or a rising arpeggio, or repeated notes accelerating.
        if (atmo || leadMode == 1)
            arpBars (L, p, 0, N, p.prog, p.progMinor, 0.65f, c.limit, pick (c.r, 3), true);
        else if (leadMode == 2 && ! p.motif.empty())
        {
            const int note = p.motif.front().pitch;
            for (int b = 0; b < N; ++b)
            {
                const double div = b < N / 2 ? 0.5 : 0.25;
                for (double t = 0; t < 4.0 - 1.0e-9; t += div) add (L, b * 4.0 + t, div * 0.8, note + (b >= N - 2 ? 12 : 0), 0.5f + 0.4f * (float) b / N, c.limit);
            }
        }
        else motifBars (L, p, p.motif, p.motifBars, 0, N, p.prog, p.progMinor, false, 0.8f, 0, 1.0, c.limit);
        ccRamp (L, 0.0, N * 4.0, 0.3f, 1.0f);
        L.notes.erase (std::remove_if (L.notes.begin(), L.notes.end(), [&] (const NoteEvent& n) { return n.beat >= N * 4.0 - 0.5; }), L.notes.end());
        s.recipe = intense ? juce::String (utf8 ("Tension maximale : caisse claire de la 1re à la dernière mesure, kick doublé, stabs qui montent."))
                           : (atmo ? juce::String (utf8 ("Montée douce : arpège ascendant, roulement de clap sur 4 mesures, filtre qui s'ouvre."))
                                   : juce::String (utf8 ("Montée classique : roulement qui accélère, filtre basse/nappe qui s'ouvre, silence sur le dernier temps.")));
    }

    void buildDrop (GenSection& s, Ctx& c, bool second)
    {
        auto& D = s.clips[(size_t) Lane::Drums]; auto& B = s.clips[(size_t) Lane::Bass];
        auto& P = s.clips[(size_t) Lane::Pad];   auto& L = s.clips[(size_t) Lane::Lead]; auto& C = s.clips[(size_t) Lane::Chords];
        const auto& p = c.p; const int N = c.bars;
        const bool atmo = c.fl == Flavour::Atmospheric || c.mood.atmos > 0;
        const bool intense = c.fl == Flavour::Intense || c.mood.intense > 0;
        const int fillKind = pick (c.r, 4);
        const int stabPattern = pick (c.r, 4);
        for (int b = 0; b < N; ++b)
        {
            auto m = fullDrums (p);
            if (second)
            {
                if (intense) m.ride = 0.8f;
                if (b >= N / 2 || intense) m.shaker = 0.45f;
            }
            drumBar (D, p, b, m, c.r, 0.06f, c.limit);
            if (second ? (b % 4 == 3) : (b % 8 == 7)) fill (D, b, b == N - 1 ? 2.0 : 1.0, fillKind + b / 4, 0.8f, c.limit, c.r);
            if (b == 0 || (second && b == N / 2)) add (D, b * 4.0, 1.5, Drum::crash, 0.9f, c.limit);
        }
        // Accents: extra kick/clap pickups for "plus percutant" moods.
        if (c.mood.intense > 0 || (second && intense))
            for (int b = 1; b < N; b += 2) add (D, b * 4.0 + 3.75, 0.1, Drum::clap, 0.7f, c.limit);

        for (int b = 0; b < N; ++b)
        {
            const int root = p.prog[(size_t) b % p.prog.size()];
            bassBar (B, p, b, root, root, 0.95f, c.limit, second ? 1 + (b % 2) : 0, c.r, c.cr);
        }
        // Chords: stabs on four-on-the-floor styles / intense, pad otherwise.
        if (isFourOnFloorStyle (p.style) || intense)
            stabBars (C, p, 0, N, p.prog, p.progMinor, stabPattern, intense ? 0.8f : 0.65f, c.limit);
        padBars (P, p, 0, N, p.prog, p.progMinor, atmo || p.style == Style::Melodic, atmo ? 0.6f : 0.4f, c.limit);

        // Lead: the motif (yours or FINISH's); drop 2 varies it.
        std::vector<NoteEvent> motif = p.motif;
        int transpose = 0;
        if (second)
        {
            motif = varyMotif (p.motif, p, c.r, 0.25f + 0.6f * c.cr);
            transpose = intense ? 12 : 0;
        }
        motifBars (L, p, motif, p.motifBars, 0, N, p.prog, p.progMinor, false, 0.9f, transpose, 1.0, c.limit);
        if (second && ! intense)
        {
            // Second half: harmony a third above the motif (counter-line).
            Clip harm;
            motifBars (harm, p, motif, p.motifBars, N / 2, N, p.prog, p.progMinor, false, 0.6f, 0, 1.0, c.limit);
            for (auto& n : harm.notes) { n.pitch = scaleStep (n.pitch, 2, p.scale); L.notes.push_back (n); }
        }
        if (atmo) { ccRamp (L, 0.0, N * 4.0, 0.7f, 0.7f, 2); }
        s.recipe = second
            ? (intense ? juce::String (utf8 ("Reprise du drop une octave plus haut, ride, fills toutes les 4 mesures."))
                       : juce::String (utf8 ("Ton drop varié : motif retouché, contre-chant à la tierce en 2e moitié, fills, basse qui saute d'octave.")))
            : juce::String (utf8 ("Drop complet construit sur ton motif, ta tonalité et ton groove."));
    }

    void buildBreak (GenSection& s, Ctx& c, bool bridge)
    {
        auto& D = s.clips[(size_t) Lane::Drums]; auto& B = s.clips[(size_t) Lane::Bass];
        auto& P = s.clips[(size_t) Lane::Pad];   auto& L = s.clips[(size_t) Lane::Lead]; auto& C = s.clips[(size_t) Lane::Chords];
        const auto& p = c.p; const int N = c.bars; const int half = N / 2;
        const bool atmo = c.fl == Flavour::Atmospheric || c.mood.atmos > 0;
        const bool intense = c.fl == Flavour::Intense || c.mood.intense > 0;
        const bool dark = c.mood.darker > 0;
        const auto& roots = bridge ? p.altProg : p.prog;
        const auto& mins = bridge ? p.altMinor : p.progMinor;
        const int texture = pick (c.r, 3);   // 0 pad + slow motif, 1 arpeggio, 2 piano-like chords

        // Drums breathe: nothing / light percussion, a half-time pulse when intense, back in the last bars.
        for (int b = 0; b < N; ++b)
        {
            DrumMix m;
            if (intense) { m.chh = 0.5f; m.perc = 0.5f; }
            else if (! atmo && b >= half) { m.chh = 0.4f; m.perc = 0.35f; }
            if (bridge && ! atmo) { m.perc = 0.5f; m.shaker = b >= 2 ? 0.3f : 0.0f; }
            drumBar (D, p, b, m, c.r, 0.1f, c.limit);
            if (intense || (bridge && b >= half)) { add (D, b * 4.0, 0.2, Drum::kick, 0.8f, c.limit); add (D, b * 4.0 + 2.0, 0.2, Drum::clap, 0.7f, c.limit); }
        }
        if (! atmo) fill (D, N - 1, 1.0, pick (c.r, 4), 0.7f, c.limit, c.r);

        // Bass: absent at first, long roots later (dark = an octave lower).
        for (int b = (atmo ? N : half); b < N; ++b)
            add (B, b * 4.0, 4.0 - 0.05, bassPitch (roots[(size_t) b % roots.size()], p.bassOctaveBase) - (dark ? 12 : 0) + (dark && p.bassOctaveBase < 31 ? 12 : 0), 0.7f, c.limit);

        padBars (P, p, 0, N, roots, mins, true, 0.75f, c.limit, dark ? 55 : 62);
        ccRamp (P, 0.0, N * 4.0, dark ? 0.25f : 0.45f, dark ? 0.45f : 0.75f);

        if (texture == 1 || atmo)
            arpBars (C, p, 0, N, roots, mins, 0.45f, c.limit, pick (c.r, 3), false);
        else if (texture == 2)
        {
            int ctr = dark ? 57 : 64;
            for (int b = 0; b < N; ++b)
            {
                const auto v = voice (chordPcs (roots[(size_t) b % roots.size()], mins[(size_t) b % mins.size()], true, false, p.scale), ctr);
                for (int i = 0; i < (int) v.size(); ++i) add (C, b * 4.0 + i * 0.25, 4.0 - i * 0.25 - 0.05, v[(size_t) i], 0.6f, c.limit);   // rolled chord
                add (C, b * 4.0 + 2.5, 1.4, v.back(), 0.45f, c.limit);
            }
        }

        // Melody: the motif slowed down (break), or a new line from it (bridge).
        std::vector<NoteEvent> motif = p.motif;
        if (bridge)
        {
            // Inversion around the first note + light variation: new melody, same DNA.
            if (! motif.empty())
            {
                const int axis = motif.front().pitch;
                for (auto& n : motif) n.pitch = snapToScale (axis - (n.pitch - axis), p.scale);
            }
            motif = varyMotif (motif, p, c.r, 0.3f + 0.5f * c.cr);
        }
        const double stretch = atmo || ! bridge ? 2.0 : 1.0;
        const int leadFrom = intense ? N / 4 : (atmo ? 0 : half);
        motifBars (L, p, motif, p.motifBars, leadFrom, N, roots, mins, true, bridge ? 0.8f : 0.6f, dark ? -12 : 0, stretch, c.limit);
        ccRamp (L, leadFrom * 4.0, N * 4.0, 0.35f, 0.7f);

        s.recipe = bridge
            ? juce::String (utf8 ("Pont : nouvelle couleur harmonique (")) + juce::String ((int) roots.size()) + utf8 (" accords), mélodie inversée dérivée de ton motif, groove minimal.")
            : (atmo ? juce::String (utf8 ("Respiration : sans batterie, nappe large, arpège, motif deux fois plus lent."))
                    : juce::String (utf8 ("Break : la batterie s'efface, accords tenus, motif ralenti, la basse revient en 2e moitié.")));
        if (dark) s.recipe << utf8 (" Version sombre : registre grave, filtre plus fermé.");
    }

    void buildOutro (GenSection& s, Ctx& c)
    {
        auto& D = s.clips[(size_t) Lane::Drums]; auto& B = s.clips[(size_t) Lane::Bass];
        auto& P = s.clips[(size_t) Lane::Pad];   auto& L = s.clips[(size_t) Lane::Lead];
        const auto& p = c.p; const int N = c.bars; const int half = N / 2;
        const bool atmo = c.fl == Flavour::Atmospheric || c.mood.atmos > 0 || c.mood.progressive > 0;
        for (int b = 0; b < N; ++b)
        {
            const float fade = 1.0f - 0.6f * (float) b / N;
            DrumMix m;
            if (atmo) { if (b < half) { m.kick = fade; m.chh = 0.6f * fade; m.perc = 0.5f; } else { m.perc = 0.4f * fade; m.shaker = 0.3f * fade; } }
            else
            {
                m.kick = b < N - 2 ? fade : 0.0f; m.chh = 0.6f * fade; m.ohh = b < half ? 0.7f : 0.0f;
                m.clap = b < half + 2 ? 0.8f * fade : 0.0f; m.perc = b < half ? 0.6f : 0.0f;
            }
            drumBar (D, p, b, m, c.r, 0.06f, c.limit);
        }
        for (int b = 0; b < (atmo ? half : half + 2) && b < N; ++b)
            bassBar (B, p, b, p.prog[(size_t) b % p.prog.size()], p.prog[(size_t) b % p.prog.size()], 0.85f, c.limit, 0, c.r, c.cr);
        ccRamp (B, 0.0, half * 4.0 + 8.0, 1.0f, 0.3f);
        padBars (P, p, 0, N, p.prog, p.progMinor, true, 0.6f, c.limit);
        // Final chord held on the tonic.
        P.notes.erase (std::remove_if (P.notes.begin(), P.notes.end(), [&] (const NoteEvent& n) { return n.beat >= (N - 2) * 4.0; }), P.notes.end());
        for (int pitch : voice (chordPcs (p.keyRoot, p.minor, true, false, p.scale), 60)) add (P, (N - 2) * 4.0, 8.0 - 0.05, pitch, 0.6f, c.limit);
        ccRamp (P, 0.0, N * 4.0, 0.8f, 0.1f);
        motifBars (L, p, p.motif, p.motifBars, 0, atmo ? N - 2 : half, p.prog, p.progMinor, false, 0.6f, 0, atmo ? 2.0 : 1.0, c.limit);
        ccRamp (L, 0.0, N * 4.0, 0.7f, 0.15f);
        s.recipe = atmo ? juce::String (utf8 ("Fin en douceur : la batterie part à mi-parcours, motif ralenti, accord final tenu."))
                        : juce::String (utf8 ("Sortie miroir de l'intro : basse puis clap s'en vont, kick jusqu'au bout, accord final sur la tonique."));
    }

    // "Make the drop hit harder" on the user's own drop: a drum layer of accents that fits their groove
    // (pickups, ghost claps, open hats where their groove is empty, fills and crashes on the phrase ends).
    void buildAccentLayer (GenSection& s, Ctx& c)
    {
        auto& A = s.layers[(size_t) Lane::Drums];
        A = {};
        const auto& p = c.p; const int N = c.bars;
        const bool sparse = c.dna.density < 2.2f;
        for (int b = 0; b < N; ++b)
        {
            const double b0 = b * 4.0;
            if (b % 8 == 0) add (A, b0, 1.5, Drum::crash, 0.85f, c.limit);
            for (int st = 0; st < 16; ++st)
            {
                const bool userHit = p.kick[(size_t) st] > 0 || p.clap[(size_t) st] > 0 || p.snare[(size_t) st] > 0;
                if (st == 15 && b % 2 == 1) add (A, b0 + st * 0.25, 0.1, Drum::clap, 0.6f, c.limit);           // pickup into the next bar
                if (sparse && ! userHit && p.chh[(size_t) st] <= 0 && st % 2 == 1) add (A, b0 + st * 0.25 + swingOffset (st, p.swing), 0.1, Drum::closedHat, 0.45f, c.limit);
                if (st == 14 && b % 4 == 3) add (A, b0 + st * 0.25, 0.1, Drum::kick, 0.8f, c.limit);           // double kick at phrase end
            }
            if (b % 4 == 3) fill (A, b, b % 8 == 7 ? 2.0 : 1.0, pick (c.r, 4), 0.75f, c.limit, c.r);
        }
        std::sort (A.notes.begin(), A.notes.end(), [] (const NoteEvent& x, const NoteEvent& y) { return x.beat < y.beat; });
        s.recipe = utf8 ("Ton drop reste le tien. Calque d'accents ajouté (piste DRUMS) : crash tous les 8 temps, relances de clap, fills en fin de phrase")
                 + (sparse ? utf8 (", hats en contretemps là où ton groove respire.") : utf8 ("."))
                 + utf8 (" + traitement PUNCH sur le master.");
    }

    void buildSectionImpl (GenSection& s, const Palette& p, const MusicalDNA& dna, Flavour fl, float cr, int index, bool beforeDrop, bool afterDrop)
    {
        for (auto& c : s.clips) c = {};
        Rng r ((juce::int64) s.seed * 7919 + (juce::int64) s.role * 104729 + 17);
        Ctx c { p, dna, fl, juce::jlimit (0.0f, 1.0f, cr + 0.3f * s.mood.progressive), s.mood, r, s.bars, s.lengthBeats(), index, beforeDrop, afterDrop };
        switch (s.role)
        {
            case Role::Intro:  buildIntro (s, c); break;
            case Role::Build:  buildBuild (s, c); break;
            case Role::Drop:
                buildDrop (s, c, false);
                // The user's own drop: the MIDI here is only a sketch for previewing the whole arrangement in FINISH.
                if (s.isReference)
                {
                    s.recipe = utf8 ("Ton drop reste le tien : FINISH n'exporte que l'impact sur la piste FX (l'esquisse sert à la préécoute).");
                    if (s.mood.intense > 0) buildAccentLayer (s, c);
                }
                break;
            case Role::Drop2:  buildDrop (s, c, true); break;
            case Role::Break:  buildBreak (s, c, false); break;
            case Role::Bridge: buildBreak (s, c, true); break;
            case Role::Outro:  buildOutro (s, c); break;
        }
        // Melodic mood boosts the lead, minimal strips extra layers.
        if (s.mood.minimal > 0) { s.clips[(size_t) Lane::Chords] = {}; if (s.role != Role::Drop2) s.clips[(size_t) Lane::Pad].notes.clear(); }
        if (s.mood.melodic > 0 && s.clips[(size_t) Lane::Lead].notes.empty() && ! s.isReference)
            motifBars (s.clips[(size_t) Lane::Lead], p, p.motif, p.motifBars, 0, s.bars, p.prog, p.progMinor, s.role == Role::Bridge, 0.75f, 0, 1.0, s.lengthBeats());
        for (auto& clip : s.clips)
            std::sort (clip.notes.begin(), clip.notes.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.beat < b.beat - 1.0e-9 || (std::abs (a.beat - b.beat) <= 1.0e-9 && a.pitch < b.pitch); });
    }

    bool isDrop (Role r) { return r == Role::Drop || r == Role::Drop2; }
}

// ======================================================================================
Palette makePalette (const MusicalDNA& dna, Flavour flavour, float creativity, juce::uint32 seed)
{
    Palette p;
    Rng r ((juce::int64) seed * 2654435761u + 11);
    p.keyRoot = dna.keyRoot; p.minor = dna.minor;
    p.style = dna.style; p.bpm = dna.bpm;
    p.swing = dna.swing;
    static const int majSteps[7] = { 0, 2, 4, 5, 7, 9, 11 }, minSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    for (int d = 0; d < 7; ++d) p.scale[(size_t) ((p.keyRoot + (p.minor ? minSteps[d] : majSteps[d])) % 12)] = true;
    p.bassOctaveBase = (p.style == Style::Trap || p.style == Style::DnB || p.style == Style::HipHop) ? 28 : 31;

    // Harmony: the user's loop when we heard one, otherwise a loop that suits the key.
    if (dna.hasChords && ! dna.chordRoots.empty())
    {
        p.prog = dna.chordRoots; p.progMinor = dna.chordMinor;
        while (p.prog.size() < 2) { p.prog.push_back (p.prog.front()); p.progMinor.push_back (p.progMinor.front()); }
    }
    else if (dna.hasBass && ! dna.bassLine.empty())
    {
        // Infer one chord per bar from the bass notes.
        int roots[7]; bool qual[7];
        MusicAnalyzer::diatonicTriads (p.keyRoot, p.minor, roots, qual);
        for (int bar = 0; bar < std::max (1, dna.bassBars); ++bar)
        {
            std::array<float, 12> w {};
            for (auto& n : dna.bassLine) if ((int) (n.beat / 4.0) == bar) w[(size_t) (n.pitch % 12)] += (float) n.length;
            const int pc = (int) (std::max_element (w.begin(), w.end()) - w.begin());
            int d = 0; for (int k = 0; k < 7; ++k) if (roots[k] == pc) d = k;
            p.prog.push_back (roots[d]); p.progMinor.push_back (qual[d]);
        }
        if (p.prog.size() == 1) { Rng r2 ((juce::int64) seed + 5); Palette q = p; defaultProgression (q, r2); q.prog[0] = p.prog[0]; q.progMinor[0] = p.progMinor[0]; p.prog = q.prog; p.progMinor = q.progMinor; }
    }
    else
        defaultProgression (p, r);
    alternativeProgression (p, r, creativity);

    // Groove: the user's drums when we heard them.
    defaultDrums (p);
    if (dna.hasDrums)
    {
        p.drumsFromUser = true;
        const bool clapStyle = isFourOnFloorStyle (p.style);
        for (int s = 0; s < 16; ++s)
        {
            p.kick[(size_t) s] = dna.kick[(size_t) s] >= 0.5f ? 0.75f + 0.25f * dna.kick[(size_t) s] : 0.0f;
            const float sn = dna.snare[(size_t) s] >= 0.55f ? 0.7f + 0.3f * dna.snare[(size_t) s] : 0.0f;
            if (clapStyle) { p.clap[(size_t) s] = sn; p.snare[(size_t) s] = 0.0f; } else { p.snare[(size_t) s] = sn; p.clap[(size_t) s] = sn * 0.5f; }
            const float h = dna.hats[(size_t) s];
            const bool offbeat8 = s % 4 == 2;
            p.ohh[(size_t) s] = (clapStyle && offbeat8 && h >= 0.6f) ? 0.75f : 0.0f;
            p.chh[(size_t) s] = (h >= 0.4f && p.ohh[(size_t) s] <= 0.0f) ? 0.3f + 0.4f * h : 0.0f;
        }
        int kicks = 0; for (auto v : p.kick) kicks += v > 0;
        if (kicks == 0) { Palette d = p; defaultDrums (d); p.kick = d.kick; }
    }
    p.fourOnFloor = p.kick[0] > 0 && p.kick[4] > 0 && p.kick[8] > 0 && p.kick[12] > 0;

    // Bass line and motif: the user's when we heard them.
    if (dna.hasBass && ! dna.bassLine.empty() && flavour != Flavour::Atmospheric)
    {
        p.bass = dna.bassLine; p.bassBars = std::max (1, dna.bassBars); p.bassFromUser = true;
    }
    if (dna.hasMelody && ! dna.melody.empty())
    {
        p.motif = dna.melody; p.motifBars = std::max (1, dna.melodyBars); p.motifFromUser = true;
        if (flavour == Flavour::Intense && creativity > 0.5f) p.motif = varyMotif (p.motif, p, r, creativity * 0.4f);
    }
    else
    {
        p.motif = generateMotif (p, r, creativity);
        p.motifBars = 2;
    }
    return p;
}

std::vector<std::pair<Role, int>> defaultPlan (Flavour flavour, bool referenceDrop, int dropBars)
{
    const int d = referenceDrop ? dropBars : 16;
    switch (flavour)
    {
        case Flavour::Atmospheric:
            return { { Role::Intro, 16 }, { Role::Build, 8 }, { Role::Drop, d }, { Role::Break, 16 }, { Role::Bridge, 8 }, { Role::Build, 8 }, { Role::Drop2, 16 }, { Role::Outro, 16 } };
        case Flavour::Intense:
            return { { Role::Intro, 8 }, { Role::Build, 8 }, { Role::Drop, d }, { Role::Break, 8 }, { Role::Build, 8 }, { Role::Drop2, 16 }, { Role::Outro, 8 } };
        case Flavour::Faithful:
        default:
            return { { Role::Intro, 16 }, { Role::Build, 8 }, { Role::Drop, d }, { Role::Break, 16 }, { Role::Build, 8 }, { Role::Drop2, 16 }, { Role::Outro, 16 } };
    }
}

static void nameSections (Arrangement& arr)
{
    std::array<int, numRoles> count {};
    for (auto& s : arr.sections) count[(size_t) s.role]++;
    std::array<int, numRoles> seen {};
    for (auto& s : arr.sections)
    {
        const int k = ++seen[(size_t) s.role];
        s.name = roleName (s.role);
        if (count[(size_t) s.role] > 1 && s.role != Role::Drop2) s.name << " " << k;
    }
}

void refreshTransitions (Arrangement& arr)
{
    arr.layout();
    nameSections (arr);
    for (size_t i = 0; i < arr.sections.size(); ++i)
    {
        auto& s = arr.sections[i];
        auto& fx = s.clips[(size_t) Lane::Fx];
        fx = {};
        const bool nextDrop = i + 1 < arr.sections.size() && isDrop (arr.sections[i + 1].role);
        const bool nextBreak = i + 1 < arr.sections.size() && (arr.sections[i + 1].role == Role::Break || arr.sections[i + 1].role == Role::Bridge);
        const double L = s.lengthBeats();
        if (s.role == Role::Build)
            fx.fx.push_back ({ FxKind::Riser, 0.0, L, 0.75f });
        else if (nextDrop)
            fx.fx.push_back ({ FxKind::Sweep, std::max (0.0, L - 8.0), std::min (8.0, L), 0.6f });
        if (nextDrop || nextBreak)
            fx.fx.push_back ({ FxKind::ReverseCymbal, L - 4.0, 4.0, nextDrop ? 0.8f : 0.55f });
        if (isDrop (s.role))
            fx.fx.push_back ({ FxKind::Impact, 0.0, 8.0, 0.9f });
        if ((s.role == Role::Break || s.role == Role::Bridge) && i > 0 && isDrop (arr.sections[i - 1].role))
            fx.fx.push_back ({ FxKind::Downlifter, 0.0, 8.0, 0.7f });
        if (s.role == Role::Outro)
            fx.fx.push_back ({ FxKind::Downlifter, L - 8.0, 8.0, 0.5f });
        if (s.role == Role::Intro && ! nextDrop && i + 1 < arr.sections.size())
            fx.fx.push_back ({ FxKind::Sweep, std::max (0.0, L - 8.0), std::min (8.0, L), 0.45f });
    }
}

void buildSection (Arrangement& arr, int index, const MusicalDNA& dna, juce::uint32 seed)
{
    if (index < 0 || index >= (int) arr.sections.size()) return;
    const auto palette = makePalette (dna, arr.flavour, arr.creativity, arr.paletteSeed);
    auto& s = arr.sections[(size_t) index];
    s.seed = seed;
    const bool beforeDrop = index + 1 < (int) arr.sections.size() && isDrop (arr.sections[(size_t) index + 1].role);
    const bool afterDrop = index > 0 && isDrop (arr.sections[(size_t) index - 1].role);
    buildSectionImpl (s, palette, dna, arr.flavour, arr.creativity, index, beforeDrop, afterDrop);
    refreshTransitions (arr);
    ++arr.version;
}

Arrangement generate (const MusicalDNA& dna, Flavour flavour, float creativity, juce::uint32 seed)
{
    Arrangement arr;
    arr.flavour = flavour;
    arr.creativity = juce::jlimit (0.0f, 1.0f, creativity);
    arr.bpm = dna.bpm;
    arr.paletteSeed = seed;
    // The user's drop stays theirs when we heard drums + bass (a real drop); a lone melody gets a generated drop.
    const bool reference = dna.hasDrums && (dna.hasBass || dna.hasChords);
    int dropBars = 16;
    if (reference)
        dropBars = dna.barsAnalysed >= 12 ? 16 : 8;
    const auto plan = defaultPlan (flavour, reference, dropBars);
    juce::uint32 k = seed;
    for (auto& [role, bars] : plan)
    {
        GenSection s;
        s.role = role; s.bars = bars;
        s.isReference = reference && role == Role::Drop;
        s.seed = (k = k * 1664525u + 1013904223u);
        arr.sections.push_back (s);
    }
    arr.layout();
    const auto palette = makePalette (dna, flavour, arr.creativity, seed);
    for (size_t i = 0; i < arr.sections.size(); ++i)
    {
        const bool beforeDrop = i + 1 < arr.sections.size() && isDrop (arr.sections[i + 1].role);
        const bool afterDrop = i > 0 && isDrop (arr.sections[i - 1].role);
        buildSectionImpl (arr.sections[i], palette, dna, flavour, arr.creativity, (int) i, beforeDrop, afterDrop);
    }
    refreshTransitions (arr);
    arr.summary = flavourSummary (flavour, dna);
    ++arr.version;
    return arr;
}

int insertSection (Arrangement& arr, int position, Role role, int bars, const MusicalDNA& dna, SectionMood mood, juce::uint32 seed)
{
    position = juce::jlimit (0, (int) arr.sections.size(), position);
    GenSection s;
    s.role = role; s.bars = bars; s.mood = mood; s.seed = seed;
    arr.sections.insert (arr.sections.begin() + position, s);
    buildSection (arr, position, dna, seed);
    return position;
}

juce::String flavourSummary (Flavour flavour, const MusicalDNA& dna)
{
    const juce::String base = dna.hasMelody ? utf8 ("ton motif") : utf8 ("un motif écrit dans ta tonalité");
    switch (flavour)
    {
        case Flavour::Atmospheric:
            return utf8 ("ATMO : plus d'espace. Intro sans kick, long break avec nappes et arpèges, pont harmonique, ") + base + utf8 (" ralenti.");
        case Flavour::Intense:
            return utf8 ("INTENSE : plus d'énergie. Sections courtes, roulements de caisse claire, stabs, drop 2 une octave plus haut.");
        case Flavour::Faithful:
        default:
            return utf8 ("FIDÈLE : au plus près de ton morceau. Tes patterns, ta basse et ") + base + utf8 (" construisent chaque partie.");
    }
}

} // namespace ArrangementGenerator
