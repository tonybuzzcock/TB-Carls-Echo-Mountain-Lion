// Echo.h
// Carlsbro Echo (the big mains-powered "Alu series" analogue delay pedal),
// modelled from Dirk Hendrik's drawing (rev P, 23 Feb 2012, traced from a
// real unit):
//
//   JK1 -> C1 470n -> R1 1K -> IC1.C follower (R4 100K to VBIAS, C3 47p)
//      -> C4 47u -> DRY (DRY OUTPUT jack, bottom of MIX, S1 bypass)
//      -> C5 4u7 -> R5 10K into IC2.B (NE571 compressor: G cell in the
//         feedback, rectifier on the output, C6 680n; C8 220p across it)
//         SWELL feedback also sums in here through R6 6K8
//   -> R9 12K / R10 15K / C45 4n7 / C11 4n7: Sallen-Key low-pass round IC1.D,
//      whose gain is 1 + R13 / (R12 27K || (R11 18K + C10 10n)); R13 is
//      drawn as 22K but taken as 2K2 (see Echo.cpp): x1.08 bass, x1.2 treble
//   -> two 3rd-order low-pass sections, R14 15K C13 4n7 R15 12K C14 22n
//      R16 15K C15 470p (IC1.A), R17 15K C16 4n7 R18 12K C18 22n R19 15K
//      C17 470p (IC1.B)
//   -> VR1.B (47K log, ganged with the clock pot) with R20 2K2 + C19 4n7
//      from its wiper to ground: a tone filter that darkens as the delay
//      gets longer
//   -> C20 4u7 -> IC3 R5101 CCD input (R21 100K bias from the BBD BIAS trim,
//      C46 47p)
//   IC3 outputs (pins 8, 12) -> R29 / R30 22K -> VR5 5K OUTPUT TRIM wiper
//      -> C27 4u7 -> IC5.D inverting, R34 18K feedback
//   -> two more 3rd-order low-pass sections: R35 4K7 C28 4n7 R36 15K C29 22n
//      R37 15K C30 470p (IC5.A), R38 22K C31 4n7 R39 10K C32 22n R40 22K
//      C33 470p (IC5.B)
//   -> C34 4u7 -> IC2.A (NE571 expander, C37 680n, C36 1n across R3)
//   -> C35 4u7 -> TP24 (the echo)
//      -> R41 2K2, C38 47n to ground, VR2 22K lin SWELL, wiper -> C39 4u7
//         -> R6 6K8 back into the compressor (the repeats)
//      -> top of VR3 22K lin MIX (bottom = DRY); wiper -> S1.A
//   S1.A: MIX wiper, or DRY (effect off) -> C40 4u7 -> IC5.C follower
//      -> C41 4u7 / R45 22K -> R46 100R -> MIX OUTPUT
//   Clock: IC4 (CD4007) oscillator, R33 3K9 + VR1.A (47K log) with C26 680p
//      + 100p; measured 10.9 kHz to 125 kHz.
//
// Modelled as a circuit: every resistor and capacitor in the audio path
// (each op-amp stage from its transfer function), the RC4136s and NE571s
// clipping at their rails, the two NE571s as gain cells driven by their
// rectifiers (so the compander breathes as the real one does), the CCD as
// a clocked sample-and-hold chain with its staircase output and clock
// aliasing, and the clock frequency set by VR1.A.
// Modelled behaviourally, values estimated (named constants in Echo.cpp):
// the R5101's length in clock periods, gain and clipping level, the clock
// law between the measured end points, the NE571s' unity-gain level and the
// supply rails.
//
// Volts in, volts out (1.0 = 1 V). Runs at 4x with Oversampler4x.
// Plain C++98, no allocation after construction.

#ifndef TBCE_ECHO_H
#define TBCE_ECHO_H

#include "Filters.h"
#include "Oversampler4x.h"

namespace tbce
{

class Echo
{
public:
    Echo();

    void prepare (double sampleRate);
    void reset();

    // Front panel (0..1 pot rotation)
    void setDelay (double v);           // VR1 47K log dual: clock and tone
    void setSwell (double v);           // VR2 22K lin: repeats
    void setMix (double v);             // VR3 22K lin: direct .. echo
    void setEffectOn (bool on);         // S1: effect in / dry
    // Plugin only
    void setInputGainDb (double dB);
    void setOutputGainDb (double dB);

    // Mono in, mono out
    void process (const float* in, float* out, int numSamples);

    static int latencySamples() { return Oversampler4x::latencySamples(); }

    // Introspection for the tests
    static double clockHz (double delayPot);
    static double delayMs (double delayPot);
    static double audioTaper (double x);
    double currentDelayMs() const { return 1000.0 * curDelay; }
    double peakCcd() const { return peakCcdLevel; }
    void   clearPeakCcd() { peakCcdLevel = 0.0; }
    double compressorEnv() const { return envC; }
    double expanderEnv() const { return envE; }

    enum { kStages = 2000, kRing = 4096 };

private:
    double tickOs (double x);
    void   updateDelayFilters (double delayPot);
    void   updateSwellNet (double swellPot);
    void   updateMix (double mixPot);

    double fsBase, fsOs;
    Oversampler4x up, down;

    // Targets and smoothed values
    double delayT, swellT, mixT, effectT, inT, outT;
    double delayP, swell, mix, effect, inG, outG;
    double smKnob, smSwitch;
    double delayApplied, swellApplied, mixApplied;
    int countdown;

    // Input and dry
    FirstOrder inHp;            // C1 / R4
    FirstOrder c5Hp;            // C5 into R5
    // Compressor IC2.B
    double vComp, iPrev, envC;
    double kEnvC, kEnvE;
    // Pre-filters
    ThirdOrder preSk;           // IC1.D Sallen-Key with its emphasis network
    ThirdOrder pre1, pre2;      // IC1.A, IC1.B
    FirstOrder toneVr1b;        // VR1.B, R20, C19 into the CCD's input
    FirstOrder c20Hp;           // C20 into R21
    // CCD
    float ring[kRing];
    int ringPos;
    double tickPhase, ccdHeld, lastCcdIn, curDelay;
    FirstOrder c27Hp;           // C27 into IC5.D
    ThirdOrder post1, post2;    // IC5.A, IC5.B
    FirstOrder c34Hp;           // C34 into the expander
    // Expander IC2.A
    FirstOrder expLp;           // C36 across R3
    double envE;
    FirstOrder c35Hp;           // C35
    // Swell
    FirstOrder swellNet;        // R41, C38, VR2, into R6's virtual ground
    FirstOrder c39Hp;           // C39 / R6
    double echoNode;            // TP24, one sample back for the feedback
    double mixWet, mixDry;
    // Output
    FirstOrder c40Hp, c41Hp;

    double peakCcdLevel;
};

} // namespace tbce

#endif
