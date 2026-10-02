//  SuperTuxKart - a fun racing game with go-kart
//
//  Local latency experiments, see latency_tests.hpp.

#include "utils/latency_tests.hpp"

#include "config/stk_config.hpp"
#include "input/input.hpp"
#include "io/file_manager.hpp"
#include "items/attachment.hpp"
#include "items/powerup.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "karts/kart_properties.hpp"
#include "karts/rescue_animation.hpp"
#include "main_loop.hpp"
#include "modes/world.hpp"
#include "network/rewind_manager.hpp"
#include "race/race_manager.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"
#include "utils/profiler.hpp"
#include "utils/time.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

bool        ItemLatencyLogger::m_enabled = false;
bool        WallCrashLogger::m_enabled   = false;
bool        FallTestLogger::m_enabled    = false;
bool        BananaTestLogger::m_enabled  = false;
std::string ItemLatencyLogger::m_log_dir, WallCrashLogger::m_log_dir,
            FallTestLogger::m_log_dir,    BananaTestLogger::m_log_dir;

namespace
{
    /** Bullet caps the speed at max speed, but rarely bit-exact. */
    const float MAX_SPEED_TOLERANCE = 0.995f;

    int  tick()  { return World::getWorld()->getTicksSinceStart(); }
    double ms()  { return getTimeMilliseconds(); }

    /** Local player kart in an active, not rewinding race. */
    bool isPlayerInRace(AbstractKart* kart)
    {
        return kart->getController() &&
               kart->getController()->isLocalPlayerController() &&
               !RewindManager::get()->isRewinding() &&
               World::getWorld()->isActiveRacePhase();
    }

    /** Writes dir/<prefix>_<track>_<time>.csv (no dir: user config folder). */
    void writeCsv(const char* prefix, const std::string& dir,
                  const char* header, const std::string& rows)
    {
        if (rows.empty()) return;
        StkTime::TimeType t = StkTime::getTimeSinceEpoch();
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", std::localtime(&t));
        const std::string name = std::string(prefix) + "_" +
            RaceManager::get()->getTrackName() + "_" + ts + ".csv";
        if (!dir.empty()) file_manager->checkAndCreateDirectoryP(dir);
        const std::string path = dir.empty()
            ? file_manager->getUserConfigFile(name) : dir + "/" + name;
        std::ofstream out(FileUtils::getPortableWritingPath(path));
        if (!out.is_open())
        {
            Log::error("LatencyTest", "Cannot open '%s'", path.c_str());
            return;
        }
        out << header << "\n" << rows;
        Log::info("LatencyTest", "Saved '%s'", path.c_str());
    }

    /** Clears the rows of a race, 3 decimals for floating point values. */
    void clearRows(std::ostringstream& rows)
    {
        rows.str("");
        rows << std::fixed << std::setprecision(3);
    }
}   // namespace

// ============================================================================
// --item-test: fire input (T1) -> bowling ball launch (T2), and the
// processing time of Powerup::use().
namespace
{
namespace item
{
    struct Event
    {
        int    id = 0, input_tick = -1, launch_tick = -1;
        double input_ms = 0.0, launch_ms = 0.0, use_us = -1.0;
        std::string status;
    };
    std::vector<Event> g_events;
    Event              g_cur;
    AbstractKart*      g_kart      = NULL;   // NULL: not waiting for T2
    int                g_wait      = 0;
    double             g_use_start = -1.0;
    bool               g_race      = false;

    /** Local player (fires by hand) or AI kart (fires in skidding_ai.cpp). */
    bool isTarget(const AbstractKart* kart)
    {
        const Controller* c = kart ? kart->getController() : NULL;
        return c && (c->isLocalPlayerController() || !c->isPlayerController());
    }

    void closeEvent(const char* status, bool keep = false)
    {
        g_cur.status = status;
        g_events.push_back(g_cur);
        if (!keep) g_cur = Event();
        g_kart = NULL;
        g_wait = 0;
    }
}   // namespace item
}   // namespace

bool ItemLatencyLogger::shouldForceBowling(const AbstractKart* kart)
{
    return m_enabled && item::isTarget(kart);
}

void ItemLatencyLogger::onFireInput(AbstractKart* kart)
{
    using namespace item;
    if (g_kart || !isTarget(kart) ||
        kart->getPowerup()->getType() != PowerupManager::POWERUP_BOWLING)
        return;
    g_cur            = Event();
    g_cur.id         = (int)g_events.size() + 1;
    g_cur.input_tick = tick();
    g_cur.input_ms   = ms();
    g_kart           = kart;
    g_wait           = 0;
}

