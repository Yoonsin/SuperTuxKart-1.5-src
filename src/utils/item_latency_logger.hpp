//  SuperTuxKart - a fun racing game with go-kart
//
//  [item-latency] Item-use latency measurement for experiments.
//  Everything in this file is disabled unless this command line flag
//  is given:
//      --item-test[=DIR]   every bonus box gives exactly 1 bowling ball,
//                          the player fires it by hand, and each
//                          fire-input -> projectile-launch is written to
//                          item_latency_<track>_<time>.csv (in DIR if
//                          given, otherwise in the user config folder)
//  Without this flag STK behaves exactly like the original code.

#ifndef HEADER_ITEM_LATENCY_LOGGER_HPP
#define HEADER_ITEM_LATENCY_LOGGER_HPP

#include "items/powerup_manager.hpp"

#include <string>

class AbstractKart;

class ItemLatencyLogger
{
private:
    static bool        m_enabled;
    static std::string m_log_dir;

public:
    // ---- Flag (set once from main.cpp) ---------------------------------
    static void setEnabled(bool b)                     { m_enabled = b; }
    static void setLogDirectory(const std::string& d)  { m_log_dir = d; }
    /** True if --item-test is on. Every hook checks this. */
    static bool isActive()                         { return m_enabled; }

    // ---- Queries -------------------------------------------------------
    static bool isTargetKart(const AbstractKart* kart);
    static bool shouldForceBowling(const AbstractKart* kart)
                                { return m_enabled && isTargetKart(kart); }

    // ---- Hooks ---------------------------------------------------------
    /** item_manager.cpp: after a local player collected an item. */
    static void onBowlingCollected(AbstractKart* kart);
    /** player_controller.cpp: fire button pressed (T1). */
    static void onFireInput(AbstractKart* kart);
    /** projectile_manager.cpp: projectile created and fired (T2). */
    static void onProjectileCreated(AbstractKart* kart,
                                    PowerupManager::PowerupType type);
    /** main_loop.cpp: once per physics tick, before the race update. */
    static void onTick(bool race_active);
    /** main_loop.cpp: once per rendered frame. */
    static void onFrame(bool race_active);
    /** main_loop.cpp: when the main loop ends. */
    static void finish();
    /** Called by UseScope with the duration of Powerup::use(). */
    static void reportUseDuration(double duration_us);

    // ---- RAII scope for Powerup::use() ---------------------------------
    class UseScope
    {
    private:
        bool   m_active;
        double m_start_ms;
    public:
        UseScope(const AbstractKart* kart, PowerupManager::PowerupType type);
        ~UseScope();
    };
};   // ItemLatencyLogger

#endif
