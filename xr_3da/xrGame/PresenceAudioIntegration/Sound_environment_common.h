/*
====================================================================================================
  Presence Audio SDK Integration for X-Ray Engine
  File: Sound_environment_common.h
====================================================================================================

  This header defines the data structure that represents the acoustic
  environment of the listener at a given frame.  It follows the EAX 2.0 /
  OpenAL EFX specification and is used to transfer reverb parameters from
  the Presence Audio SDK to the engine's sound driver.

  The structure is shared between the main thread (game logic) and the sound
  thread, therefore its members are updated atomically via the double‑buffer
  mechanism implemented elsewhere.

====================================================================================================

  Copyright (c) 2025 Presence Collaboratory, NSDeathman & Gemini 3

====================================================================================================

  Developed by: NSDeathman (Architecture & Core), Gemini 3 (Optimization & Math)
  Organization: Presence Collaboratory
====================================================================================================
*/

#pragma once

// Include engine base types (Fvector, u32, etc.) if needed.
#include "../../xr_collide_defs.h"

// =================================================================================================
// EAX ENVIRONMENT DATA STRUCTURE
// =================================================================================================
/**
 * @brief Stores all acoustic properties for a single listener position.
 *
 * The fields map directly to the EAX 2.0 / OpenAL EFX listener properties.
 * Volume levels are expressed in **millibels (mB)**: 0 mB = full scale,
 * -10000 mB = silence.  Time constants are in seconds, distances in metres.
 *
 * The structure is designed to be copied as a whole when the engine's
 * sound renderer applies a new environment.  The `dwFlags` member tells
 * the driver which parameters contain valid data.
 */
struct SEAXEnvironmentData
{
    // =============================================================================================
    // Master Volume & Tone
    // =============================================================================================

    /** Overall reverb volume.  Range: [-10000, 0] mB. */
    LONG lRoom;

    /** High‑frequency attenuation (affects brightness).  Range: [-10000, 0] mB. */
    LONG lRoomHF;

    /**
     * Distance‑based rolloff factor for the reverb effect.
     * 0.0 = no rolloff, 10.0 = very fast falloff.
     */
    float flRoomRolloffFactor;

    // =============================================================================================
    // Decay
    // =============================================================================================

    /**
     * Reverberation decay time (RT60) – the time in seconds for the
     * reverb to drop by 60 dB.  Valid range: [0.1, 20.0].
     */
    float flDecayTime;

    /**
     * Ratio of high‑frequency decay time to low‑frequency decay time.
     * < 1.0: high frequencies die faster (natural).
     * > 1.0: high frequencies persist longer (synthetic).
     * Valid range: [0.1, 2.0].
     */
    float flDecayHFRatio;

    // =============================================================================================
    // Early Reflections
    // =============================================================================================

    /** Volume of the first discrete echoes.  Range: [-10000, 1000] mB. */
    LONG lReflections;

    /**
     * Delay before the first reflection reaches the listener.
     * Depends on room size.  Range: [0.0, 0.3] seconds.
     */
    float flReflectionsDelay;

    // =============================================================================================
    // Late Reverberation Tail
    // =============================================================================================

    /** Volume of the diffuse reverb tail.  Range: [-10000, 2000] mB. */
    LONG lReverb;

    /**
     * Delay of the late reverb relative to the early reflections.
     * Range: [0.0, 0.1] seconds.
     */
    float flReverbDelay;

    // =============================================================================================
    // Environment Properties
    // =============================================================================================

    /**
     * Perceived size of the acoustic space in metres.
     * Range: [1.0, 100.0].
     */
    float flEnvironmentSize;

    /**
     * Echo density (diffusion).
     * 1.0 = dense, smooth reverb (bathroom).
     * 0.0 = sparse, grainy echoes (stadium).
     * Range: [0.0, 1.0].
     */
    float flEnvironmentDiffusion;

    /**
     * High‑frequency absorption caused by air (humidity, fog).
     * Range: [-100, 0] (0 = no extra absorption).
     */
    float flAirAbsorptionHF;

    // =============================================================================================
    // System Flags & State
    // =============================================================================================

    /**
     * Bitmask indicating which EAX parameters are valid.
     * Typical value: 0x3F (almost all parameters active).
     */
    DWORD dwFlags;

    /**
     * Engine frame stamp when the data was computed.
     * Used for synchronisation between the main and sound threads.
     */
    u32 dwFrameStamp;

    /** Set to true when the structure contains freshly computed valid data. */
    bool bDataValid;

    // =============================================================================================
    // Methods
    // =============================================================================================

    /**
     * @brief Constructor – initialises the structure with neutral defaults.
     */
    SEAXEnvironmentData()
    {
        Reset();
    }

    /**
     * @brief Resets all parameters to their default values.
     *
     * The defaults correspond to a generic medium‑sized room with moderate
     * reflections and a natural decay.
     */
    void Reset()
    {
        lRoom = -1000;
        lRoomHF = -100;
        flRoomRolloffFactor = 0.0f;
        flDecayTime = 1.49f;
        flDecayHFRatio = 0.83f;
        lReflections = -2602;
        flReflectionsDelay = 0.007f;
        lReverb = 200;
        flReverbDelay = 0.011f;
        flEnvironmentSize = 7.5f;
        flEnvironmentDiffusion = 1.0f;
        flAirAbsorptionHF = -5.0f;
        dwFlags = 0x00000001 | 0x00000002 | 0x00000004 |
            0x00000008 | 0x00000010 | 0x00000020;
        dwFrameStamp = 0;
        bDataValid = false;
    }

    /**
     * @brief Copies all fields from another environment data structure.
     *
     * This is used when swapping the double‑buffer between threads.
     */
    void CopyFrom(const SEAXEnvironmentData& other)
    {
        lRoom = other.lRoom;
        lRoomHF = other.lRoomHF;
        flRoomRolloffFactor = other.flRoomRolloffFactor;
        flDecayTime = other.flDecayTime;
        flDecayHFRatio = other.flDecayHFRatio;
        lReflections = other.lReflections;
        flReflectionsDelay = other.flReflectionsDelay;
        lReverb = other.lReverb;
        flReverbDelay = other.flReverbDelay;
        flEnvironmentSize = other.flEnvironmentSize;
        flEnvironmentDiffusion = other.flEnvironmentDiffusion;
        flAirAbsorptionHF = other.flAirAbsorptionHF;
        dwFlags = other.dwFlags;
        dwFrameStamp = other.dwFrameStamp;
        bDataValid = other.bDataValid;
    }
};
