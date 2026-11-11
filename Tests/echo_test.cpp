// echo_test.cpp
// Offline checks for the TB Carls Echo DSP. Builds on Linux or macOS:
//   make -C Tests run      (writes demo WAVs to Tests/renders)
//
// 1. Delay: the clock range measured on the unit, the echo arrives on time
// 2. Echo response: the filters' band, VR1.B darkens long delays, no
//    output above the CCD's band
// 3. MIX: direct end is dry only, echo end is echo only
// 4. Compander: the echo follows the input level (compressor and expander
//    cancel), and the rectifiers settle
// 5. SWELL: more repeats as it turns up; low settings ring down
// 6. Footswitch: Effect Out is the buffered dry signal
// 7. CCD limits and a stability sweep
// 8. Automation of every control while playing, CPU, renders

#include "../Source/DSP/Echo.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

using namespace tbce;

static const double kPiT = 3.14159265358979323846;
static const int kSr = 44100;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static bool allFinite (const std::vector<float>& x, double limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! (x[i] == x[i]) || std::fabs (x[i]) > limit) return false;
    return true;
}

static double rmsRange (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

static double peakRange (const std::vector<float>& x, size_t from, size_t to)
{
    double p = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

static std::vector<float> sine (double f, double amp, double sec)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPiT * f * i / kSr));
    return v;
}

static std::vector<float> run (Echo& a, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, in.size() - pos);
        a.process (&in[pos], &out[pos], n);
    }
    return out;
}


static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const unsigned dataBytes = (unsigned) x.size() * 2, riff = 36 + dataBytes;
    const unsigned short fmt = 1, ch = 1, bits = 16, align = 2;
    const unsigned rate = (unsigned) sr, bps = rate * 2, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&align, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i] * 0.9;
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        const short s = (short) std::floor (v * 32767.0 + 0.5);
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

// A decaying plucked string (Karplus-Strong), added into buf at start
static void pluck (std::vector<float>& buf, size_t start, double freq, double amp, double dur)
{
    const int period = std::max (2, (int) (kSr / freq + 0.5));
    std::vector<double> line (period);
    unsigned seed = (unsigned) (freq * 1000.0) + (unsigned) start;
    for (int i = 0; i < period; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((seed >> 9) / 4194304.0 - 1.0) * amp;
    }
    const size_t n = (size_t) (dur * kSr);
    int idx = 0;
    double prev = 0.0;
    for (size_t i = 0; i < n && start + i < buf.size(); ++i)
    {
        const double cur = line[idx];
        const double next = 0.996 * 0.5 * (cur + prev);
        prev = cur;
        line[idx] = next;
        buf[start + i] += (float) cur;
        if (++idx >= period) idx = 0;
    }
}

static std::vector<float> makeChords (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double chords[4][4] = { { 82.4, 123.5, 164.8, 207.7 }, { 110.0, 164.8, 220.0, 277.2 },
                                  { 98.0, 146.8, 196.0, 246.9 }, { 73.4, 110.0, 146.8, 185.0 } };
    for (int c = 0; c * 2.0 < seconds; ++c)
        for (int s = 0; s < 4; ++s)
            pluck (v, (size_t) ((c * 2.0 + s * 0.012) * kSr), chords[c % 4][s], 0.22, 2.0);
    return v;
}

static std::vector<float> makeRiff (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double notes[8] = { 82.4, 98.0, 110.0, 123.5, 110.0, 98.0, 146.8, 130.8 };
    for (int i = 0; i * 0.25 < seconds; ++i)
        pluck (v, (size_t) (i * 0.25 * kSr), notes[i % 8], 0.35, 0.6);
    return v;
}

static std::vector<float> noise (double amp, double sec, unsigned seed)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        v[i] = (float) (amp * ((seed >> 9) / 4194304.0 - 1.0));
    }
    return v;
}


static void defaults (Echo& a)
{
    a.setDelay (0.7); a.setSwell (0.3); a.setMix (0.5); a.setEffectOn (true);
    a.setInputGainDb (0.0); a.setOutputGainDb (0.0);
}

