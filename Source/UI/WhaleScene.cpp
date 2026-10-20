#include "WhaleScene.h"

using namespace Theme;

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    inline float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
    inline float approach (float cur, float target, float rate, float dt) { return cur + (target - cur) * (1.0f - std::exp (-rate * dt)); }
    inline float profileHz (int b) { static const float hz[29] = { 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 }; return hz[b]; }
}

WhaleScene::WhaleScene()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);
}

void WhaleScene::resized()
{
    const float w = (float) getWidth();
    L = w * 0.52f;
    H = w * 0.057f;
    cx = w * 0.49f;
    cy = (float) getHeight() * 0.72f;
    buildBackground();
    plankton.clear();
    for (int i = 0; i < 90; ++i)
        plankton.push_back ({ { rng.nextFloat() * w, surfaceY() + rng.nextFloat() * ((float) getHeight() - surfaceY()) },
                              { (rng.nextFloat() - 0.5f) * 4.0f, -2.0f - rng.nextFloat() * 6.0f }, 1.0f, 0.6f + rng.nextFloat() * 1.2f,
                              juce::Colour (0xffaaa0ff).withAlpha (0.12f + 0.35f * rng.nextFloat()) });
}

void WhaleScene::buildBackground()
{
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    background = juce::Image (juce::Image::RGB, w, h, true);
    juce::Graphics g (background);
    const float sy = surfaceY();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff040309), 0, 0, juce::Colour (0xff0c0720), 0, sy, false));
    g.fillRect (0.0f, 0.0f, (float) w, sy);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a0d38), 0, sy, juce::Colour (0xff070411), 0, (float) h, false));
    g.fillRect (0.0f, sy, (float) w, (float) h - sy);
    juce::Random r (11);
    for (int i = 0; i < 16; ++i)   // soft nebula blobs
    {
        const float x = r.nextFloat() * (float) w, y = r.nextFloat() * (float) h, rad = (0.08f + 0.18f * r.nextFloat()) * (float) w;
        const juce::Colour c = (i % 3 == 0 ? juce::Colour (0xff5a1e96) : i % 3 == 1 ? juce::Colour (0xff2a1a70) : juce::Colour (0xff1a2a78)).withAlpha (0.10f);
        g.setGradientFill (juce::ColourGradient (c, x, y, c.withAlpha (0.0f), x + rad, y, true));
        g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
    }
    for (int i = 0; i < 260; ++i)   // stars above the surface
    {
        const float x = r.nextFloat() * (float) w, y = r.nextFloat() * (sy - 6.0f), s = r.nextFloat() < 0.8f ? 1.0f : 1.8f;
        g.setColour (juce::Colour (0xffece6ff).withAlpha (0.25f + 0.75f * r.nextFloat()));
        g.fillEllipse (x, y, s, s);
    }
}

float WhaleScene::profile (float s)
{
    if (s < 0.3f) { const float x = s / 0.3f; return std::sqrt (juce::jmax (0.0f, 1.0f - (1.0f - x) * (1.0f - x))); }
    if (s < 0.9f) return 0.07f + 0.93f * std::pow (std::cos (pi * 0.5f * (s - 0.3f) / 0.6f), 1.1f);
    return 0.07f;
}

WhaleScene::Frame WhaleScene::frame (float s) const
{
    const float amp = H * (0.17f + 0.35f * kickSm);
    auto local = [&] (float u) { return juce::Point<float> (L * (0.5f - u), amp * std::sin (2.0f * pi * 0.85f * u + swim) * std::pow (u, 1.6f)); };
    const auto a = local (juce::jmax (0.0f, s - 0.002f)), b = local (juce::jmin (1.0f, s + 0.002f)), p = local (s);
    float tx = b.x - a.x, ty = b.y - a.y;
    const float n = std::sqrt (tx * tx + ty * ty) + 1.0e-6f;
    tx /= n; ty /= n;
    float nx = -ty, ny = tx;
    if (ny < 0) { nx = -nx; ny = -ny; }
    return { p.x, p.y, tx, ty, nx, ny };
}