void ItemLatencyLogger::onProjectileCreated(AbstractKart* kart,
                                            PowerupManager::PowerupType type)
{
    using namespace item;
    if (!g_kart || kart != g_kart || type != PowerupManager::POWERUP_BOWLING)
        return;
    g_cur.launch_tick = tick();
    g_cur.launch_ms   = ms();
    closeEvent("ok", /*keep for onUseEnd*/true);
}

void ItemLatencyLogger::onUseStart(const AbstractKart* kart,
                                   PowerupManager::PowerupType type)
{
    item::g_use_start = type == PowerupManager::POWERUP_BOWLING &&
                        item::isTarget(kart) ? ms() : -1.0;
}

void ItemLatencyLogger::onUseEnd()
{
    using namespace item;
    if (g_use_start >= 0.0 && !g_events.empty() &&
        g_events.back().id == g_cur.id && g_events.back().use_us < 0.0)
        g_events.back().use_us = (ms() - g_use_start) * 1000.0;
    g_use_start = -1.0;
}

void ItemLatencyLogger::onTick(bool race_active)
{
    using namespace item;
    if (!m_enabled) return;
    if (!race_active)
    {
        if (g_kart) closeEvent("race_end");
        g_cur = Event();
    }
    else if (g_kart && ++g_wait > stk_config->time2Ticks(1.0f))
        closeEvent("timeout");
}

void ItemLatencyLogger::onFrame(bool race_active)
{
    using namespace item;
    if (!m_enabled || race_active == g_race) return;
    g_race = race_active;
    std::ostringstream rows;
    clearRows(rows);
    for (const Event& e : g_events)
    {
        const bool ok = e.launch_tick >= 0;
        rows << e.id << "," << e.status << ","
             << (ok ? e.launch_tick - e.input_tick : -1) << ","
             << (ok ? (e.launch_ms - e.input_ms) * 1000.0 : -1.0) << ","
             << e.use_us << "\n";
    }
    if (!race_active)
        writeCsv("item_latency", m_log_dir,
                 "event_id,status,delta_ticks,delta_us,use_duration_us",
                 rows.str());
    g_events.clear();
}

// ============================================================================
// --wall-test: the distance from standstill to max speed is measured at race
// start. After a wall crash the kart reverses that distance * 1.3 + 2 m, then
// accelerates again: crash -> back at max speed. After every event the kart
// is reset to its start position.
namespace
{
namespace wall
{
    enum State { S_LEARN, S_IDLE, S_CRASHED, S_BACKING, S_RECOVERING };

    const float BACK_FACTOR       = 1.3f;   // reverse dist = accel_dist *
    const float BACK_MARGIN       = 2.0f;   // this + this margin
    const float DEFAULT_BACK_DIST = 30.0f;  // used if accel_dist unknown
    const float TIMEOUT_SECONDS   = 20.0f;  // give up an event after this

    State              g_state = S_LEARN;
    AbstractKart*      g_kart  = NULL;
    std::ostringstream g_rows;
    int                g_count = 0;
    bool               g_race = false, g_reset = false, g_start_set = false;
    float              g_accel_dist = -1.0f;
    Vec3               g_start_xyz, g_crash_xyz, g_last_xyz;
    int                g_crash_tick = -1, g_release_tick = -1;
    double             g_crash_ms = 0.0, g_release_ms = 0.0;
    float              g_speed_ratio = 0.0f, g_front_dot = 0.0f;
    float              g_max_speed = 0.0f, g_back_dist = 0.0f;
    bool               g_at_max = false;

    void brake(bool on)
    {
        g_kart->getController()->action(PA_BRAKE, on ? Input::MAX_VALUE : 0);
    }

    bool isMeasuring()
    {
        return g_state == S_CRASHED || g_state == S_BACKING ||
               g_state == S_RECOVERING;
    }

    /** recover_tick < 0: the kart did not get back to max speed. */
    void closeEvent(const char* status, int recover_tick = -1,
                    double now_ms = 0.0)
    {
        if (g_state == S_BACKING) brake(false);
        const bool ok = recover_tick >= 0;
        g_rows << ++g_count << "," << status << ","
               << (ok ? recover_tick - g_crash_tick   : -1) << ","
               << (ok ? recover_tick - g_release_tick : -1) << ","
               << (ok ? now_ms - g_crash_ms   : -1.0) << ","
               << (ok ? now_ms - g_release_ms : -1.0) << ","
               << g_speed_ratio << "," << g_front_dot << "\n";
        g_state = S_IDLE;
        g_reset = std::string(status) != "race_end";
    }
}   // namespace wall
}   // namespace