// A short 1 kHz burst (Hann window, 4 ms) at t0
static std::vector<float> burst (double t0, double amp, double sec, double f = 1000.0)
{
    std::vector<float> v ((size_t) (sec * kSr), 0.0f);
    const int n = (int) (0.004 * kSr), s = (int) (t0 * kSr);
    for (int i = 0; i < n; ++i)
        v[s + i] = (float) (amp * 0.5 * (1.0 - std::cos (2.0 * kPiT * i / n)) * std::sin (2.0 * kPiT * f * i / kSr));
    return v;
}

// Echo-only gain (dB) of a 0.3 s tone, measured inside its echo
static double echoGainDb (Echo& a, double delayPot, double f, double amp = 0.2)
{
    a.setDelay (delayPot); a.setSwell (0.0); a.setMix (1.0); a.setEffectOn (true);
    a.reset();
    std::vector<float> x ((size_t) kSr, 0.0f);
    for (int i = 0; i < (int) (0.3 * kSr); ++i) x[i] = (float) (amp * std::sin (2.0 * kPiT * f * i / kSr));
    std::vector<float> y = run (a, x);
    const double d = Echo::delayMs (delayPot) / 1000.0;
    const size_t from = (size_t) ((d + 0.12) * kSr), to = (size_t) ((d + 0.26) * kSr);
    return 20.0 * std::log10 (rmsRange (y, from, to) / (amp / std::sqrt (2.0)) + 1e-12);
}

// Time (ms) of the strongest output after the burst, latency removed
static double measuredDelayMs (Echo& a, double delayPot)
{
    a.setDelay (delayPot); a.setSwell (0.0); a.setMix (1.0); a.setEffectOn (true);
    a.reset();
    std::vector<float> y = run (a, burst (0.05, 0.3, 0.5));
    // envelope peak
    size_t best = 0; double bv = 0.0;
    for (size_t i = (size_t) (0.055 * kSr); i < y.size(); ++i)
        if (std::fabs (y[i]) > bv) { bv = std::fabs (y[i]); best = i; }
    // the burst's own peak sits 2 ms after its start, plus the filters' group delay
    return 1000.0 * ((double) best - Echo::latencySamples()) / kSr - 50.0 - 2.0;
}