juce::Point<float> WhaleScene::bodyPoint (float s, float u, float wob, int k) const
{
    const auto f = frame (s);
    const float pr = profile (s);
    const float h = H * pr * (u < 0 ? 0.74f : 1.15f);
    const float off = u * h + wob * H * 0.07f * std::sin (s * (9.0f + (float) (k % 5)) * pi + (float) k * 0.7f + t * 2.2f) * pr;
    const float x = f.x + f.nx * off, y = f.y + f.ny * off;
    const float c = std::cos (tilt), sn = std::sin (tilt);
    return { cx + x * c - y * sn, cy + x * sn + y * c };
}

void WhaleScene::advance (const Input& input, float dt)
{
    in = input;
    t += dt;
    const bool problem = in.mood == Mood::tooLoud || in.mood == Mood::clipping || in.mood == Mood::phase;
    const bool calm = in.mood == Mood::validated || in.mood == Mood::ok;

    kickSm = approach (kickSm, in.kick, 18.0f, dt);
    // 1.3 living whale: turquoise once validated, a ring + bubbles on each kick
    teal = approach (teal, in.mood == Mood::validated ? 1.0f : 0.0f, 1.5f, dt);
    kickFast = approach (kickFast, in.kick, 30.0f, dt);
    kickCool -= dt;
    if (! in.reduced && in.mood != Mood::idle && in.kick > 0.5f && kickPrev <= 0.5f && kickCool <= 0.0f)
    {
        kickCool = 0.2f;
        if (rings.size() < 4) rings.push_back ({ 0.0f });
        for (int i = 0; i < 2 && bubbles.size() < 40; ++i)
        {
            const auto p0 = bodyPoint (0.03f + 0.12f * rng.nextFloat(), -0.7f + 0.9f * rng.nextFloat(), 0, 0);
            bubbles.push_back ({ p0, -(18.0f + 30.0f * rng.nextFloat()), 1.5f + 2.5f * rng.nextFloat(), 1.0f, rng.nextFloat() * 6.28f });
        }
    }
    kickPrev = in.kick;
    for (auto& r : rings) r.age += dt;
    rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.age > 0.9f; }), rings.end());
    for (auto& b : bubbles)
    {
        b.p.y += b.vy * dt;
        b.p.x += std::sin (b.phase + t * 3.0f) * 9.0f * dt;
        b.life -= dt * 0.5f;
        if (b.p.y < surfaceY() + 4.0f) b.life = 0.0f;
    }
    bubbles.erase (std::remove_if (bubbles.begin(), bubbles.end(), [] (const Bubble& b) { return b.life <= 0.0f; }), bubbles.end());

    // sea floor: the curves glide towards the latest measurement
    if (in.hasSpectrum)
    {
        for (int b = 0; b < 29; ++b) seaShown[b] = seaInit ? approach (seaShown[b], in.spectrum[b], 2.5f, dt) : in.spectrum[b];
        seaInit = true;
    }
    else seaInit = false;
    for (int b = 0; b < 29; ++b)
    {
        refShown[b]   = refInit ? approach (refShown[b], in.refMed[b], 4.0f, dt) : in.refMed[b];
        refLoShown[b] = refInit ? approach (refLoShown[b], in.refLo[b], 4.0f, dt) : in.refLo[b];
        refHiShown[b] = refInit ? approach (refHiShown[b], in.refHi[b], 4.0f, dt) : in.refHi[b];
    }
    refInit = true;
    widthSm = approach (widthSm, clamp01 (in.width * 6.0f), 3.0f, dt);
    for (int b = 0; b < 8; ++b)
    {
        bandAvg[b] = approach (bandAvg[b], in.bands[b], 0.5f, dt);
        bandE[b] = approach (bandE[b], clamp01 ((in.bands[b] - bandAvg[b] + 6.0f) / 12.0f) * (in.bands[b] > -80.0f ? 1.0f : 0.0f), 12.0f, dt);
    }
    swim += dt * (1.1f + 1.4f * kickSm);

    // depth from the loudness error: on target = just under the surface, too loud = breaking through
    const float sy = surfaceY();
    float targetY;
    if (in.mood == Mood::idle) targetY = (float) getHeight() * 0.70f;
    else targetY = sy + H * 1.25f - juce::jlimit (-10.0f, 3.0f, in.loudErr) * H * 0.38f;
    targetY = juce::jlimit (sy - H * 0.2f, (float) getHeight() - H * 1.4f, targetY);
    if (in.mood == Mood::validated) targetY = juce::jmin (targetY, sy + H * 0.95f);
    const float oldCy = cy;
    cy = approach (cy, targetY, 1.2f, dt);
    vy = approach (vy, (cy - oldCy) / juce::jmax (1.0e-3f, dt), 4.0f, dt);
    tilt = approach (tilt, juce::jlimit (-0.22f, 0.12f, vy * 0.004f) + 0.03f * std::sin (t * 0.4f), 3.0f, dt);
    cx = (float) getWidth() * 0.49f + (float) getWidth() * 0.015f * std::sin (t * 0.13f);

    const float chaosTarget = problem ? 1.0f : in.mood == Mood::working ? 0.15f + 0.6f * (1.0f - in.autoProgress) : calm ? 0.06f : in.mood == Mood::tooSoft ? 0.4f : 0.3f;
    chaos = approach (chaos, chaosTarget, 1.5f, dt);
    glow = approach (glow, in.mood == Mood::idle ? 0.35f : in.mood == Mood::validated ? 1.0f : 0.8f, 2.0f, dt);
    hot = approach (hot, (in.mood == Mood::tooLoud || in.mood == Mood::clipping) ? 1.0f : 0.0f, 3.0f, dt);

    if (in.souffleCount != lastSouffle)
    {
        lastSouffle = in.souffleCount;
        souffleT = 0.0f;
    }
    if (souffleT >= 0.0f)
    {
        souffleT += dt;
        if (souffleT < 1.6f)   // geyser of stars
            for (int i = 0; i < (in.reduced ? 3 : 8); ++i)
            {
                const float spread = (rng.nextFloat() - 0.5f) * 1.6f;
                particles.push_back ({ blowhole, { spread * H * 3.0f, -(4.5f + 3.5f * rng.nextFloat()) * H },
                                       1.0f, 0.6f + rng.nextFloat() * 1.4f,
                                       rng.nextFloat() < 0.6f ? cyan.interpolatedWith (juce::Colours::white, rng.nextFloat() * 0.6f) : magenta.interpolatedWith (juce::Colours::white, 0.4f) });
            }
        if (souffleT > 4.5f) souffleT = -1.0f;
    }
    for (auto& p : particles)
    {
        p.v.y += H * 6.0f * dt;   // gravity
        p.p += p.v * dt;
        p.life -= dt * 0.55f;
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }), particles.end());

    for (auto& p : plankton)
    {
        p.p += p.v * dt;
        if (p.p.y < sy + 4.0f) p.p.y = (float) getHeight() + 2.0f;
    }

    // sparks when the master is too hot
    if (hot > 0.3f && ! in.reduced)
        for (int i = 0; i < 3; ++i)
        {
            const float s = 0.1f + 0.6f * rng.nextFloat();
            const auto base = bodyPoint (s, -1.0f, 0, 0);
            const float a = rng.nextFloat() * 2.0f * pi, len = 3.0f + rng.nextFloat() * 10.0f;
            const juce::Point<float> p0 (base.x + (rng.nextFloat() - 0.5f) * 30.0f, base.y - rng.nextFloat() * H * 0.8f);
            sparks.push_back ({ p0, p0 + juce::Point<float> (std::cos (a), std::sin (a)) * len, 1.0f });
        }
    for (auto& s : sparks) s.life -= dt * 2.5f;
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());
}