void WallCrashLogger::onCrash(AbstractKart* kart, const Vec3& normal,
                              float speed_before)
{
    using namespace wall;
    // Only a new crash into a wall (not floor / ceiling) while driving
    const btVector3 up = kart->getTrans().getBasis().getColumn(1);
    if (!g_race || isMeasuring() || g_reset || !isPlayerInRace(kart) ||
        std::fabs(normal.dot(up)) > 0.7f || speed_before < 1.0f)
        return;
    g_max_speed    = kart->getCurrentMaxSpeed();
    g_crash_tick   = tick();
    g_crash_ms     = ms();
    g_release_tick = -1;
    g_release_ms   = 0.0;
    g_speed_ratio  = g_max_speed > 0.0f ? speed_before / g_max_speed : 0.0f;
    g_front_dot    = kart->getTrans().getBasis().getColumn(2).dot(normal);
    g_at_max       = speed_before >= g_max_speed * MAX_SPEED_TOLERANCE;
    g_back_dist    = g_accel_dist > 0.0f
                   ? g_accel_dist * BACK_FACTOR + BACK_MARGIN
                   : DEFAULT_BACK_DIST;
    g_kart         = kart;
    g_crash_xyz    = g_last_xyz = kart->getXYZ();
    g_state        = S_CRASHED;
}

void WallCrashLogger::onSpeedUpdate(AbstractKart* kart, float speed)
{
    using namespace wall;
    if (!g_race || !isPlayerInRace(kart)) return;
    g_kart = kart;
    if (g_reset) return;

    // --auto-accel presses only once per race and the kart reset releases
    // all keys, so accelerate here (except while reversing)
    if (g_state != S_CRASHED && g_state != S_BACKING)
        kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);

    if (g_state == S_LEARN)
    {
        if (!g_start_set && speed > 0.2f)
        {
            g_start_xyz = kart->getXYZ();
            g_start_set = true;
        }
        if (g_start_set &&
            speed >= kart->getCurrentMaxSpeed() * MAX_SPEED_TOLERANCE)
        {
            g_accel_dist = (kart->getXYZ() - g_start_xyz).length();
            g_state = S_IDLE;
            g_reset = true;
        }
        return;
    }
    if (g_state == S_IDLE) return;

    // A rescue teleports the kart, more than 3 m in one tick
    if ((kart->getXYZ() - g_last_xyz).length() > 3.0f)
        return closeEvent("reset");
    g_last_xyz = kart->getXYZ();

    if (g_state == S_CRASHED)
    {
        brake(true);
        g_state = S_BACKING;
    }
    else if (g_state == S_BACKING)
    {
        const bool far_enough =
            (kart->getXYZ() - g_crash_xyz).length() >= g_back_dist;
        // Pressed again every tick, a key repeat of accel would cancel it
        brake(!far_enough);
        if (far_enough)
        {
            g_release_tick = tick();
            g_release_ms   = ms();
            g_state        = S_RECOVERING;
        }
    }
    else if (speed >= g_max_speed * MAX_SPEED_TOLERANCE)
        return closeEvent(g_at_max ? "ok" : "not_max", tick(), ms());
    if (tick() - g_crash_tick > stk_config->time2Ticks(TIMEOUT_SECONDS))
        closeEvent("timeout");
}

/** Done before the race update of this tick, not inside Kart::update(). */
void WallCrashLogger::onTick(bool race_active)
{
    using namespace wall;
    if (!m_enabled || !race_active || !g_reset || !g_kart ||
        !World::getWorld()->isActiveRacePhase())
        return;
    g_kart->reset();
    g_kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);
    g_reset = false;
}

void WallCrashLogger::onFrame(bool race_active)
{
    using namespace wall;
    if (!m_enabled || race_active == g_race) return;
    g_race = race_active;
    if (!race_active)
    {
        if (isMeasuring()) closeEvent("race_end");
        writeCsv("wall_recovery", m_log_dir, "event_id,status,total_ticks,"
                 "recovery_ticks,total_ms,recovery_ms,speed_ratio,front_dot",
                 g_rows.str());
    }
    clearRows(g_rows);
    g_count      = 0;
    g_state      = S_LEARN;
    g_kart       = NULL;
    g_start_set  = g_reset = false;
    g_accel_dist = -1.0f;
}

// ============================================================================
// --fall-test: the first rescue of a race, track exit -> rescue animation
// over. The kart then drives on 10 m, the result is saved and the game quits.
namespace
{
namespace fall
{
    enum State { S_DRIVING, S_RESCUING, S_DRIVING_ON, S_DONE };

