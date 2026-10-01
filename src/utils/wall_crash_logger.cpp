//  SuperTuxKart - a fun racing game with go-kart
//
//  [wall-test] See wall_crash_logger.hpp. All code here only runs
//  when --wall-test is given.

#include "utils/wall_crash_logger.hpp"

#include "config/stk_config.hpp"
#include "input/input.hpp"
#include "io/file_manager.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "karts/kart_properties.hpp"
#include "modes/world.hpp"
#include "network/rewind_manager.hpp"
#include "tracks/track.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"
#include "utils/profiler.hpp"   // getTimeMilliseconds()
#include "utils/time.hpp"
#include "utils/vec3.hpp"

#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <vector>

bool        WallCrashLogger::m_enabled = false;
std::string WallCrashLogger::m_log_dir = "";

// ============================================================================
namespace
{
    /** Speed counts as "max speed" at or above this fraction of the kart's
     *  current max speed (float rounding: bullet caps the velocity at the
     *  max speed, but the value is rarely bit-exact). */
    const float MAX_SPEED_TOLERANCE = 0.995f;
    /** Reverse distance = accel_distance * factor + margin. */
    const float BACK_FACTOR         = 1.3f;
    const float BACK_MARGIN         = 2.0f;
    /** Used if the kart never reached max speed before the first crash. */
    const float DEFAULT_BACK_DIST   = 30.0f;
    /** Give up an event after this many seconds. */
    const float TIMEOUT_SECONDS     = 20.0f;

    enum State
    {
        S_LEARN,        // race start: measuring accel_distance
        S_IDLE,         // driving, waiting for a wall crash
        S_CRASHED,      // crash seen, press brake on the next tick
        S_BACKING,      // reversing away from the wall
        S_RECOVERING    // brake released, waiting for max speed
    };

    struct Record
    {
        int         id            = 0;
        int         crash_tick    = -1;
        int         release_tick  = -1;
        int         recover_tick  = -1;
        double      crash_ms      = 0.0;
        double      release_ms    = 0.0;
        float       speed_ratio   = 0.0f;   // crash speed / max speed
        float       front_dot     = 0.0f;   // -1 = head-on
        bool        at_max        = false;
        float       max_speed     = 0.0f;
        float       back_distance = 0.0f;
        double      recovery_ms   = -1.0;
        double      total_ms      = -1.0;
        std::string status;
    };

    State               g_state          = S_LEARN;
    AbstractKart*       g_kart           = NULL;
    Record              g_current;
    std::vector<Record> g_records;
    int                 g_next_id        = 1;
    bool                g_race_active    = false;
    std::string         g_track          = "unknown";

    Vec3                g_start_xyz;
    bool                g_start_set      = false;
    float               g_accel_distance = -1.0f;
    Vec3                g_crash_xyz;
    Vec3                g_last_xyz;       // for reset (teleport) check
    AbstractKart*       g_target_kart    = NULL;
    bool                g_pending_reset  = false;

    // ------------------------------------------------------------------------
    int currentTick()
    {
        World* w = World::getWorld();
        return w ? w->getTicksSinceStart() : -1;
    }   // currentTick

    // ------------------------------------------------------------------------
    bool isTargetKart(AbstractKart* kart)
    {
        return kart && kart->getController() &&
               kart->getController()->isLocalPlayerController();
    }   // isTargetKart

    // ------------------------------------------------------------------------
    void pressBrake(bool pressed)
    {
        if (g_kart && g_kart->getController())
        {
            g_kart->getController()->action(PA_BRAKE,
                                            pressed ? Input::MAX_VALUE : 0);
        }
    }   // pressBrake

    // ------------------------------------------------------------------------
    float backDistance()
    {
        return g_accel_distance > 0.0f
             ? g_accel_distance * BACK_FACTOR + BACK_MARGIN
             : DEFAULT_BACK_DIST;
    }   // backDistance

    // ------------------------------------------------------------------------
    void closeCurrent(const char* status)
    {
        g_current.status = status;
        g_records.push_back(g_current);
        g_state = S_IDLE;
        // Same start condition for the next crash (see onTick)
        if (std::string(status) != "race_end")
            g_pending_reset = true;
    }   // closeCurrent

    // ------------------------------------------------------------------------
    void resetAll()
    {
        g_state          = S_LEARN;
        g_kart           = NULL;
        g_current        = Record();
        g_start_set      = false;
        g_accel_distance = -1.0f;
        g_target_kart    = NULL;
        g_pending_reset  = false;
    }   // resetAll

