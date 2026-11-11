// Echo.cpp
// See Echo.h for the signal path. Component values are from the drawing;
// the estimates (things the drawing doesn't fix) are marked.

#include "Echo.h"

#include <cmath>
#include <cstring>

namespace tbce
{

namespace
{
    // Supply: Z1 11V8 and TR1 BC182 -> VCC about 11.2 V, VBIAS = VCC / 2
    const double kRail = 4.1;                  // RC4136 / NE571 swing about VBIAS

    // Input: C1 470n into R4 100K; C5 4u7 into R5 10K
    const double kC1 = 470.0e-9, kR4 = 100.0e3, kC5 = 4.7e-6, kR5 = 10.0e3;
    // Compressor IC2.B: R6 6K8 swell input, C8 220p across the G cell, C6 680n
    const double kR6 = 6.8e3, kC8 = 220.0e-12, kC6 = 680.0e-9;
    // IC1.D Sallen-Key: R9 12K, R10 15K, C45 4n7 to ground, C11 4n7 to the output;
    // gain network R13 22K, R12 27K (C12 4u7 treated as a short), R11 18K + C10 10n
    const double kR9 = 12.0e3, kR10 = 15.0e3, kC45 = 4.7e-9, kC11 = 4.7e-9;
    const double kR13 = 22.0e3, kR12 = 27.0e3, kR11 = 18.0e3, kC10 = 10.0e-9;
    // VR1.B 47K log, R20 2K2 + C19 4n7; C20 4u7 into R21 100K
    const double kVR1 = 47.0e3, kR20 = 2.2e3, kC19 = 4.7e-9, kC20 = 4.7e-6, kR21 = 100.0e3;
    // IC5.D: R29 / R30 22K into the ends of VR5 5K (wiper centred), R34 18K
    const double kR29 = 22.0e3, kVR5 = 5.0e3, kR34 = 18.0e3, kC27 = 4.7e-6;
    // Expander IC2.A: C34 4u7, C37 680n, C36 1n across R3; C35 4u7
    const double kC34 = 4.7e-6, kC37 = 680.0e-9, kC36 = 1.0e-9, kC35 = 4.7e-6;
    // Swell: R41 2K2, C38 47n, VR2 22K lin, C39 4u7
    const double kR41 = 2.2e3, kC38 = 47.0e-9, kVR2 = 22.0e3, kC39 = 4.7e-6;
    // Mix: VR3 22K lin into C40 4u7 / R43 100K; C41 4u7 / R45 22K
    const double kVR3 = 22.0e3, kC40 = 4.7e-6, kR43 = 100.0e3, kC41 = 4.7e-6, kR45 = 22.0e3;
    // Clock: R33 3K9 + VR1.A; measured 10.9 kHz to 125 kHz at TP12
    const double kClockMax = 125.0e3, kClockMin = 10.9e3;

    // NE571 internals (datasheet): R3 20K, rectifier 10K
    const double kNe571R = 20.0e3, kNe571Rect = 10.0e3;

    // --- Estimates (not fixed by the drawing) ---------------------------------
    // NE571 unity-gain level, average volts (0 dBu sine, 775 mV rms)
    const double kVu = 0.70;
    // Compressor's largest gain at very low levels (relative to R5)
    const double kCompMaxGain = 100.0;
    // Clock law: f = A / (R + R0) with R the VR1.A track in circuit; R0 and A
    // fitted to the measured end points (R0 comes out near R33 3K9).
    const double kClockR0 = kVR1 / (kClockMax / kClockMin - 1.0);
    const double kClockA  = kClockMax * kClockR0;
    // R5101 CCD: one sample per clock period through its 2000 stages
    // (Echo::kStages), so 16 ms to 183 ms over the measured clock range.
    // Gain through the chip and its outputs, and its clipping level at the
    // input (BBD BIAS trim centred).
    const double kCcdGain = 0.40;
    const double kCcdClip = 2.0;
    // -------------------------------------------------------------------------