    const float TIMEOUT_SECONDS       = 10.0f; // give up if respawn too slow
    const float DRIVE_DISTANCE        = 10.0f; // drive this far before saving
    const float DRIVE_TIMEOUT_SECONDS = 10.0f; // give up driving on

    State  g_state = S_DRIVING;
    bool   g_race  = false;
    float  g_last_speed = 0.0f, g_speed_before = 0.0f;
    Vec3   g_last_xyz, g_fall_xyz, g_respawn_xyz;
    int    g_start_tick = -1, g_respawn_ticks = -1;
    double g_start_ms = 0.0, g_respawn_ms = -1.0;

    void save(const std::string& dir, const char* status, bool quit)
    {
        std::ostringstream row;
        clearRows(row);
        row << status << "," << g_respawn_ticks << "," << g_respawn_ms << ","
            << g_speed_before << "," << g_fall_xyz.getX() << ","
            << g_fall_xyz.getZ() << "\n";
        writeCsv("fall_respawn", dir, "status,respawn_ticks,respawn_ms,"
                 "speed_before,fall_x,fall_z", row.str());
        g_state = S_DONE;
        if (quit) main_loop->requestAbort();
    }
}   // namespace fall
}   // namespace

void FallTestLogger::onKartUpdate(AbstractKart* kart, float speed)
{
    using namespace fall;
    if (!g_race || g_state == S_DONE || !isPlayerInRace(kart)) return;

    const bool rescuing =
        dynamic_cast<RescueAnimation*>(kart->getKartAnimation()) != NULL;
    if (!rescuing)
        kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);

    if (g_state == S_DRIVING && !rescuing)
    {
        if (!kart->getKartAnimation())
        {
            g_last_speed = speed;
            g_last_xyz   = kart->getXYZ();
        }
    }
    else if (g_state == S_DRIVING)
    {
        g_start_tick   = tick();
        g_start_ms     = ms();
        g_speed_before = g_last_speed;
        g_fall_xyz     = g_last_xyz;
        g_state        = S_RESCUING;
    }
    else if (g_state == S_RESCUING && !rescuing)
    {
        g_respawn_ticks = tick() - g_start_tick;
        g_respawn_ms    = ms() - g_start_ms;
        g_respawn_xyz   = kart->getXYZ();
        g_state         = S_DRIVING_ON;
    }
    else if (g_state == S_RESCUING)
    {
        if (tick() - g_start_tick > stk_config->time2Ticks(TIMEOUT_SECONDS))
            save(m_log_dir, "timeout", true);
    }
    else if ((kart->getXYZ() - g_respawn_xyz).length() >= DRIVE_DISTANCE)
        save(m_log_dir, "ok", true);
    else if (tick() - (g_start_tick + g_respawn_ticks) >
             stk_config->time2Ticks(DRIVE_TIMEOUT_SECONDS))
        save(m_log_dir, "no_drive", true);
}

void FallTestLogger::onFrame(bool race_active)
{
    using namespace fall;
    if (!m_enabled || race_active == g_race) return;
    g_race = race_active;
    if (!race_active)
    {
        if (g_state == S_RESCUING)   save(m_log_dir, "race_end", false);
        if (g_state == S_DRIVING_ON) save(m_log_dir, "no_drive", false);
        g_state = S_DONE;
        return;
    }
    g_state         = S_DRIVING;
    g_last_speed    = 0.0f;
    g_start_tick    = g_respawn_ticks = -1;
    g_respawn_ms    = -1.0;
}

// ============================================================================
// --banana-test: a banana always attaches a parachute. banana hit (T1) ->
// speed starts to drop (T2), and a check that the speed drops to 70%. After
// every hit the kart drives on until max speed, then it is reset to its
// start position.
namespace
{
namespace banana
{
    const float TARGET_FRACTION = 0.70f;  // "applied" speed threshold
    const float REACT_DROP      = 0.001f; // speed drop counted as "reacting"
    const float DISTURB_DROP    = 1.0f;   // bigger 1-tick drop = not the chute
    const float TIMEOUT_SECONDS = 15.0f;  // give up if chute still attached
    const float RECOVER_SECONDS = 15.0f;  // give up waiting for max speed

    struct Sample { int tick; double ms; float speed; };