    // ------------------------------------------------------------------------
    void writeFile(const std::string& dir)
    {
        if (g_records.empty())
        {
            Log::info("WallCrash", "No wall crashes recorded in this race.");
            return;
        }

        StkTime::TimeType t = StkTime::getTimeSinceEpoch();
        struct tm* now = std::localtime(&t);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", now);
        const std::string name = "wall_recovery_" + g_track + "_" +
                                 std::string(ts) + ".csv";

        std::string path;
        if (dir.empty())
        {
            path = file_manager->getUserConfigFile(name);
        }
        else
        {
            file_manager->checkAndCreateDirectoryP(dir);
            path = dir + "/" + name;
        }

        std::ofstream out(FileUtils::getPortableWritingPath(path));
        if (!out.is_open())
        {
            Log::error("WallCrash", "Cannot open '%s'", path.c_str());
            return;
        }

        // status        : use only "ok" rows
        // total_ticks    : crash -> max speed again, physics ticks
        //                  (incl. reverse)
        // recovery_ticks : brake released -> max speed again, physics ticks
        // total_ms       : same as total_ticks in real time
        // recovery_ms    : same as recovery_ticks in real time
        // speed_ratio    : crash speed / max speed (1.000 = max speed)
        // front_dot      : crash angle (-1 = head-on)
        out << std::fixed << std::setprecision(3);
        out << "event_id,status,total_ticks,recovery_ticks,"
               "total_ms,recovery_ms,"
               "speed_ratio,front_dot\n";
        for (const Record& r : g_records)
        {
            const bool ok = r.recover_tick >= 0 && r.release_tick >= 0;
            out << r.id                                              << ","
                << r.status                                          << ","
                << (ok ? r.recover_tick - r.crash_tick : -1)         << ","
                << (ok ? r.recover_tick - r.release_tick : -1)       << ","
                << (ok ? r.total_ms : -1.0)                          << ","
                << (ok ? r.recovery_ms : -1.0)                       << ","
                << r.speed_ratio                                     << ","
                << r.front_dot                                       << "\n";
        }
        out.close();
        Log::info("WallCrash", "Saved wall recovery times to '%s'",
                  path.c_str());
        g_records.clear();
    }   // writeFile
}   // namespace

// ============================================================================
void WallCrashLogger::onCrash(AbstractKart* kart, const Vec3& normal,
                              float speed_before)
{
    if (!m_enabled || !g_race_active || !isTargetKart(kart))
        return;
    // Only a new crash while driving forward starts an event
    if (g_state != S_IDLE && g_state != S_LEARN)
        return;
    // Kart is about to be reset to the start position (see onTick)
    if (g_pending_reset)
        return;
    if (RewindManager::get()->isRewinding())
        return;
    World* w = World::getWorld();
    if (!w || !w->isActiveRacePhase())
        return;

    // Ignore floor / ceiling hits (e.g. landing after a jump)
    const btVector3 up = kart->getTrans().getBasis().getColumn(1);
    if (std::fabs(normal.dot(up)) > 0.7f)
        return;
    // Ignore touching a wall while (almost) standing still
    if (speed_before < 1.0f)
        return;

    const float max_speed = kart->getCurrentMaxSpeed();

    g_kart                  = kart;
    g_current               = Record();
    g_current.id            = g_next_id++;
    g_current.crash_tick    = currentTick();
    g_current.crash_ms      = getTimeMilliseconds();
    g_current.speed_ratio   = max_speed > 0.0f ? speed_before / max_speed
                                               : 0.0f;
    g_current.front_dot     = kart->getTrans().getBasis().getColumn(2)
                                  .dot(normal);
    g_current.max_speed     = max_speed;
    g_current.at_max        = speed_before >= max_speed * MAX_SPEED_TOLERANCE;
    g_current.back_distance = backDistance();
    g_crash_xyz             = kart->getXYZ();
    g_last_xyz              = g_crash_xyz;
    g_state                 = S_CRASHED;
}   // onCrash