void WhaleScene::drawSurface (juce::Graphics& g)
{
    const float sy = surfaceY(), w = (float) getWidth();
    juce::Colour c = violet;
    if (hot > 0.05f) c = c.interpolatedWith (orange, hot);
    if (in.mood == Mood::validated) c = cyan;
    const float rip = souffleT >= 0 ? juce::jmax (0.0f, 1.0f - souffleT / 4.0f) : 0.0f;
    for (float x = 0; x < w; x += 4.0f)
    {
        float y = sy + 2.0f * std::sin (x * 0.012f + t * 0.8f) + 1.2f * std::sin (x * 0.031f - t * 1.3f);
        if (rip > 0)
        {
            const float d = std::abs (x - blowhole.x);
            y += 10.0f * rip * std::exp (-d / (w * 0.12f)) * std::sin (d * 0.07f - souffleT * 9.0f);
        }
        float a = 0.45f + 0.35f * std::sin (x * 0.7f);
        if (hot > 0.05f) a *= 1.0f + hot * std::exp (-std::abs (x - cx) / (w * 0.2f));
        g.setColour (c.withAlpha (juce::jlimit (0.1f, 1.0f, a)));
        g.fillEllipse (x - 1.1f, y - 1.1f, 2.2f, 2.2f);
    }
    g.setColour (dim.withAlpha (0.8f));
    g.setFont (mono (11.0f));
    g.drawText (in.targetText, 14, (int) sy - 20, 300, 14, juce::Justification::centredLeft);
}

