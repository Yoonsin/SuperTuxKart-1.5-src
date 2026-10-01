//  SuperTuxKart - a fun racing game with go-kart
//
//  [banana-test] Banana hit -> parachute slowdown measurement.
//  Everything in this file is disabled unless this command line flag
//  is given:
//      --banana-test[=DIR]
//  For the local player kart:
//   - a banana always attaches a parachute (instead of a random
//     bomb / anvil / parachute),
//   - the time from the banana hit until the kart speed has dropped to
//     70% of the speed at the hit (parachute slowdown) is measured,
//   - results are written to banana_debuff_<track>_<time>.csv
//     (in DIR if given, otherwise in the user config folder).
//  Same conditions for every hit: after every measured hit the kart drives
//  on until it is back at max speed, then it is reset to its start
//  position, so every hit is the same (first) banana
//  at the same spot, speed and ticks. This repeats until the race ends
//  (use --profile-time=N to set the length of the run).
//  Without this flag STK behaves exactly like the original code.

#ifndef HEADER_BANANA_TEST_LOGGER_HPP
#define HEADER_BANANA_TEST_LOGGER_HPP

#include <string>

class AbstractKart;

class BananaTestLogger
{
private:
    static bool        m_enabled;
    static std::string m_log_dir;

public:
    // ---- Flag (set once from main.cpp) ---------------------------------
    static void setEnabled(bool b)                     { m_enabled = b; }
    static void setLogDirectory(const std::string& d)  { m_log_dir = d; }
    static bool isActive()                         { return m_enabled; }

    // ---- Hooks ---------------------------------------------------------
    /** kart.cpp: Kart::collectedItem(), banana case. Returns true if the
     *  banana was handled here (parachute attached), false to run the
     *  original banana code. */
    static bool onBananaHit(AbstractKart* kart, float speed);
    /** kart.cpp: Kart::update(), right after updateSpeed(). */
    static void onKartUpdate(AbstractKart* kart, float speed);
    /** main_loop.cpp: once per physics tick, before the race update.
     *  Resets the kart to its start position when requested. */
    static void onTick(bool race_active);
    /** main_loop.cpp: once per rendered frame. */
    static void onFrame(bool race_active);
    /** main_loop.cpp: when the main loop ends. */
    static void finish();
};   // BananaTestLogger

#endif