// ----------------------------------------------------------------------------
void WallCrashLogger::onSpeedUpdate(AbstractKart* kart, float speed)
{
    if (!m_enabled || !g_race_active || !isTargetKart(kart))
        return;
    if (RewindManager::get()->isRewinding())
        return;
    World* w = World::getWorld();
    if (!w || !w->isActiveRacePhase())
        return;
    g_target_kart = kart;
    // Wait until the requested reset is done (see onTick)
    if (g_pending_reset)
        return;

    // Drive forward by itself (does not depend on the input device, so it
    // also works on Android with --race-now). Not while reversing.
    if ((g_state == S_LEARN || g_state == S_IDLE || g_state == S_RECOVERING)
        && kart->getController())
        kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);

    switch (g_state)
    {
    case S_LEARN:
        // Distance from standstill to max speed at race start
        if (!g_start_set && speed > 0.2f)
        {
            g_start_xyz = kart->getXYZ();
            g_start_set = true;
        }
        if (g_start_set &&
            speed >= kart->getCurrentMaxSpeed() * MAX_SPEED_TOLERANCE)
        {
            g_accel_distance = (kart->getXYZ() - g_start_xyz).length();
            Log::info("WallCrash", "Accel distance to max speed: %.2f m, "
                      "reverse distance: %.2f m",
                      g_accel_distance, backDistance());
            g_state = S_IDLE;
            // Every crash starts from the start position (see onTick)
            g_pending_reset = true;
        }
        return;

    case S_IDLE:
        return;

    case S_CRASHED:
    case S_BACKING:
    case S_RECOVERING:
        // A kart reset (e.g. goal in soccer, rescue) teleports the kart:
        // more than 3 m in one physics tick is impossible while driving.
        if ((kart->getXYZ() - g_last_xyz).length() > 3.0f)
        {
            if (g_state == S_BACKING)
                pressBrake(false);
            closeCurrent("reset");
            return;
        }
        g_last_xyz = kart->getXYZ();
        break;
    }

    switch (g_state)
    {
    case S_LEARN:
    case S_IDLE:
        return;

    case S_CRASHED:
        g_kart = kart;
        pressBrake(true);   // start reversing
        g_state = S_BACKING;
        break;

    case S_BACKING:
        if ((kart->getXYZ() - g_crash_xyz).length() >=
            g_current.back_distance)
        {
            // Accel (held key or --auto-accel) takes over again
            pressBrake(false);
            g_current.release_tick = currentTick();
            g_current.release_ms   = getTimeMilliseconds();
            g_state = S_RECOVERING;
        }
        else
        {
            // Re-press every tick: a held accel key (key repeat) would
            // otherwise cancel the brake and stop the reversing.
            pressBrake(true);
        }
        break;

    case S_RECOVERING:
        if (speed >= g_current.max_speed * MAX_SPEED_TOLERANCE)
        {
            g_current.recover_tick = currentTick();
            const double now_ms    = getTimeMilliseconds();
            g_current.recovery_ms  = now_ms - g_current.release_ms;
            g_current.total_ms     = now_ms - g_current.crash_ms;
            closeCurrent(g_current.at_max ? "ok" : "not_max");
            return;
        }
        break;
    }

    if (currentTick() - g_current.crash_tick >
        stk_config->time2Ticks(TIMEOUT_SECONDS))
    {
        if (g_state == S_BACKING)
            pressBrake(false);
        closeCurrent("timeout");
    }
}   // onSpeedUpdate

// ----------------------------------------------------------------------------
/** Puts the kart back to its start position, standing still, so that every
 *  crash happens with the same speed, angle and number of ticks. Done here
 *  (before the race update of this tick) and not inside Kart::update(). */
void WallCrashLogger::onTick(bool race_active)
{
    if (!m_enabled || !race_active || !g_pending_reset || !g_target_kart)
        return;
    World* w = World::getWorld();
    if (!w || !w->isActiveRacePhase())
        return;

    g_target_kart->reset();   // start position, speed 0, physics reset
    // The controller reset releases all keys: accelerate again
    if (g_target_kart->getController())
        g_target_kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);
    g_pending_reset = false;
}   // onTick

// ----------------------------------------------------------------------------
void WallCrashLogger::onFrame(bool race_active)
{
    if (!m_enabled)
        return;

    if (race_active && !g_race_active)
    {
        g_records.clear();
        g_next_id       = 1;
        resetAll();
        g_track = Track::getCurrentTrack() ?
                  Track::getCurrentTrack()->getIdent() : "unknown";
    }
    else if (!race_active && g_race_active)
    {
        if (g_state == S_CRASHED || g_state == S_BACKING ||
            g_state == S_RECOVERING)
            closeCurrent("race_end");
        writeFile(m_log_dir);
        resetAll();
    }
    g_race_active = race_active;
}   // onFrame

// ----------------------------------------------------------------------------
void WallCrashLogger::finish()
{
    if (!m_enabled)
        return;
    if (g_state == S_CRASHED || g_state == S_BACKING ||
        g_state == S_RECOVERING)
        closeCurrent("race_end");
    if (g_race_active)
        writeFile(m_log_dir);
    g_race_active = false;
}   // finish