void WhaleScene::drawTrails (juce::Graphics& g, juce::Colour c)
{
    const int n = in.reduced ? 6 : 12;
    const float len = (float) getWidth() * 0.4f;
    const auto f = frame (0.97f);
    const float cs = std::cos (tilt), sn = std::sin (tilt);
    const float dx = f.tx * cs - f.ty * sn, dy = f.tx * sn + f.ty * cs;
    const float px = -dy, py = dx;
    juce::Point<float> starts[4] = { flukeTips[0], flukeTips[1], tail, finTip };
    for (int j = 0; j < n; ++j)
    {
        const auto s0 = starts[j % 4];
        const float spread = ((float) j / (float) (n - 1) - 0.5f) * (20.0f + 70.0f * chaos + 60.0f * widthSm);
        const float ph = (float) j * 1.7f, fr = 0.004f + 0.012f * chaos * (float) ((j * 37) % 10) / 10.0f;
        constexpr int chunks = 6;
        for (int ch = 0; ch < chunks; ++ch)
        {
            juce::Path p;
            for (int i = 0; i <= 10; ++i)
            {
                const float d = len * ((float) ch + (float) i / 10.0f) / (float) chunks;
                const float r = d / len;
                const float wob = std::sin (d * fr + ph + t * (1.5f + 2.0f * chaos)) * (4.0f + 40.0f * chaos) * r;
                const float off = spread * r + wob;
                const juce::Point<float> q (s0.x + dx * d + px * off, s0.y + dy * d + py * off);
                if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
            }
            const float a = std::pow (1.0f - (float) ch / (float) chunks, 1.5f) * (j % 3 == 0 ? 0.7f : 0.45f) * glow;
            g.setColour (c.withAlpha (a));
            g.strokePath (p, juce::PathStrokeType (1.1f));
        }
    }
}

