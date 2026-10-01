//  SuperTuxKart - a fun racing game with go-kart
//
//  [fall-test] Track exit -> respawn measurement for experiments.
//  Everything in this file is disabled unless this command line flag
//  is given:
//      --fall-test[=DIR]
//  For the local player kart, only the FIRST rescue (respawn) of a race is
//  measured: from the tick the rescue starts (track exit detected) until
//  the rescue animation is over (kart can be controlled again).
//  The result is written to fall_respawn_<track>_<time>.csv
//  (in DIR if given, otherwise in the user config folder).
//  Without this flag STK behaves exactly like the original code.

#ifndef HEADER_FALL_TEST_LOGGER_HPP
#define HEADER_FALL_TEST_LOGGER_HPP

#include <string>

class AbstractKart;

class FallTestLogger
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
    /** kart.cpp: Kart::update(), right after updateSpeed(). */
    static void onKartUpdate(AbstractKart* kart, float speed);
    /** main_loop.cpp: once per rendered frame. */
    static void onFrame(bool race_active);
    /** main_loop.cpp: when the main loop ends. */
    static void finish();
};   // FallTestLogger

#endif