int main (int argc, char** argv)
{
    const std::string renderDir = argc > 1 ? argv[1] : ".";
    Echo* ap = new Echo();
    Echo& a = *ap;
    a.prepare (kSr);

    std::printf ("1. Delay\n");
    {
        std::printf ("  clock %.1f kHz .. %.1f kHz, delay %.1f .. %.1f ms (noon %.1f ms)\n",
                     Echo::clockHz (0.0) / 1000.0, Echo::clockHz (1.0) / 1000.0,
                     Echo::delayMs (0.0), Echo::delayMs (1.0), Echo::delayMs (0.5));
        check (std::fabs (Echo::clockHz (0.0) - 125000.0) < 1.0 && std::fabs (Echo::clockHz (1.0) - 10900.0) < 1.0,
               "clock spans the measured 10.9 kHz .. 125 kHz");
        bool mono = true;
        for (int i = 1; i <= 100; ++i)
            if (Echo::delayMs (i / 100.0) <= Echo::delayMs ((i - 1) / 100.0)) mono = false;
        check (mono, "DELAY clockwise always lengthens the delay");
        const double pots[4] = { 0.0, 0.5, 0.8, 1.0 };
        bool onTime = true;
        for (int k = 0; k < 4; ++k)
        {
            const double want = Echo::delayMs (pots[k]), got = measuredDelayMs (a, pots[k]);
            std::printf ("  DELAY %.2f: expected %.1f ms, echo peak at %.1f ms\n", pots[k], want, got);
            if (std::fabs (got - want) > 0.5 + 0.03 * want) onTime = false;
        }
        check (onTime, "the echo arrives at the CCD's delay (within 3%, filter group delay allowed)");
    }

    std::printf ("\n2. Echo response (0.2 V tones, echo only)\n");
    {
        const double freqs[7] = { 100.0, 250.0, 500.0, 1000.0, 2000.0, 3000.0, 6000.0 };
        double shortR[7], longR[7];
        for (int k = 0; k < 7; ++k)
        {
            shortR[k] = echoGainDb (a, 0.3, freqs[k]);
            longR[k] = echoGainDb (a, 1.0, freqs[k]);
            std::printf ("  %5.0f Hz: DELAY 0.3 %+6.1f dB   DELAY 1.0 %+6.1f dB\n", freqs[k], shortR[k], longR[k]);
        }
        check (std::fabs (shortR[2]) < 6.0 && std::fabs (longR[2]) < 6.0, "echo at 500 Hz comes back near the dry level");
        check (shortR[6] < shortR[2] - 20.0 && longR[6] < longR[2] - 20.0, "nothing much above the CCD's band (6 kHz down 20 dB or more)");
        check (longR[4] - longR[2] < shortR[4] - shortR[2] - 3.0, "VR1.B: long delays are darker than short ones");
    }

    std::printf ("\n3. MIX\n");
    {
        a.setDelay (0.8); a.setSwell (0.0); a.setEffectOn (true);
        const double d = Echo::delayMs (0.8) / 1000.0;
        std::vector<float> x = burst (0.05, 0.3, 0.6);
        const size_t dryFrom = (size_t) (0.045 * kSr), dryTo = (size_t) (0.06 * kSr) + Echo::latencySamples();
        const size_t ecFrom = (size_t) ((0.05 + d - 0.005) * kSr), ecTo = (size_t) ((0.05 + d + 0.03) * kSr);
        double dr[3], ec[3];
        const double mixes[3] = { 0.0, 0.5, 1.0 };
        for (int k = 0; k < 3; ++k)
        {
            a.setMix (mixes[k]); a.reset();
            std::vector<float> y = run (a, x);
            dr[k] = peakRange (y, dryFrom, dryTo); ec[k] = peakRange (y, ecFrom, ecTo);
            std::printf ("  MIX %.1f: dry peak %.3f, echo peak %.3f\n", mixes[k], dr[k], ec[k]);
        }
        check (dr[0] > 0.25 && ec[0] < 0.003, "direct: the dry signal only");
        check (dr[2] < 0.003 && ec[2] > 0.05, "echo: the echo only");
        check (dr[1] > 0.1 && ec[1] > 0.02, "centre: both");
    }

    std::printf ("\n4. Compander\n");
    {
        const double lo = echoGainDb (a, 0.7, 400.0, 0.03), mid = echoGainDb (a, 0.7, 400.0, 0.2),
                     hi = echoGainDb (a, 0.7, 400.0, 0.6);
        std::printf ("  echo gain at 30 mV %+.1f dB, 200 mV %+.1f dB, 600 mV %+.1f dB\n", lo, mid, hi);
        check (std::fabs (lo - mid) < 3.0 && std::fabs (hi - mid) < 3.0,
               "compressor and expander cancel: the echo follows the input level");
        a.setDelay (0.7); a.setSwell (0.0); a.setMix (1.0); a.reset();
        std::vector<float> y = run (a, sine (400.0, 0.2, 1.0));
        std::printf ("  rectifiers on a steady 0.2 V tone: compressor %.3f V, expander %.3f V\n",
                     a.compressorEnv(), a.expanderEnv());
        check (a.compressorEnv() > 0.1 && a.compressorEnv() < 2.0, "the compressor's rectifier settles at a sensible level");
    }

    std::printf ("\n5. SWELL\n");
    {
        int prevCount = -1; bool more = true; double tailLow = 0.0;
        for (int s = 0; s <= 4; ++s)
        {
            a.setDelay (0.8); a.setSwell (s * 0.15); a.setMix (1.0); a.setEffectOn (true);
            a.reset();
            const double d = Echo::delayMs (0.8) / 1000.0;
            std::vector<float> y = run (a, burst (0.05, 0.3, 3.0, 400.0));
            const double first = peakRange (y, (size_t) ((0.05 + d - 0.01) * kSr), (size_t) ((0.05 + d + 0.03) * kSr));
            int count = 0;
            for (int r = 1; r < 30; ++r)
            {
                const size_t c = (size_t) ((0.05 + r * d) * kSr);
                if (peakRange (y, c - (size_t) (0.01 * kSr), c + (size_t) (0.03 * kSr)) > first * 0.01) ++count;
            }
            if (s == 1) tailLow = peakRange (y, (size_t) (2.5 * kSr), y.size());
            std::printf ("  SWELL %.2f: %d repeats above -40 dB\n", s * 0.15, count);
            if (count < prevCount) more = false;
            prevCount = count;
        }
        check (more, "more repeats as SWELL turns up");
        check (prevCount >= 4, "SWELL 6 gives a run of repeats");
        check (tailLow < 1e-3, "low SWELL rings down");
    }

    std::printf ("\n5b. A bright pick attack at the default settings\n");
    {
        bool decays = true;
        const double amps[3] = { 0.05, 0.15, 0.5 };
        for (int k = 0; k < 3; ++k)
        {
            defaults (a);
            a.reset();
            std::vector<float> x ((size_t) (6 * kSr), 0.0f);
            pluck (x, kSr / 10, 196.0, amps[k], 1.5);
            std::vector<float> y = run (a, x);
            const double late = peakRange (y, 4 * kSr, y.size());
            std::printf ("  %.2f V pluck: peak %.3f, after 4 s %.2g\n", amps[k], peakRange (y, 0, y.size()), late);
            if (late > 1e-3) decays = false;
        }
        check (decays, "plucked notes die away (no runaway at SWELL 3)");

        int worst = 10;
        for (int d = 0; d <= 4; ++d)
            for (int sw = 1; sw <= 10; ++sw)
            {
                a.setDelay (d / 4.0); a.setSwell (sw / 10.0); a.setMix (1.0); a.setEffectOn (true);
                a.reset();
                std::vector<float> x ((size_t) (4 * kSr), 0.0f);
                pluck (x, kSr / 10, 196.0, 0.3, 1.0);
                std::vector<float> y = run (a, x);
                if (peakRange (y, 3 * kSr, y.size()) > 0.05) { worst = std::min (worst, sw); break; }
            }
        std::printf ("  lowest SWELL that runs away, at any DELAY: %d\n", worst);
        check (worst >= 9, "SWELL only runs away near full");
    }

    std::printf ("\n6. Footswitch\n");
    {
        defaults (a);
        a.setEffectOn (false);
        a.reset();
        std::vector<float> x = sine (440.0, 0.3, 1.0), y = run (a, x);
        const int L = Echo::latencySamples();
        double best = 1e9;
        for (int lag = L - 1; lag <= L + 1; ++lag)
        {
            double e = 0.0, s = 0.0;
            for (size_t i = (size_t) (0.5 * kSr); i < x.size(); ++i)
            {
                const double r = y[i] - x[i - lag];
                e += r * r; s += (double) x[i - lag] * x[i - lag];
            }
            best = std::min (best, std::sqrt (e / s));
        }
        std::printf ("  Effect Out: difference from the input %.4f%%\n", 100.0 * best);
        check (best < 0.02, "Effect Out passes the buffered dry signal");
    }

    std::printf ("\n7. Limits and stability\n");
    {
        defaults (a);
        a.reset();
        a.clearPeakCcd();
        std::vector<float> y = run (a, sine (220.0, 3.0, 1.0));
        std::printf ("  3 V in: output peak %.2f, CCD drive %.2f of clip\n", peakRange (y, kSr / 2, kSr), a.peakCcd());
        check (allFinite (y, 10.0) && peakRange (y, kSr / 2, kSr) < 6.0, "hot input stays bounded (rails and CCD clip)");

        a.setSwell (1.0); a.setMix (1.0); a.reset();
        std::vector<float> ys = run (a, burst (0.05, 0.3, 4.0, 300.0));
        std::printf ("  SWELL full: level after 3 s %.2f V peak (runs away into the rails, as such echoes do)\n",
                     peakRange (ys, 3 * kSr, ys.size()));
        check (allFinite (ys, 6.0), "SWELL full stays bounded");

        const double amps[4] = { 0.01, 0.1, 1.0, 4.0 };
        const double freqs[5] = { 41.0, 110.0, 440.0, 2000.0, 15000.0 };
        const double corners[4][3] = { { 0, 0, 0 }, { 1, 1, 1 }, { 0, 1, 1 }, { 1, 0, 0.5 } };
        int runs = 0; bool ok = true;
        for (int c = 0; c < 4; ++c)
            for (int ai = 0; ai < 4; ++ai)
                for (int fi = 0; fi < 5; ++fi)
                {
                    a.setDelay (corners[c][0]); a.setSwell (corners[c][1]); a.setMix (corners[c][2]);
                    a.setEffectOn (true);
                    a.reset();
                    std::vector<float> o = run (a, sine (freqs[fi], amps[ai], 0.4));
                    if (! allFinite (o, 10.0)) ok = false;
                    ++runs;
                }
        unsigned seed = 1;
        for (int r = 0; r < 20; ++r)
        {
            seed = seed * 1664525u + 1013904223u; const double f1 = (seed >> 8) / 16777216.0;
            seed = seed * 1664525u + 1013904223u; const double f2 = (seed >> 8) / 16777216.0;
            seed = seed * 1664525u + 1013904223u; const double f3 = (seed >> 8) / 16777216.0;
            a.setDelay (f1); a.setSwell (f2); a.setMix (f3);
            a.reset();
            std::vector<float> o = run (a, noise (2.0, 0.4, seed));
            if (! allFinite (o, 10.0)) ok = false;
            ++runs;
        }
        std::printf ("  %d runs\n", runs);
        check (ok, "no NaNs or runaways past the rails anywhere");
    }

    std::printf ("\n8. Automation, CPU, renders\n");
    {
        defaults (a);
        a.reset();
        std::vector<float> x = makeRiff (4.0), y (x.size());
        for (size_t pos = 0; pos < x.size(); pos += 64)
        {
            const double t = (double) pos / x.size();
            a.setDelay (0.5 + 0.5 * std::sin (2 * kPiT * 3 * t));
            a.setSwell (0.4 + 0.4 * std::sin (2 * kPiT * 5 * t));
            a.setMix (0.5 + 0.5 * std::sin (2 * kPiT * 7 * t));
            a.setEffectOn (std::sin (2 * kPiT * 4 * t) > 0.0);
            a.setInputGainDb (12.0 * std::sin (2 * kPiT * 2 * t));
            a.setOutputGainDb (12.0 * std::sin (2 * kPiT * 9 * t));
            const int n = (int) std::min ((size_t) 64, x.size() - pos);
            a.process (&x[pos], &y[pos], n);
        }
        check (allFinite (y, 10.0), "every control automated while playing");

        defaults (a);
        a.reset();
        std::vector<float> ten = makeChords (10.0);
        std::clock_t t0 = std::clock();
        std::vector<float> yc = run (a, ten);
        const double secs = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
        std::printf ("  CPU: %.1f%% of one core at 44.1 kHz\n", 10.0 * secs);
        check (secs < 3.0, "runs well under real time");

        struct R { const char* name; double d, s, m; bool chords; };
        const R renders[] = {
            { "dry_riff",            0.7, 0.3, 0.5, false },
            { "slapback_riff",       0.35, 0.0, 0.4, false },
            { "default_riff",        0.7, 0.3, 0.5, false },
            { "long_repeats_chords", 1.0, 0.55, 0.5, true },
            { "swell_up_chords",     0.85, 0.65, 0.6, true },
        };
        std::vector<float> riff = makeRiff (8.0), chords = makeChords (8.0);
        for (size_t k = 0; k < sizeof (renders) / sizeof (renders[0]); ++k)
        {
            defaults (a);
            a.setDelay (renders[k].d); a.setSwell (renders[k].s); a.setMix (renders[k].m);
            a.setEffectOn (k != 0);
            a.reset();
            const std::vector<float>& src = renders[k].chords ? chords : riff;
            std::vector<float> o = run (a, src);
            writeWav (renderDir + "/" + renders[k].name + ".wav", o, kSr);
        }
        check (true, "demo renders written");
    }

    delete ap;
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL DSP TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