void WhaleScene::drawWhale (juce::Graphics& g, juce::Colour ca, juce::Colour cb, float alpha, juce::Point<float> offset, bool ghost)
{
    const int lines = in.reduced ? 14 : 26, segs = in.reduced ? 36 : 64;
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::translation (offset));
    const float glowAmt = (in.reduced || ghost) ? 0.0f : glow * (0.8f + 0.8f * kickFast);

    for (int k = 0; k <= lines; ++k)
    {
        const float u = -1.0f + 2.0f * (float) k / (float) lines;
        const int band = juce::jlimit (0, 7, (int) std::round ((1.0f - (u + 1.0f) * 0.5f) * 7.0f));   // top = highs
        const float e = bandE[band];
        juce::Path p;
        for (int i = 0; i <= segs; ++i)
        {
            const auto q = bodyPoint (0.002f + 0.993f * (float) i / (float) segs, u * 0.98f, (0.4f + chaos) * e, k);
            if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        const juce::Colour c = ca.interpolatedWith (cb, (1.0f - u) * 0.5f).withAlpha (alpha * (0.42f + 0.58f * e) * (0.6f + 0.4f * (1.0f - std::abs (u))));
        if (std::abs (u) > 0.97f) neon (g, p, c.withAlpha (alpha * 0.9f), 1.6f, glowAmt);
        else
        {
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
    }
    // eye
    const auto eye = bodyPoint (0.12f, 0.05f, 0, 0);
    g.setColour (juce::Colours::white.withAlpha (alpha));
    g.fillEllipse (eye.x - 3.0f, eye.y - 3.0f, 6.0f, 6.0f);
    blowhole = bodyPoint (0.09f, -1.0f, 0, 0);

    // pectoral fin (long humpback fin, pointing back and down)
    {
        const auto base = bodyPoint (0.27f, 0.86f, 0, 0);
        const auto f = frame (0.27f);
        float ang = std::atan2 (f.ty, f.tx) - 0.42f;
        ang += tilt;
        const float dx = std::cos (ang), dy = std::sin (ang), qx = -dy, qy = dx, fl = L * 0.27f;
        for (int j = 0; j < 9; ++j)
        {
            const float w = ((float) j / 8.0f - 0.5f) * 2.0f;
            juce::Path p;
            for (int i = 0; i <= 20; ++i)
            {
                const float r = (float) i / 20.0f, bulge = std::sin (pi * std::pow (r, 0.8f)) * fl * 0.07f * w;
                const juce::Point<float> q (base.x + dx * fl * r + qx * bulge, base.y + dy * fl * r + qy * bulge);
                if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
            }
            g.setColour (ca.interpolatedWith (cb, 0.3f + 0.05f * (float) j).withAlpha (alpha * (0.45f + 0.4f * bandE[1])));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        finTip = { base.x + dx * fl, base.y + dy * fl };
    }
    // flukes
    {
        const auto f = frame (0.995f);
        const float c = std::cos (tilt), sn = std::sin (tilt);
        auto tr = [&] (float x, float y) { return juce::Point<float> (cx + x * c - y * sn, cy + x * sn + y * c); };
        tail = tr (f.x, f.y);
        for (int side = 0; side < 2; ++side)
        {
            const float sg = side == 0 ? -1.0f : 1.0f;
            const float beat = 1.0f + 0.25f * kickSm;
            const auto tip = tr (f.x + f.tx * L * 0.07f + f.nx * sg * L * 0.15f * beat, f.y + f.ty * L * 0.07f + f.ny * sg * L * 0.15f * beat);
            flukeTips[side] = tip;
            const float vx = tip.x - tail.x, vy2 = tip.y - tail.y, ln = std::sqrt (vx * vx + vy2 * vy2) + 1.0e-6f;
            const float qx = -vy2 / ln, qy = vx / ln;
            for (int j = 0; j < 9; ++j)
            {
                const float w = ((float) j / 8.0f - 0.5f) * 2.0f;
                juce::Path p;
                for (int i = 0; i <= 20; ++i)
                {
                    const float r = (float) i / 20.0f, b = std::sin (pi * r) * ln * 0.18f * w + ln * 0.12f * r * r;
                    const juce::Point<float> q (tail.x + vx * r + qx * b, tail.y + vy2 * r + qy * b);
                    if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
                }
                g.setColour (ca.interpolatedWith (cb, 0.15f + 0.07f * (float) j).withAlpha (alpha * (0.45f + 0.5f * bandE[0])));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
        }
    }
}

void WhaleScene::drawSouffle (juce::Graphics& g)
{
    for (auto& p : particles)
    {
        g.setColour (p.c.withAlpha (clamp01 (p.life)));
        g.fillEllipse (p.p.x - p.size, p.p.y - p.size, p.size * 2.0f, p.size * 2.0f);
    }
    if (souffleT < 0) return;
    const float sy = surfaceY(), w = (float) getWidth();
    // ripples
    for (int k = 0; k < 5; ++k)
    {
        const float r = (souffleT * 0.6f + (float) k * 0.12f) * w * 0.25f;
        const float a = clamp01 (1.0f - souffleT / 3.5f) * (1.0f - (float) k * 0.15f);
        if (a <= 0) continue;
        g.setColour (cyan.withAlpha (a * 0.8f));
        g.drawEllipse (blowhole.x - r, sy - r * 0.11f, r * 2.0f, r * 0.22f, 1.2f);
    }
    // the Cetus constellation lights up for a moment
    const float ca = clamp01 (souffleT < 1.0f ? souffleT : (4.0f - souffleT) / 2.0f);
    if (ca > 0)
    {
        const float ox = w * 0.30f, oy = sy * 0.30f, s = w * 0.11f;
        const juce::Point<float> pts[] = { { 0, 0.1f }, { 0.45f, 0.4f }, { 0.85f, 0.15f }, { 1.3f, 0.6f }, { 1.15f, 1.15f }, { 0.65f, 1.25f }, { 0.25f, 0.9f }, { 0.45f, 0.4f } };
        juce::Path p;
        for (int i = 0; i < 8; ++i)
        {
            const juce::Point<float> q (ox + pts[i].x * s, oy + pts[i].y * s * 0.55f);
            if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        neon (g, p, cyan.withAlpha (0.7f * ca), 1.2f, ca);
        g.setColour (juce::Colours::white.withAlpha (ca));
        for (auto& q : pts) g.fillEllipse (ox + q.x * s - 3.0f, oy + q.y * s * 0.55f - 3.0f, 6.0f, 6.0f);
    }
}

void WhaleScene::drawHud (juce::Graphics& g)
{
    const float w = (float) getWidth();
    // loudness readout
    g.setColour (text);
    g.setFont (mono (40.0f));
    g.drawText (in.lufsText, juce::Rectangle<float> (w - 330, 14, 300, 46), juce::Justification::centredRight);
    g.setColour (dim);
    g.setFont (mono (10.5f, 0.06f));
    g.drawText (fr ("SHORT-TERM LUFS (3 s)  ·  ") + in.targetText, juce::Rectangle<float> (w - 430, 60, 400, 14), juce::Justification::centredRight);
    // verdict badge
    {
        g.setFont (bold (13.5f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), in.verdictText) + 36.0f;
        const juce::Rectangle<float> r (w - 30 - tw, 80, tw, 26);
        g.setColour (in.verdictColour.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (in.verdictColour);
        g.drawRoundedRectangle (r, 5.0f, 1.2f);
        g.fillEllipse (r.getX() + 11, r.getCentreY() - 3.5f, 7, 7);
        g.drawText (in.verdictText, r.withTrimmedLeft (24), juce::Justification::centredLeft);
    }
    // caption + AUTO progress
    if (in.caption.isNotEmpty())
    {
        g.setColour (juce::Colour (0xffb2a6d6));
        g.setFont (body (13.0f));
        g.drawText (in.caption, 16, 18, (int) (w * 0.55f), 18, juce::Justification::centredLeft);
    }
    if (in.autoRunning)
    {
        const juce::Rectangle<float> bar (16, 42, 260, 4);
        g.setColour (line);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (magenta);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * clamp01 (in.autoProgress)), 2.0f);
    }
    // vertical loudness meter
    {
        const float sy = surfaceY();
        const juce::Rectangle<float> m (w - 14, sy + 16, 5, (float) getHeight() - sy - 30);
        g.setColour (line);
        g.fillRoundedRectangle (m, 2.5f);
        const float h = m.getHeight() * clamp01 (in.meter);
        g.setColour (in.mood == Mood::validated ? green : hot > 0.3f ? orange : violet);
        g.fillRoundedRectangle (m.withTop (m.getBottom() - h), 2.5f);
    }
}

void WhaleScene::drawPulse (juce::Graphics& g, juce::Colour c)
{
    if (in.mood == Mood::idle) return;
    const auto centre = bodyPoint (0.42f, 0.0f, 0, 0);
    const float rx = L * 0.42f, ry = H * 1.25f;
    const float a = (0.07f + 0.20f * kickFast) * glow;
    g.setGradientFill (juce::ColourGradient (c.withAlpha (a), centre.x, centre.y, c.withAlpha (0.0f), centre.x + rx, centre.y, true));
    g.fillEllipse (centre.x - rx, centre.y - ry, rx * 2.0f, ry * 2.0f);
    for (auto& r : rings)
    {
        const float k = 0.75f + 0.75f * r.age / 0.9f, al = (1.0f - r.age / 0.9f) * 0.45f;
        g.setColour (c.withAlpha (al));
        g.drawEllipse (centre.x - rx * k, centre.y - ry * 0.75f * k, rx * 2.0f * k, ry * 1.5f * k, 1.3f);
    }
}

void WhaleScene::drawSeabed (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float y1 = h - 10.0f, y0 = h - 84.0f, x0 = 16.0f, x1 = w - 30.0f;
    const float l0 = std::log10 (22.0f), l1 = std::log10 (18000.0f);
    auto X = [&] (float f) { return x0 + (std::log10 (f) - l0) / (l1 - l0) * (x1 - x0); };
    float lo = 1.0e9f, hi = -1.0e9f;
    for (int b = 2; b < 29; ++b) { lo = juce::jmin (lo, refLoShown[b]); hi = juce::jmax (hi, refHiShown[b]); }
    lo -= 3.0f; hi += 3.0f;
    auto Y = [&] (float v) { return juce::jlimit (y0 - 8.0f, y1, y1 - (v - lo) / juce::jmax (1.0f, hi - lo) * (y1 - y0)); };

    // darker water near the floor
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x00050310), 0, y0 - 24.0f, juce::Colour (0xaa050310), 0, h, false));
    g.fillRect (0.0f, y0 - 24.0f, w, h - y0 + 24.0f);

    auto curve = [&] (const float* v, float shift)
    {
        juce::Path p;
        for (int b = 0; b < 29; ++b)
        {
            const juce::Point<float> q (X (profileHz (b)), Y (v[b] + shift));
            if (b == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        return p.createPathWithRoundedCorners (40.0f);
    };
    // references: inter-quartile band + dashed median
    {
        juce::Path band;
        for (int b = 0; b < 29; ++b) { const juce::Point<float> q (X (profileHz (b)), Y (refHiShown[b])); if (b == 0) band.startNewSubPath (q); else band.lineTo (q); }
        for (int b = 28; b >= 0; --b) band.lineTo (X (profileHz (b)), Y (refLoShown[b]));
        band.closeSubPath();
        g.setColour (violet.withAlpha (0.16f));
        g.fillPath (band.createPathWithRoundedCorners (30.0f));
        juce::Path dashed;
        const float dl[] = { 5.0f, 5.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, curve (refShown, 0.0f), dl, 2);
        g.setColour (juce::Colour (0xffb49cff).withAlpha (0.85f));
        g.fillPath (dashed);
    }
    g.setFont (mono (10.0f, 0.06f));
    if (! seaInit)
    {
        g.setColour (dim.withAlpha (0.8f));
        g.drawText (fr ("Play your track: its spectrum appears here, over the references of the style"), juce::Rectangle<float> (x0, y0 + 18, x1 - x0, 14), juce::Justification::centred);
    }
    else
    {
        // what is heard, levelled like the references (200 Hz - 5 kHz)
        float am = 0, ap = 0;
        for (int b = 9; b <= 23; ++b) { am += seaShown[b]; ap += refShown[b]; }
        const float shift = (ap - am) / 15.0f;
        const auto mix = curve (seaShown, shift);
        juce::Path fill (mix);
        fill.lineTo (x1, y1); fill.lineTo (x0, y1); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (green.withAlpha (0.26f), 0, y0, green.withAlpha (0.03f), 0, y1, false));
        g.fillPath (fill);
        neon (g, mix, juce::Colour (0xffbffff0), 1.5f, 0.7f);
        // the two clearest gaps, as arrows towards the references
        const int rl[] = { 1, 5, 9, 14, 19, 24, 27 }, rh[] = { 4, 8, 13, 18, 23, 26, 28 };
        const char* names[] = { "sub", "bass", "low-mids", "mids", "presence", "highs", "air" };
        float ex[7] {};
        for (int r = 0; r < 7; ++r)
        {
            float dev = 0, tol = 0;
            for (int b = rl[r]; b <= rh[r]; ++b) { dev += seaShown[b] + shift - refShown[b]; tol += juce::jlimit (1.0f, 3.0f, 0.5f * (refHiShown[b] - refLoShown[b])); }
            const int n = rh[r] - rl[r] + 1; dev /= (float) n; tol /= (float) n;
            ex[r] = std::abs (dev) > tol ? dev : 0.0f;
        }
        for (int k = 0; k < 2; ++k)
        {
            int best = -1;
            for (int r = 0; r < 7; ++r) if (std::abs (ex[r]) >= 1.5f && (best < 0 || std::abs (ex[r]) > std::abs (ex[best]))) best = r;
            if (best < 0) break;
            const int bc = (rl[best] + rh[best]) / 2;
            const float x = X (profileHz (bc)), ya = Y (seaShown[bc] + shift), yb = Y (refShown[bc]);
            g.setColour (magenta);
            g.drawLine (x, ya, x, yb, 2.0f);
            const float dir = yb > ya ? 1.0f : -1.0f;
            juce::Path tri; tri.addTriangle (x - 5.0f, yb - dir * 7.0f, x + 5.0f, yb - dir * 7.0f, x, yb);
            g.fillPath (tri);
            const juce::String lab = (ex[best] > 0 ? "+" : "") + juce::String (ex[best], 1) + " dB " + names[best];
            g.drawText (lab, juce::Rectangle<float> (x + 8.0f, juce::jmin (ya, yb) - 2.0f, 140.0f, 13.0f), juce::Justification::centredLeft);
            ex[best] = 0.0f;
        }
    }
    // legend + frequency marks
    g.setColour (juce::Colour (0xffbffff0).withAlpha (seaInit ? 0.95f : 0.4f));
    g.drawText (fr ("\xe2\x80\x94 YOUR SOUND"), juce::Rectangle<float> (x1 - 290, y0 - 22, 290, 12), juce::Justification::centredRight);
    g.setColour (juce::Colour (0xffb49cff));
    g.drawText (fr ("- - REFERENCES \xc2\xb7 ") + in.styleName, juce::Rectangle<float> (x1 - 290, y0 - 9, 290, 12), juce::Justification::centredRight);
    g.setColour (faint);
    g.setFont (mono (9.0f));
    for (float f : { 50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f })
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), juce::Rectangle<float> (X (f) - 15, y1 - 11, 30, 10), juce::Justification::centred);
}

