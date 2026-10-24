#pragma once

#include "Parameters.h"

struct SynthPreset { const char* name; std::vector<std::pair<const char*, float>> values; };

/** Заводські пресети. Не вказані параметри — за замовчуванням.
    Індекси: хвилі Saw 0 / Square 1 / Tri 2 / Sine 3 / Super 4; фільтр Ladder24 0 / Ladder12 1 / MS-20 2 / Band 3 / High 4;
    драйв Warm 0 / Fuzz 1 / Fold 2 / Rectify 3; режим Poly 0 / Mono 1 / Legato 2; двигун Analog 0 / Sax 1 / Bowls 2;
    джерела: LFO1 1, LFO2 2, ModEnv 3, Vel 4, Wheel 5, AT 6; цілі: Pitch 1, PitchA 2, PitchB 3, ShapeA 4, ShapeB 5, FM 6,
    Cutoff 7, Reso 8, Drive 9, Amp 10, Pan 11, Detune 12, Noise 13, Breath 14, Growl 15. */
inline const std::vector<SynthPreset>& getSynthPresets()
{
    using namespace SynIDs;
    static const std::vector<SynthPreset> p {
        { "Init", {} },

        // ---------- BASS ----------
        { "BASS · Velvet Grit", { { mode, 1 }, { glide, 30 }, { aWave, 0 }, { aLevel, 80 }, { bWave, 1 }, { bOct, -1 }, { bLevel, 55 }, { bFine, 0 },
            { subLevel, 60 }, { cutoff, 380 }, { reso, 20 }, { fEnv, 35 }, { fD, 300 }, { fS, 20 }, { fKey, 30 },
            { dType, 0 }, { dAmount, 45 }, { dLow, 120 }, { dTone, 4500 }, { aR, 120 }, { drift, 5 } } },
        { "BASS · Hand That Feeds Fuzz", { { mode, 1 }, { aWave, 0 }, { bWave, 1 }, { bLevel, 70 }, { bFine, 9 }, { subLevel, 35 },
            { fType, 2 }, { cutoff, 900 }, { reso, 35 }, { fEnv, 30 }, { fD, 250 }, { fS, 35 }, { dType, 1 }, { dAmount, 65 }, { dLow, 90 }, { dTone, 6000 },
            { aR, 100 }, { drift, 5 } } },
        { "BASS · Future Proof Drive", { { mode, 1 }, { aWave, 0 }, { bWave, 0 }, { bOct, -1 }, { bLevel, 60 }, { bFine, 12 }, { subLevel, 40 },
            { cutoff, 600 }, { reso, 25 }, { fEnv, 45 }, { fD, 220 }, { fS, 25 }, { dType, 0 }, { dAmount, 55 }, { dLow, 100 }, { dTone, 5500 }, { aR, 110 } } },
        { "BASS · Reese Deep", { { mode, 1 }, { glide, 20 }, { aWave, 0 }, { aUni, 3 }, { aDetune, 30 }, { aSpread, 30 }, { bWave, 0 }, { bLevel, 70 }, { bFine, -15 },
            { subLevel, 45 }, { cutoff, 500 }, { reso, 15 }, { fEnv, 15 }, { dAmount, 30 }, { dLow, 120 }, { aR, 200 } } },
        { "BASS · Invaders Snarl", { { mode, 1 }, { aWave, 0 }, { aUni, 3 }, { aDetune, 35 }, { aSpread, 40 }, { bWave, 1 }, { bLevel, 45 }, { bOct, -1 },
            { fType, 2 }, { cutoff, 1200 }, { reso, 45 }, { fEnv, 50 }, { fD, 150 }, { fS, 20 }, { dType, 1 }, { dAmount, 70 }, { dLow, 100 }, { dTone, 7000 }, { aR, 90 } } },
        { "BASS · Sub Pressure", { { mode, 1 }, { aWave, 3 }, { aShape, 30 }, { aLevel, 75 }, { subLevel, 70 }, { cutoff, 300 }, { reso, 10 }, { fEnv, 20 },
            { dAmount, 25 }, { dLow, 80 }, { aA, 3 }, { aR, 150 }, { drift, 0 } } },
        { "BASS · Rubber Pluck", { { mode, 1 }, { aWave, 1 }, { aShape, 30 }, { cutoff, 250 }, { reso, 35 }, { fEnv, 60 }, { fD, 180 }, { fS, 0 },
            { aS, 70 }, { dAmount, 20 }, { dLow, 90 }, { aR, 90 } } },
        { "BASS · Fold Growl", { { volume, -11 }, { mode, 1 }, { aWave, 2 }, { aShape, 40 }, { subLevel, 50 }, { fType, 1 }, { cutoff, 1500 }, { reso, 20 }, { fEnv, 10 },
            { dType, 2 }, { dAmount, 35 }, { dLow, 120 }, { l1Sync, 1 }, { l1Div, 6 }, { l1Shape, 0 },
            { mSrc[0], 1 }, { mDst[0], 4 }, { mAmt[0], 40 }, { mSrc[1], 5 }, { mDst[1], 7 }, { mAmt[1], 30 } } },
        { "BASS · Octave Fuzz", { { mode, 1 }, { aWave, 1 }, { subLevel, 30 }, { cutoff, 1100 }, { reso, 15 }, { fEnv, 25 }, { dType, 3 }, { dAmount, 60 }, { dLow, 110 }, { dTone, 5000 } } },
        { "BASS · Analog Mono", { { mode, 1 }, { aWave, 0 }, { bWave, 1 }, { bOct, -1 }, { bLevel, 60 }, { bFine, 0 }, { cutoff, 450 }, { reso, 30 },
            { fEnv, 40 }, { fD, 400 }, { fS, 30 }, { aR, 120 } } },

        // ---------- LEAD / RIFF ----------
        { "LEAD · Invader Riff", { { aWave, 4 }, { aDetune, 40 }, { aShape, 60 }, { bWave, 0 }, { bOct, 1 }, { bLevel, 35 }, { fType, 2 }, { cutoff, 3000 }, { reso, 30 },
            { fEnv, 25 }, { fD, 300 }, { fS, 40 }, { dType, 1 }, { dAmount, 40 }, { dLow, 150 }, { dlOn, 1 }, { dlTime, 3 }, { dlMix, 15 }, { dlFb, 25 } } },
        { "LEAD · Burning Up", { { mode, 1 }, { glide, 40 }, { aWave, 0 }, { bWave, 1 }, { bOct, -1 }, { bLevel, 50 }, { fType, 2 }, { cutoff, 1800 }, { reso, 55 },
            { fEnv, 35 }, { fD, 400 }, { fS, 40 }, { dAmount, 20 }, { dlOn, 1 }, { dlTime, 2 }, { dlMix, 20 }, { vibAuto, 20 } } },
        { "LEAD · High Rise", { { volume, -10 }, { aWave, 1 }, { aShape, 40 }, { bWave, 0 }, { bSemi, 7 }, { bLevel, 50 }, { fType, 1 }, { cutoff, 2200 }, { reso, 20 }, { fEnv, 25 },
            { l1Rate, 0.7f }, { mSrc[0], 1 }, { mDst[0], 4 }, { mAmt[0], 30 }, { dlOn, 1 }, { dlTime, 3 }, { dlFb, 40 }, { dlMix, 22 }, { chOn, 1 } } },
        { "LEAD · Phantom Saw", { { aWave, 0 }, { aUni, 5 }, { aDetune, 22 }, { aSpread, 70 }, { bWave, 0 }, { bOct, -1 }, { bLevel, 40 }, { cutoff, 1600 }, { reso, 20 },
            { fEnv, 20 }, { dAmount, 15 }, { chOn, 1 }, { rvOn, 1 }, { rvSize, 55 }, { rvMix, 25 }, { aR, 400 } } },
        { "LEAD · Sync Scream", { { mode, 1 }, { aWave, 0 }, { aLevel, 40 }, { bWave, 0 }, { bLevel, 80 }, { bSync, 1 }, { bSemi, 7 }, { fType, 2 }, { cutoff, 4000 }, { reso, 25 },
            { mD, 500 }, { mSrc[0], 3 }, { mDst[0], 3 }, { mAmt[0], 45 }, { mSrc[1], 5 }, { mDst[1], 3 }, { mAmt[1], 40 }, { dAmount, 30 }, { dlOn, 1 }, { dlMix, 18 } } },
        { "LEAD · Hit Parade", { { aWave, 1 }, { aShape, 15 }, { bWave, 2 }, { bOct, 1 }, { bLevel, 45 }, { cutoff, 2000 }, { reso, 20 }, { fEnv, 20 }, { chOn, 1 }, { chMix, 40 },
            { vibAuto, 40 }, { rvOn, 1 }, { rvSize, 40 }, { rvMix, 15 } } },
        { "LEAD · Light Up The Sky", { { aWave, 0 }, { bWave, 3 }, { bSemi, 7 }, { fm, 40 }, { bLevel, 0 }, { fType, 3 }, { cutoff, 1800 }, { reso, 35 }, { fEnv, 40 }, { fD, 250 },
            { dType, 1 }, { dAmount, 30 }, { dLow, 150 }, { mSrc[0], 3 }, { mDst[0], 6 }, { mAmt[0], 40 } } },
        { "LEAD · Glide Mono", { { mode, 2 }, { glide, 120 }, { aWave, 0 }, { bWave, 0 }, { bLevel, 60 }, { bFine, 6 }, { cutoff, 3000 }, { reso, 25 }, { fEnv, 15 },
            { vibAuto, 30 }, { dlOn, 1 }, { dlTime, 5 }, { dlMix, 15 } } },

        // ---------- PAD / KEYS / ARP ----------
        { "PAD · Dark Supersaw", { { volume, -13 }, { aWave, 4 }, { aDetune, 35 }, { aShape, 50 }, { bWave, 4 }, { bOct, -1 }, { bLevel, 50 }, { cutoff, 2500 }, { fEnv, 10 },
            { aA, 300 }, { aR, 1200 }, { chOn, 1 }, { rvOn, 1 }, { rvSize, 60 }, { rvMix, 30 } } },
        { "PAD · Phantogram Haze", { { volume, -13 }, { aWave, 0 }, { aUni, 5 }, { aDetune, 20 }, { aSpread, 90 }, { bWave, 1 }, { bLevel, 40 }, { fType, 1 }, { cutoff, 1200 }, { reso, 15 },
            { aA, 600 }, { aR, 1500 }, { l2Rate, 0.15f }, { mSrc[0], 2 }, { mDst[0], 7 }, { mAmt[0], 15 }, { chOn, 1 }, { rvOn, 1 }, { rvSize, 70 }, { rvMix, 40 }, { drift, 25 } } },
        { "KEYS · Electro Stab", { { aWave, 0 }, { aUni, 3 }, { aDetune, 25 }, { bWave, 0 }, { bOct, 1 }, { bLevel, 40 }, { cutoff, 900 }, { reso, 25 }, { fEnv, 60 }, { fD, 250 }, { fS, 10 },
            { aD, 400 }, { aS, 40 }, { aR, 250 }, { dlOn, 1 }, { dlTime, 3 }, { dlMix, 20 } } },
        { "KEYS · Brass Synth", { { aWave, 0 }, { bWave, 0 }, { bLevel, 70 }, { bFine, 8 }, { cutoff, 700 }, { reso, 10 }, { fA, 60 }, { fD, 500 }, { fS, 50 }, { fEnv, 50 },
            { aA, 20 }, { aR, 300 }, { vibAuto, 20 }, { chOn, 1 }, { chMix, 30 } } },
        { "ARP · Ladytron Pulse", { { aWave, 1 }, { aShape, 35 }, { cutoff, 1500 }, { reso, 30 }, { fEnv, 40 }, { fD, 150 }, { fS, 0 }, { aS, 60 }, { aR, 120 },
            { arpOn, 1 }, { arpMode, 0 }, { arpRate, 8 }, { arpOct, 2 }, { arpGate, 50 }, { dlOn, 1 }, { dlTime, 3 }, { dlMix, 25 }, { dlFb, 40 } } },
        { "ARP · Dark Electro", { { aWave, 0 }, { bWave, 1 }, { bOct, -1 }, { bLevel, 50 }, { fType, 2 }, { cutoff, 1100 }, { reso, 40 }, { fEnv, 45 }, { fD, 180 }, { fS, 10 },
            { dAmount, 20 }, { arpOn, 1 }, { arpMode, 2 }, { arpRate, 8 }, { arpOct, 2 }, { arpGate, 60 }, { aR, 100 } } },
        { "PLUCK · Glass Lines", { { aWave, 2 }, { bWave, 3 }, { bOct, 1 }, { bLevel, 40 }, { cutoff, 5000 }, { fEnv, 20 }, { aD, 300 }, { aS, 0 }, { aR, 300 },
            { dlOn, 1 }, { dlTime, 5 }, { dlFb, 45 }, { dlMix, 30 }, { rvOn, 1 }, { rvSize, 50 }, { rvMix, 25 } } },

        // ---------- SAX (фізична модель) ----------
        { "SAX · Baritone Classic", { { engine, 1 }, { mode, 2 }, { glide, 40 }, { saxBreath, 60 }, { saxReed, 50 }, { saxAir, 30 }, { saxBody, 60 }, { saxScoop, 25 },
            { vibAuto, 35 }, { vibRate, 5.0f }, { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aD, 1000 }, { aS, 100 }, { aR, 150 }, { ampVel, 0 },
            { mSrc[0], 5 }, { mDst[0], 15 }, { mAmt[0], 50 }, { mSrc[1], 6 }, { mDst[1], 14 }, { mAmt[1], 30 }, { rvOn, 1 }, { rvSize, 35 }, { rvMix, 15 }, { drift, 0 } } },
        { "SAX · Bari Growl", { { engine, 1 }, { mode, 2 }, { glide, 30 }, { saxBreath, 80 }, { saxReed, 70 }, { saxGrowl, 45 }, { saxAir, 25 }, { saxBody, 65 }, { saxScoop, 35 },
            { vibAuto, 20 }, { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 120 }, { ampVel, 0 },
            { rvOn, 1 }, { rvSize, 30 }, { rvMix, 12 }, { drift, 0 } } },
        { "SAX · Smoky Ballad", { { engine, 1 }, { mode, 2 }, { glide, 60 }, { saxBreath, 40 }, { saxReed, 30 }, { saxAir, 55 }, { saxBody, 70 }, { saxScoop, 30 },
            { vibAuto, 50 }, { vibRate, 4.6f }, { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 250 }, { ampVel, 0 },
            { rvOn, 1 }, { rvSize, 55 }, { rvMix, 25 }, { drift, 0 } } },
        { "SAX · Honker Riff", { { engine, 1 }, { mode, 2 }, { glide, 20 }, { saxBreath, 90 }, { saxReed, 85 }, { saxGrowl, 20 }, { saxAir, 20 }, { saxBody, 55 }, { saxScoop, 20 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 90 }, { ampVel, 0 }, { dAmount, 15 }, { dLow, 150 }, { drift, 0 } } },
        { "SAX · Bari Section (chords)", { { volume, -16 }, { engine, 1 }, { mode, 0 }, { saxBreath, 55 }, { saxReed, 45 }, { saxAir, 25 }, { saxBody, 60 }, { saxScoop, 15 }, { vibAuto, 25 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 180 }, { ampVel, 0 }, { chOn, 1 }, { chDepth, 25 }, { chMix, 30 },
            { rvOn, 1 }, { rvSize, 45 }, { rvMix, 20 }, { drift, 15 } } },
        { "SAX · Dark Electro Sax", { { engine, 1 }, { mode, 2 }, { glide, 40 }, { saxBreath, 70 }, { saxReed, 60 }, { saxGrowl, 15 }, { saxBody, 50 }, { vibAuto, 20 },
            { fType, 2 }, { cutoff, 2200 }, { reso, 35 }, { fEnv, 0 }, { fKey, 30 }, { fVel, 0 }, { l1Sync, 1 }, { l1Div, 6 }, { mSrc[0], 1 }, { mDst[0], 7 }, { mAmt[0], 20 },
            { aA, 1 }, { aS, 100 }, { aR, 150 }, { ampVel, 0 }, { dAmount, 25 }, { dlOn, 1 }, { dlTime, 3 }, { dlMix, 22 }, { dlFb, 35 } } },

        // ---------- AMBIENT / MEDITATION ----------
        { "AMB · Tibetan Bowl", { { engine, 2 }, { bowlType, 0 }, { bowlDecay, 85 }, { bowlHard, 35 }, { bowlBeat, 45 }, { bowlBright, 45 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aD, 20000 }, { aS, 100 }, { aR, 15000 }, { ampVel, 30 },
            { rvOn, 1 }, { rvSize, 70 }, { rvMix, 35 }, { drift, 0 } } },
        { "AMB · Singing Bowl (rubbed)", { { engine, 2 }, { bowlType, 0 }, { bowlDecay, 90 }, { bowlHard, 20 }, { bowlRub, 60 }, { bowlBeat, 55 }, { bowlBright, 40 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 15000 }, { rvOn, 1 }, { rvSize, 80 }, { rvMix, 40 }, { drift, 0 } } },
        { "AMB · Crystal Bowl Drone", { { engine, 2 }, { bowlType, 1 }, { bowlDecay, 95 }, { bowlHard, 10 }, { bowlRub, 70 }, { bowlBeat, 30 }, { bowlBright, 45 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 400 }, { aS, 100 }, { aR, 15000 },
            { rvOn, 1 }, { rvSize, 85 }, { rvMix, 45 }, { rvShimmer, 30 }, { drift, 0 } } },
        { "AMB · Temple Bell", { { volume, -12 }, { engine, 2 }, { bowlType, 2 }, { bowlDecay, 80 }, { bowlHard, 60 }, { bowlBeat, 35 }, { bowlBright, 50 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 15000 }, { rvOn, 1 }, { rvSize, 75 }, { rvMix, 35 }, { drift, 0 } } },
        { "AMB · Wind Chimes", { { engine, 2 }, { bowlType, 3 }, { bowlDecay, 60 }, { bowlHard, 70 }, { bowlBeat, 20 }, { bowlBright, 65 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 8000 }, { dlOn, 1 }, { dlTime, 5 }, { dlFb, 30 }, { dlMix, 20 },
            { rvOn, 1 }, { rvSize, 70 }, { rvMix, 35 }, { drift, 0 } } },
        { "AMB · Kalimba Dreams", { { engine, 2 }, { bowlType, 4 }, { bowlDecay, 40 }, { bowlHard, 50 }, { bowlBright, 50 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 3000 }, { dlOn, 1 }, { dlTime, 3 }, { dlFb, 40 }, { dlMix, 28 },
            { rvOn, 1 }, { rvSize, 50 }, { rvMix, 25 }, { drift, 0 } } },
        { "AMB · Glass Rain", { { volume, -12 }, { engine, 2 }, { bowlType, 5 }, { bowlDecay, 50 }, { bowlHard, 80 }, { bowlBright, 70 },
            { cutoff, 20000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 }, { reso, 0 }, { aA, 1 }, { aS, 100 }, { aR, 6000 }, { rvOn, 1 }, { rvSize, 70 }, { rvMix, 40 }, { rvShimmer, 60 }, { drift, 0 } } },
        { "AMB · Distant Bells Shimmer", { { engine, 2 }, { bowlType, 2 }, { bowlDecay, 70 }, { bowlHard, 30 }, { bowlBright, 40 }, { cutoff, 6000 }, { fEnv, 0 }, { fKey, 0 }, { fVel, 0 },
            { aA, 1 }, { aS, 100 }, { aR, 12000 }, { rvOn, 1 }, { rvSize, 95 }, { rvMix, 60 }, { rvShimmer, 70 }, { drift, 0 } } },
        { "AMB · Shimmer Pad", { { volume, -13 }, { aWave, 4 }, { aDetune, 20 }, { aShape, 40 }, { bWave, 2 }, { bOct, 1 }, { bLevel, 40 }, { fType, 1 }, { cutoff, 1800 }, { fEnv, 10 },
            { aA, 2500 }, { aD, 3000 }, { aS, 90 }, { aR, 6000 }, { chOn, 1 }, { rvOn, 1 }, { rvSize, 90 }, { rvMix, 55 }, { rvShimmer, 60 }, { drift, 30 } } },
        { "AMB · Deep Drone", { { volume, -12 }, { aWave, 0 }, { bWave, 0 }, { bOct, -1 }, { bLevel, 70 }, { bFine, 3 }, { subLevel, 50 }, { cutoff, 380 }, { reso, 30 }, { fEnv, 0 },
            { l2Rate, 0.05f }, { mSrc[0], 2 }, { mDst[0], 7 }, { mAmt[0], 25 }, { aA, 4000 }, { aS, 100 }, { aR, 8000 }, { rvOn, 1 }, { rvSize, 95 }, { rvMix, 50 }, { drift, 40 } } },
        { "AMB · Ocean Breath", { { aLevel, 0 }, { noise, 80 }, { fType, 3 }, { cutoff, 600 }, { reso, 30 }, { fEnv, 0 }, { fKey, 0 }, { l2Rate, 0.08f }, { l2Shape, 0 },
            { mSrc[0], 2 }, { mDst[0], 7 }, { mAmt[0], 45 }, { mSrc[1], 2 }, { mDst[1], 10 }, { mAmt[1], 35 }, { aA, 3000 }, { aS, 100 }, { aR, 6000 },
            { rvOn, 1 }, { rvSize, 90 }, { rvMix, 60 } } },
        { "AMB · Frozen Cloud", { { volume, -14 }, { aWave, 4 }, { aDetune, 30 }, { aShape, 70 }, { bWave, 3 }, { bOct, 1 }, { bLevel, 30 }, { fType, 1 }, { cutoff, 2400 }, { fEnv, 0 },
            { aA, 1500 }, { aS, 100 }, { aR, 5000 }, { chOn, 1 }, { rvOn, 1 }, { rvSize, 100 }, { rvMix, 70 }, { rvShimmer, 40 }, { drift, 30 } } },
        { "AMB · Meditation Om", { { volume, -14 }, { aWave, 3 }, { aShape, 15 }, { bWave, 2 }, { bOct, -1 }, { bLevel, 55 }, { bFine, 2 }, { subLevel, 30 }, { fType, 1 }, { cutoff, 700 }, { fEnv, 0 },
            { vibAuto, 15 }, { vibRate, 4.0f }, { aA, 3000 }, { aS, 100 }, { aR, 7000 }, { rvOn, 1 }, { rvSize, 85 }, { rvMix, 45 }, { drift, 25 } } },
        { "AMB · Aurora Sweep", { { volume, -12 }, { aWave, 0 }, { aUni, 5 }, { aDetune, 35 }, { aSpread, 100 }, { cutoff, 600 }, { reso, 45 }, { fEnv, 0 }, { l2Rate, 0.04f },
            { mSrc[0], 2 }, { mDst[0], 7 }, { mAmt[0], 45 }, { aA, 2500 }, { aS, 100 }, { aR, 6000 }, { chOn, 1 }, { rvOn, 1 }, { rvSize, 90 }, { rvMix, 50 }, { rvShimmer, 50 }, { drift, 30 } } },
        { "AMB · Warm Breath Pad", { { volume, -12 }, { aWave, 2 }, { bWave, 3 }, { bOct, 1 }, { bLevel, 35 }, { noise, 25 }, { fType, 1 }, { cutoff, 1100 }, { fA, 2000 }, { fD, 4000 }, { fS, 40 }, { fEnv, 25 },
            { aA, 1800 }, { aS, 100 }, { aR, 4000 }, { chOn, 1 }, { rvOn, 1 }, { rvSize, 75 }, { rvMix, 40 }, { drift, 20 } } },
        { "AMB · Slow Arp Stars", { { aWave, 2 }, { bWave, 3 }, { bSemi, 7 }, { bOct, 1 }, { bLevel, 35 }, { cutoff, 4000 }, { fEnv, 0 }, { aA, 5 }, { aD, 600 }, { aS, 0 }, { aR, 1500 },
            { arpOn, 1 }, { arpMode, 0 }, { arpRate, 6 }, { arpOct, 3 }, { arpGate, 40 }, { dlOn, 1 }, { dlTime, 6 }, { dlFb, 55 }, { dlMix, 30 },
            { rvOn, 1 }, { rvSize, 80 }, { rvMix, 40 }, { rvShimmer, 30 }, { drift, 15 } } },
    };
    return p;
}