    std::vector<Sample> g_samples;
    std::ostringstream  g_rows;
    int                 g_count = 0;
    bool                g_race  = false;
    AbstractKart*       g_kart  = NULL;   // NULL: not measuring
    AbstractKart*       g_reset_kart = NULL, *g_recover_kart = NULL;
    int                 g_recover_start = -1, g_hit_tick = -1;
    double              g_hit_ms = 0.0, g_apply_us = -1.0;
    float               g_speed_before = 0.0f;

    void closeEvent(std::string status)
    {
        float  speed_min  = g_samples.empty() ? 0.0f : g_samples[0].speed;
        int    react_tick = -1;
        double react_ms   = 0.0;
        const char* result = NULL;
        float prev = g_speed_before;
        for (const Sample& s : g_samples)
        {
            speed_min = std::min(speed_min, s.speed);
            if (react_tick < 0 && s.speed < g_speed_before - REACT_DROP)
            {
                react_tick = s.tick;
                react_ms   = s.ms;
            }
            if (!result && prev - s.speed > DISTURB_DROP)
                result = "disturbed";   // sudden drop: not the parachute
            else if (!result && s.speed <= g_speed_before * TARGET_FRACTION)
                result = "ok";
            prev = s.speed;
        }
        if (status == "ok" && !g_samples.empty())
            status = result ? result : "not_reached";
        const bool ok = status == "ok" && react_tick >= 0;
        g_rows << ++g_count << "," << status << ","
               << (ok ? react_tick - g_hit_tick : -1) << ","
               << (ok ? (react_ms - g_hit_ms) * 1000.0 : -1.0) << ","
               << g_apply_us << "," << g_speed_before << "," << speed_min
               << "\n";
        g_samples.clear();
        if (status != "race_end")
        {
            g_recover_kart  = g_kart;
            g_recover_start = tick();
        }
        g_kart = NULL;
    }
}   // namespace banana
}   // namespace

bool BananaTestLogger::onBananaHit(AbstractKart* kart, float speed)
{
    using namespace banana;
    if (!kart->getController() || kart->getController()->isPlayerController() ||
        RewindManager::get()->isRewinding())
        return false;
    // Only the first banana after a reset is measured
    if (g_reset_kart || g_recover_kart || g_kart ||
        kart->getAttachment()->getType() != Attachment::ATTACH_NOTHING)
        return true;

    const double hit_ms = ms();
    kart->getAttachment()->set(Attachment::ATTACH_PARACHUTE,
        stk_config->time2Ticks(kart->getKartProperties()->getParachuteDuration()));
    const double apply_us = (ms() - hit_ms) * 1000.0;
    if (!g_race) return true;
    g_apply_us     = apply_us;
    g_hit_tick     = tick();
    g_hit_ms       = hit_ms;
    g_speed_before = speed;
    g_kart         = kart;
    g_samples.clear();
    return true;
}

void BananaTestLogger::onKartUpdate(AbstractKart* kart, float speed)
{
    using namespace banana;
    if (RewindManager::get()->isRewinding()) return;
    if (kart == g_recover_kart)
    {
        if ((kart->getAttachment()->getType() == Attachment::ATTACH_NOTHING &&
             speed >= kart->getCurrentMaxSpeed() * MAX_SPEED_TOLERANCE) ||
            tick() - g_recover_start > stk_config->time2Ticks(RECOVER_SECONDS))
        {
            g_reset_kart   = kart;
            g_recover_kart = NULL;
        }
        return;
    }
    if (kart != g_kart) return;
    if (kart->getAttachment()->getType() != Attachment::ATTACH_PARACHUTE)
        return closeEvent("ok");
    g_samples.push_back({tick(), ms(), speed});
    if (tick() - g_hit_tick > stk_config->time2Ticks(TIMEOUT_SECONDS))
        closeEvent("timeout");
}

/** Done before the race update of this tick, not inside Kart::update(). */
void BananaTestLogger::onTick(bool race_active)
{
    using namespace banana;
    if (!m_enabled || !race_active || !g_reset_kart ||
        !World::getWorld()->isActiveRacePhase())
        return;
    g_reset_kart->reset();
    g_reset_kart = NULL;
}

void BananaTestLogger::onFrame(bool race_active)
{
    using namespace banana;
    if (!m_enabled || race_active == g_race) return;
    g_race = race_active;
    if (!race_active)
    {
        if (g_kart) closeEvent("race_end");
        writeCsv("banana_debuff", m_log_dir, "event_id,status,react_ticks,"
                 "react_us,apply_us,speed_before,speed_min", g_rows.str());
    }
    clearRows(g_rows);
    g_count = 0;
    g_samples.clear();
    g_kart = g_reset_kart = g_recover_kart = NULL;
}
