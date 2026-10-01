//  SuperTuxKart - a fun racing game with go-kart
//
//  [wall-test] Wall crash -> max speed recovery measurement.
//  Everything in this file is disabled unless this command line flag
//  is given:
//      --wall-test[=DIR]
//  For the local player kart (use together with --auto-accel):
//   1. At race start the distance the kart needs to go from standstill
//      to its max speed is measured (accel_distance).
//   2. When the kart hits a wall, it automatically reverses (brake key)
//      until it is accel_distance * 1.3 + 2m away from the crash point,
//      then releases the brake so --auto-accel drives it forward again.
//   3. The time from the crash until the kart is back at 100% of its max
//      speed is written to wall_recovery_<track>_<time>.csv
//      (in DIR if given, otherwise in the user config folder).
//  Crashes where the kart was not at max speed are still handled (so the
//  loop continues) but marked with status "not_max".
//  Same conditions for every crash: after the accel distance is known and
//  after every finished event, the kart is reset to its start position
//  (standing still), so every crash has the same speed, angle and ticks.
//  Without this flag STK behaves exactly like the original code.

#ifndef HEADER_WALL_CRASH_LOGGER_HPP
#define HEADER_WALL_CRASH_LOGGER_HPP

#include <string>

class AbstractKart;
class Vec3;

class WallCrashLogger
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
    /** kart.cpp: Kart::crashed(Material), before the crash reaction.
     *  speed_before is the kart speed of the last physics tick. */
    static void onCrash(AbstractKart* kart, const Vec3& normal,
                        float speed_before);
    /** kart.cpp: Kart::update(), right after updateSpeed(). */
    static void onSpeedUpdate(AbstractKart* kart, float speed);
    /** main_loop.cpp: once per physics tick, before the race update.
     *  Resets the kart to its start position when requested. */
    static void onTick(bool race_active);
    /** main_loop.cpp: once per rendered frame. */
    static void onFrame(bool race_active);
    /** main_loop.cpp: when the main loop ends. */
    static void finish();
};   // WallCrashLogger

#endif