void WhaleScene::paint (juce::Graphics& g)
{
    g.drawImageAt (background, 0, 0);
    for (auto& p : plankton)
    {
        g.setColour (p.c);
        g.fillEllipse (p.p.x, p.p.y, p.size, p.size);
    }
    drawSeabed (g);
    drawSurface (g);

    juce::Colour ca = magenta, cb = cyan;
    if (in.mood == Mood::validated) { ca = violet; cb = cyan; }
    if (teal > 0.001f) { ca = ca.interpolatedWith (juce::Colour (0xff35d6be), teal * 0.9f); cb = cb.interpolatedWith (juce::Colour (0xff86f7e6), teal * 0.9f); }
    if (hot > 0.01f) { ca = ca.interpolatedWith (juce::Colour (0xffff5ab4), hot); cb = cb.interpolatedWith (orange, hot); }
    const float alpha = in.mood == Mood::idle ? 0.55f : 1.0f;

    if (! in.reduced) drawPulse (g, hot > 0.3f ? orange : cb);
    if (in.mood == Mood::phase)
        drawWhale (g, red, orange, 0.35f, { -H * 0.35f, H * 0.18f }, true);
    drawWhale (g, ca, cb, alpha, {}, false);
    drawTrails (g, hot > 0.3f ? orange : in.mood == Mood::validated ? cyan : violet);

    for (auto& s : sparks)
    {
        g.setColour ((s.life > 0.5f ? juce::Colour (0xffffdc78) : red).withAlpha (clamp01 (s.life)));
        g.drawLine ({ s.a, s.b }, 1.4f);
    }
    for (auto& b : bubbles)
    {
        g.setColour (juce::Colour (0xffd8fff6).withAlpha (clamp01 (b.life) * 0.7f));
        g.drawEllipse (b.p.x - b.r, b.p.y - b.r, b.r * 2.0f, b.r * 2.0f, 1.0f);
    }
    drawSouffle (g);
    drawHud (g);
}