    inline double clamp01 (double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

    inline double softClip (double v, double limit)
    {
        const double knee = 0.7 * limit;
        const double a = std::fabs (v);
        if (a <= knee) return v;
        const double c = knee + (limit - knee) * std::tanh ((a - knee) / (limit - knee));
        return v < 0.0 ? -c : c;
    }

    inline double highpassFc (double r, double c) { return 1.0 / (2.0 * kPi * r * c); }

    // Polynomials in s, coefficients lowest power first, up to s^3
    void polyMul (const double* a, int na, const double* b, int nb, double* out)
    {
        for (int i = 0; i < na + nb - 1; ++i) out[i] = 0.0;
        for (int i = 0; i < na; ++i)
            for (int j = 0; j < nb; ++j)
                out[i + j] += a[i] * b[j];
    }
}

//==============================================================================
double Echo::audioTaper (double x)
{
    const double k = 4.394449154672439;        // 10% of the track at mid-rotation
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    return (std::exp (k * x) - 1.0) / (std::exp (k) - 1.0);
}

double Echo::clockHz (double pot)
{
    // DELAY clockwise puts more of VR1.A in circuit: a slower clock
    return kClockA / (kVR1 * audioTaper (clamp01 (pot)) + kClockR0);
}

double Echo::delayMs (double pot) { return 1000.0 * kStages / clockHz (pot); }

//==============================================================================
Echo::Echo()
    : fsBase (44100.0), fsOs (176400.0),
      delayT (0.55), swellT (0.35), mixT (0.5), effectT (1.0), inT (1.0), outT (1.0),
      delayApplied (-1.0), swellApplied (-1.0), mixApplied (-1.0), countdown (0),
      vComp (0.0), iPrev (0.0), envC (0.0), kEnvC (0.0), kEnvE (0.0),
      ringPos (0), tickPhase (0.0), ccdHeld (0.0), lastCcdIn (0.0), curDelay (0.1),
      envE (0.0), echoNode (0.0), mixWet (0.5), mixDry (0.5), peakCcdLevel (0.0)
{
    std::memset (ring, 0, sizeof (ring));
    prepare (44100.0);
}

void Echo::prepare (double sampleRate)
{
    fsBase = sampleRate > 1000.0 ? sampleRate : 44100.0;
    fsOs = fsBase * Oversampler4x::kFactor;

    smKnob   = std::exp (-1.0 / (0.020 * fsBase));
    smSwitch = std::exp (-1.0 / (0.006 * fsBase));

    inHp.highpass (highpassFc (kR4, kC1), fsOs);
    c5Hp.highpass (highpassFc (kR5, kC5), fsOs);

    // Rectifiers: internal 10K into C6 / C37
    kEnvC = 1.0 - std::exp (-1.0 / (kNe571Rect * kC6 * fsOs));
    kEnvE = 1.0 - std::exp (-1.0 / (kNe571Rect * kC37 * fsOs));

    // IC1.D: H = K / (D a + s R9 C45 D + s R9 C11 (a D - N)), K = N / D,
    //   N = n0 + n1 s, D = 1 + s R11 C10, a = 1 + s R10 C45
    {
        const double n0 = 1.0 + kR13 / kR12;
        const double n1 = kC10 * (kR11 * n0 + kR13);
        const double N[2] = { n0, n1 };
        const double D[2] = { 1.0, kR11 * kC10 };
        const double A[2] = { 1.0, kR10 * kC45 };
        double Da[3]; polyMul (D, 2, A, 2, Da);
        const double s9c45[2] = { 0.0, kR9 * kC45 };
        double t2[3]; polyMul (s9c45, 2, D, 2, t2);
        double aDmN[3] = { Da[0] - N[0], Da[1] - N[1], Da[2] };
        const double s9c11[2] = { 0.0, kR9 * kC11 };
        double t3[4]; polyMul (s9c11, 2, aDmN, 3, t3);
        const double num[4] = { n0, n1, 0.0, 0.0 };
        const double den[4] = { Da[0] + t2[0] + t3[0], Da[1] + t2[1] + t3[1], Da[2] + t2[2] + t3[2], t3[3] };
        preSk.setAnalogFull (num, den, fsOs);
    }
    pre1.setSection3 (15.0e3, 4.7e-9, 12.0e3, 22.0e-9, 15.0e3, 470.0e-12, fsOs);   // R14 C13 R15 C14 R16 C15
    pre2.setSection3 (15.0e3, 4.7e-9, 12.0e3, 22.0e-9, 15.0e3, 470.0e-12, fsOs);   // R17 C16 R18 C18 R19 C17
    c20Hp.highpass (highpassFc (kR21, kC20), fsOs);

    c27Hp.highpass (highpassFc ((kR29 + 0.5 * kVR5) * 0.5, kC27), fsOs);
    post1.setSection3 (4.7e3, 4.7e-9, 15.0e3, 22.0e-9, 15.0e3, 470.0e-12, fsOs);   // R35 C28 R36 C29 R37 C30
    post2.setSection3 (22.0e3, 4.7e-9, 10.0e3, 22.0e-9, 22.0e3, 470.0e-12, fsOs);  // R38 C31 R39 C32 R40 C33
    c34Hp.highpass (highpassFc (kNe571R, kC34), fsOs);
    expLp.lowpass (1.0 / (2.0 * kPi * kNe571R * kC36), fsOs);
    c35Hp.highpass (highpassFc (kVR3, kC35), fsOs);
    c39Hp.highpass (highpassFc (kR6, kC39), fsOs);
    c40Hp.highpass (highpassFc (kR43, kC40), fsOs);
    c41Hp.highpass (highpassFc (kR45, kC41), fsOs);

    reset();
}

void Echo::reset()
{
    delayP = delayT; swell = swellT; mix = mixT; effect = effectT; inG = inT; outG = outT;

    up.reset(); down.reset();
    inHp.reset(); c5Hp.reset();
    vComp = 0.0; iPrev = 0.0; envC = 0.0; envE = 0.0;
    preSk.reset(); pre1.reset(); pre2.reset(); toneVr1b.reset(); c20Hp.reset();
    std::memset (ring, 0, sizeof (ring));
    ringPos = 0; tickPhase = 0.0; ccdHeld = 0.0; lastCcdIn = 0.0;
    c27Hp.reset(); post1.reset(); post2.reset(); c34Hp.reset(); expLp.reset(); c35Hp.reset();
    swellNet.reset(); c39Hp.reset(); echoNode = 0.0;
    c40Hp.reset(); c41Hp.reset();
    peakCcdLevel = 0.0;

    delayApplied = swellApplied = mixApplied = -1.0;
    updateDelayFilters (delayP);
    updateSwellNet (swell);
    updateMix (mix);
    curDelay = kStages / clockHz (delayP);
    countdown = 0;
}

void Echo::setDelay (double v)            { delayT = clamp01 (v); }
void Echo::setSwell (double v)            { swellT = clamp01 (v); }
void Echo::setMix (double v)              { mixT = clamp01 (v); }
void Echo::setEffectOn (bool on)          { effectT = on ? 1.0 : 0.0; }
void Echo::setInputGainDb (double dB)     { inT = std::pow (10.0, dB / 20.0); }
void Echo::setOutputGainDb (double dB)    { outT = std::pow (10.0, dB / 20.0); }

//==============================================================================
void Echo::updateDelayFilters (double pot)
{
    // VR1.B: Ra from IC1.B's output to the wiper (more of it as the delay gets
    // longer), R20 + C19 from the wiper to ground, Rb from the wiper on to
    // C20 and R21 100K. Rq = Rb + R21:
    //   out / in = R21 (1 + s R20 C19) / ((Ra + Rq) + s C19 (Ra (R20 + Rq) + Rq R20))
    const double ra = kVR1 * audioTaper (pot);
    const double rq = (kVR1 - ra) + kR21;
    toneVr1b.setAnalog (kR21 * kR20 * kC19, kR21,
                        kC19 * (ra * (kR20 + rq) + rq * kR20), ra + rq, fsOs);
    delayApplied = pot;
}

void Echo::updateSwellNet (double pot)
{
    // TP24 -> R41 -> node (C38 to ground) -> VR2 top; wiper loaded by R6 into
    // the compressor's virtual ground. Wiper voltage / echo:
    //   Rlw / ((R41 + Rp) + s R41 Rp C38),  Rp = Ru + Rlw,  Rlw = Rl || R6
    const double rl = kVR2 * pot, ru = kVR2 - rl;
    const double rlw = rl * kR6 / (rl + kR6 + 1.0e-9);
    const double rp = ru + rlw;
    swellNet.setAnalog (0.0, rlw, kR41 * rp * kC38, kR41 + rp, fsOs);
    swellApplied = pot;
}

void Echo::updateMix (double pot)
{
    // VR3 wiper between the echo (top) and DRY (bottom), loaded by R43 100K
    const double rs = pot * (1.0 - pot) * kVR3;
    const double load = kR43 / (kR43 + rs);
    mixWet = pot * load;
    mixDry = (1.0 - pot) * load;
    mixApplied = pot;
}

//==============================================================================
double Echo::tickOs (double x)
{
    // IC1.C follower
    const double dry = railClip (inHp.tick (x * inG), kRail);

    // IC2.B compressor: C8 dv/dt = -(i + G v), G from the rectifier
    const double vw = c39Hp.tick (swellNet.tick (echoNode));
    const double i = c5Hp.tick (dry) / kR5 + vw / kR6;
    double G = envC / (kVu * kNe571R);
    const double gMin = 2.0 / (kCompMaxGain * kNe571R);
    if (G < gMin) G = gMin;
    const double k2c = 2.0 * fsOs * kC8;
    vComp = (vComp * (k2c - G) - (i + iPrev)) / (k2c + G);
    vComp = railClip (vComp, kRail);
    iPrev = i;
    envC += (std::fabs (vComp) - envC) * kEnvC;

    // Pre-emphasis / anti-alias filters, VR1.B
    double v = railClip (preSk.tick (vComp), kRail);
    v = railClip (pre1.tick (v), kRail);
    v = railClip (pre2.tick (v), kRail);
    v = c20Hp.tick (toneVr1b.tick (v));

    // R5101: samples once per clock period, 2000 periods back
    const double cin = softClip (v, kCcdClip);
    {
        const double a = std::fabs (cin) / kCcdClip;
        if (a > peakCcdLevel) peakCcdLevel = a;
    }
    double step = (kStages / curDelay) / fsOs;          // clock periods per oversampled sample
    if (step < 1.0e-3) step = 1.0e-3;
    const double inv = 1.0 / step;
    double pos = (1.0 - tickPhase) * inv;
    double last = 0.0, acc = 0.0;
    int guard = 0;
    while (pos <= 1.0 && guard < 64)
    {
        acc += ccdHeld * (pos - last);
        ring[ringPos] = (float) (lastCcdIn + (cin - lastCcdIn) * pos);
        ccdHeld = (double) ring[(ringPos - kStages) & (kRing - 1)];
        ringPos = (ringPos + 1) & (kRing - 1);
        last = pos;
        pos += inv;
        ++guard;
    }
    acc += ccdHeld * (1.0 - last);
    tickPhase = 1.0 - (pos - 1.0) * step;
    if (tickPhase < 0.0) tickPhase = 0.0;
    if (tickPhase >= 1.0) tickPhase = 0.999999;
    lastCcdIn = cin;

    // IC5.D, then the reconstruction filters
    const double gainD = -kR34 / ((kR29 + 0.5 * kVR5) * 0.5);
    v = railClip (gainD * c27Hp.tick (kCcdGain * acc), kRail);
    v = railClip (post1.tick (v), kRail);
    v = railClip (post2.tick (v), kRail);

    // IC2.A expander: G cell driven by its own input's rectifier
    const double ein = c34Hp.tick (v);
    envE += (std::fabs (ein) - envE) * kEnvE;
    const double vExp = railClip (-expLp.tick (ein * envE / kVu), kRail);
    echoNode = c35Hp.tick (vExp);

    // MIX, S1, IC5.C
    const double mixed = mixDry * dry + mixWet * echoNode;
    const double sel = effect * mixed + (1.0 - effect) * dry;
    return c41Hp.tick (railClip (c40Hp.tick (sel), kRail));
}

void Echo::process (const float* in, float* out, int numSamples)
{
    const double aK = 1.0 - smKnob, aW = 1.0 - smSwitch;
    double xs[Oversampler4x::kFactor], ys[Oversampler4x::kFactor];

    for (int n = 0; n < numSamples; ++n)
    {
        delayP += (delayT - delayP) * aK;
        swell += (swellT - swell) * aK;
        mix += (mixT - mix) * aK;
        effect += (effectT - effect) * aW;
        inG += (inT - inG) * aK;
        outG += (outT - outG) * aK;

        // Pot-dependent networks: rebuilt every 16 samples while moving
        if (--countdown <= 0)
        {
            countdown = 16;
            if (std::fabs (delayP - delayApplied) > 1.0e-6) updateDelayFilters (delayP);
            if (std::fabs (swell - swellApplied) > 1.0e-6) updateSwellNet (swell);
            if (std::fabs (mix - mixApplied) > 1.0e-6) updateMix (mix);
        }
        curDelay = kStages / clockHz (delayP);

        float a = in[n];
        if (! (a == a)) a = 0.0f;
        up.upsample ((double) a, xs);
        for (int k = 0; k < Oversampler4x::kFactor; ++k)
            ys[k] = tickOs (xs[k]);
        out[n] = (float) (down.downsample (ys) * outG);
    }
}

} // namespace tbce
