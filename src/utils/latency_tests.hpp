//  SuperTuxKart - a fun racing game with go-kart
//
//  Local latency experiments. Each test is off unless its flag is given
//  (without the flags STK behaves like the original code). Results go to
//  DIR/<name>_<track>_<time>.csv (no DIR: user config folder).
//    --item-test[=DIR]    item_latency    fire input -> bowling ball launch
//    --wall-test[=DIR]    wall_recovery   wall crash -> back at max speed
//    --fall-test[=DIR]    fall_respawn    track exit -> respawn done
//    --banana-test[=DIR]  banana_debuff   banana hit -> speed starts to drop

#ifndef HEADER_LATENCY_TESTS_HPP
#define HEADER_LATENCY_TESTS_HPP

#include "items/powerup_manager.hpp"
#include "utils/vec3.hpp"

#include <sstream>
#include <string>
#include <vector>

class AbstractKart;

class ItemLatencyLogger
{
    struct Event
    {
        int    id = 0, input_tick = -1, launch_tick = -1, shown_tick = -1;
        double use_us = -1.0;
        std::string status;
    };

    static bool              m_enabled;
    static std::string       m_log_dir;
    static std::vector<Event> m_events;
    static Event              m_cur;
    static AbstractKart*      m_kart;      // NULL: not waiting for T2
    static int                m_wait;
    static double             m_use_start;
    static bool                m_race;

    static bool isTarget(const AbstractKart* kart);
    static void closeEvent(const char* status, bool keep = false);
public:
    static void enable(const std::string& dir) { m_enabled = true; m_log_dir = dir; }
    static bool isActive() { return m_enabled; }
    static bool shouldForceBowling(const AbstractKart* kart);
    static void onFireInput(AbstractKart* kart);
    static void onProjectileCreated(AbstractKart* kart,
                                    PowerupManager::PowerupType type);
    static void onUseStart(const AbstractKart* kart,
                           PowerupManager::PowerupType type);
    static void onUseEnd();
    static void onTick(bool race_active);
    static void onFrame(bool race_active);
};

class WallCrashLogger
{
    enum State { S_LEARN, S_IDLE, S_CRASHED, S_BACKING, S_RECOVERING };

    static constexpr float BACK_FACTOR       = 1.3f;  // reverse dist = accel_dist *
    static constexpr float BACK_MARGIN       = 2.0f;  // this + this margin
    static constexpr float DEFAULT_BACK_DIST = 30.0f; // used if accel_dist unknown
    static constexpr float TIMEOUT_SECONDS   = 20.0f; // give up an event after this
    static constexpr float TARGET_FRACTION   = 0.8f;  // target speed = max speed * this

    static bool        m_enabled;
    static std::string m_log_dir;
    static State              m_state;
    static AbstractKart*      m_kart;
    static std::ostringstream m_rows;
    static int                m_count;
    static bool               m_race, m_reset, m_start_set;
    static float              m_accel_dist;
    static Vec3               m_start_xyz, m_crash_xyz, m_last_xyz;
    static int                m_crash_tick, m_release_tick;
    static float              m_max_speed, m_back_dist;
    static bool               m_fast;        // crash at >= target speed
    static std::string        m_pending;     // ok row waiting for frameTick()
    static int                m_pending_start;
    static double             m_pending_crash_us;
    static double              m_crash_start_ms; // ms() at onCrash accept, <0: none this call
    static double              m_crash_us;        // Kart::crashed(Material,normal) time

    static void brake(bool on);
    static bool isMeasuring();
    static void closeEvent(const char* status, int recover_tick = -1);
public:
    static void enable(const std::string& dir) { m_enabled = true; m_log_dir = dir; }
    static bool isActive() { return m_enabled; }
    static void onCrash(AbstractKart* kart, const Vec3& normal,
                        float speed_before);
    /** T2 of crash_us: end of Kart::crashed(Material, normal). */
    static void onCrashEnd();
    static void onSpeedUpdate(AbstractKart* kart, float speed);
    static void onTick(bool race_active);
    static void onFrame(bool race_active);
};

class FallTestLogger
{
    enum State { S_DRIVING, S_RESCUING, S_DRIVING_ON, S_DONE };

    static constexpr float TIMEOUT_SECONDS       = 10.0f; // respawn too slow
    static constexpr float DRIVE_DISTANCE        = 10.0f; // drive on before saving
    static constexpr float DRIVE_TIMEOUT_SECONDS = 10.0f; // give up driving on

    static bool        m_enabled;
    static std::string m_log_dir;
    static State  m_state;
    static bool   m_race;
    static Vec3   m_respawn_xyz;
    static int    m_start_tick, m_respawn_ticks, m_shown_tick;
    static double m_rescue_start_ms; // ms() at onRescueBegin accept, <0: not measuring
    static double m_rescue_us;       // RescueAnimation(kart,bool) ctor time

    static void save(const std::string& dir, const char* status, bool quit);
public:
    static void enable(const std::string& dir) { m_enabled = true; m_log_dir = dir; }
    static bool isActive() { return m_enabled; }
    /** T1: called right where kart.cpp creates the RescueAnimation (track
     *  exit), not inferred later by polling - avoids a 1-tick-late T1. */
    static void onRescueStart(AbstractKart* kart);
    /** T1 of rescue_us: start of RescueAnimation(kart, bool) constructor. */
    static void onRescueBegin(AbstractKart* kart);
    /** T2 of rescue_us: end of RescueAnimation(kart, bool) constructor. */
    static void onRescueEnd();
    static void onKartUpdate(AbstractKart* kart, float speed);
    static void onFrame(bool race_active);
};

class BananaTestLogger
{
    struct Sample { int tick; float speed; };

    static constexpr float TARGET_FRACTION = 0.70f;  // "applied" speed threshold
    static constexpr float REACT_DROP      = 0.001f; // speed drop counted as "reacting"
    static constexpr float DISTURB_DROP    = 1.0f;   // bigger 1-tick drop = not the chute
    static constexpr float TIMEOUT_SECONDS = 15.0f;  // give up if chute still attached
    static constexpr float RECOVER_SECONDS = 15.0f;  // give up waiting for max speed

    static bool        m_enabled;
    static std::string m_log_dir;
    static std::vector<Sample> m_samples;
    static std::ostringstream  m_rows;
    static int                 m_count;
    static bool                m_race;
    static AbstractKart*       m_kart;        // NULL: not measuring
    static AbstractKart*       m_reset_kart, *m_recover_kart;
    static int                 m_recover_start, m_hit_tick;
    static double              m_apply_us;
    static float               m_speed_before;
    static std::vector<int>    m_frame_ticks; // frameTick() of each frame

    static void closeEvent(std::string status);
public:
    static void enable(const std::string& dir) { m_enabled = true; m_log_dir = dir; }
    static bool isActive() { return m_enabled; }
    /** Returns true if the banana was handled here (parachute attached). */
    static bool onBananaHit(AbstractKart* kart, float speed);
    static void onKartUpdate(AbstractKart* kart, float speed);
    static void onTick(bool race_active);
    static void onFrame(bool race_active);
};

#endif
