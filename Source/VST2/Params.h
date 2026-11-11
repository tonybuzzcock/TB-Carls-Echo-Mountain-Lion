// Params.h
// Parameter table shared by the VST wrapper, the editor and the preset store.
//
// Front-panel controls first (DELAY, SWELL, MIX and the S1 footswitch), then the
// plugin-only controls in the editor's bottom strip.

#ifndef TBCE_PARAMS_H
#define TBCE_PARAMS_H

namespace tbce
{

enum Param
{
    // Front panel
    kDelay = 0, kSwell, kMix, kEffect,
    // Plugin only (bottom strip of the editor)
    kInput, kOutput,
    kNumParams
};

enum Kind { kKnob, kDelayMs, kSwitch, kDb12, kDb24 };

const char* const kParamNames[kNumParams] = {
    "Delay", "Swell", "Mix", "Effect",
    "Input", "Output"
};

const int kParamKind[kNumParams] = {
    kDelayMs, kKnob, kKnob, kSwitch,
    kDb12, kDb24
};

const float kDefaults[kNumParams] = {
    0.7f, 0.3f, 0.5f, 1.0f,
    0.5f, 0.5f
};

const double kInputRangeDb  = 12.0;
const double kOutputRangeDb = 24.0;

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tbce

#endif
