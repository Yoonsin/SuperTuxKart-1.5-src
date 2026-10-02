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

#include <string>

class AbstractKart;
class Vec3;

class ItemLatencyLogger
{
    static bool m_enabled;
    static std::string m_log_dir;
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
    static bool m_enabled;
    static std::string m_log_dir;
public:
    static void enable(const std::string& dir) { m_enabled = true; m_log_dir = dir; }
    static bool isActive() { return m_enabled; }
    static void onCrash(AbstractKart* kart, const Vec3& normal,
                        float speed_before);
    static void onSpeedUpdate(AbstractKart* kart, float speed);
    static void onTick(bool race_active);
    static void onFrame(bool race_active);
};

class FallTestLogger
{
    static bool m_enabled;
    static std::string m_log_dir;
public:
    static void enable(const std::string& dir) { m_enabled = true; m_log_dir = dir; }
    static bool isActive() { return m_enabled; }
    static void onKartUpdate(AbstractKart* kart, float speed);
    static void onFrame(bool race_active);
};

class BananaTestLogger
{
    static bool m_enabled;
    static std::string m_log_dir;
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
