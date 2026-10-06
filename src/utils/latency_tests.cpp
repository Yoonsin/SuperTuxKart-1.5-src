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

    /** Tick shown by the next rendered frame. Called from onFrame(), i.e.
     *  after all physics ticks of this frame: main_loop renders the state of
     *  this tick at the start of the next frame. -1: no world.
     *  local_ticks = this - start tick: the event span plus the ticks that
     *  wait in the same frame batch before they are drawn. The batch size
     *  depends on the frame rate, so local_ticks differs between platforms
     *  while the physics (all other *_ticks) stay the same. */
    int  frameTick() { return World::getWorld() ? tick() : -1; }
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
std::vector<ItemLatencyLogger::Event> ItemLatencyLogger::m_events;
ItemLatencyLogger::Event              ItemLatencyLogger::m_cur;
AbstractKart*                         ItemLatencyLogger::m_kart      = NULL;
int                                   ItemLatencyLogger::m_wait      = 0;
double                                ItemLatencyLogger::m_use_start = -1.0;
bool                                  ItemLatencyLogger::m_race      = false;

/** Local player (fires by hand) or AI kart (fires in skidding_ai.cpp). */
bool ItemLatencyLogger::isTarget(const AbstractKart* kart)
{
    const Controller* c = kart ? kart->getController() : NULL;
    return c && (c->isLocalPlayerController() || !c->isPlayerController());
}

void ItemLatencyLogger::closeEvent(const char* status, bool keep)
{
    m_cur.status = status;
    m_events.push_back(m_cur);
    if (!keep) m_cur = Event();
    m_kart = NULL;
    m_wait = 0;
}

bool ItemLatencyLogger::shouldForceBowling(const AbstractKart* kart)
{
    return m_enabled && isTarget(kart);
}

void ItemLatencyLogger::onFireInput(AbstractKart* kart)
{
    if (m_kart || !isTarget(kart) ||
        kart->getPowerup()->getType() != PowerupManager::POWERUP_BOWLING)
        return;
    m_cur            = Event();
    m_cur.id         = (int)m_events.size() + 1;
    m_cur.input_tick = tick();
    m_kart           = kart;
    m_wait           = 0;
}

void ItemLatencyLogger::onProjectileCreated(AbstractKart* kart,
                                            PowerupManager::PowerupType type)
{
    if (!m_kart || kart != m_kart || type != PowerupManager::POWERUP_BOWLING)
        return;
    m_cur.launch_tick = tick();
    closeEvent("ok", /*keep for onUseEnd*/true);
}

void ItemLatencyLogger::onUseStart(const AbstractKart* kart,
                                   PowerupManager::PowerupType type)
{
    m_use_start = type == PowerupManager::POWERUP_BOWLING &&
                  isTarget(kart) ? ms() : -1.0;
}

void ItemLatencyLogger::onUseEnd()
{
    if (m_use_start >= 0.0 && !m_events.empty() &&
        m_events.back().id == m_cur.id && m_events.back().use_us < 0.0)
        m_events.back().use_us = (ms() - m_use_start) * 1000.0;
    m_use_start = -1.0;
}

void ItemLatencyLogger::onTick(bool race_active)
{
    if (!m_enabled) return;
    if (!race_active)
    {
        if (m_kart) closeEvent("race_end");
        m_cur = Event();
    }
    else if (m_kart && ++m_wait > stk_config->time2Ticks(1.0f))
        closeEvent("timeout");
}

void ItemLatencyLogger::onFrame(bool race_active)
{
    if (!m_enabled) return;
    for (Event& e : m_events)
        if (e.launch_tick >= 0 && e.shown_tick < 0) e.shown_tick = frameTick();
    if (race_active == m_race) return;
    m_race = race_active;
    std::ostringstream rows;
    clearRows(rows);
    for (const Event& e : m_events)
    {
        const bool ok = e.launch_tick >= 0;
        const int delta_ticks = ok ? e.launch_tick - e.input_tick : -1;
        rows << e.id << "," << e.status << "," << delta_ticks << ","
             << (ok && e.shown_tick >= 0 ? e.shown_tick - e.input_tick : -1)
             << "," << e.use_us << "\n";
    }
    if (!race_active)
        writeCsv("item_latency", m_log_dir,
                 "event_id,status,delta_ticks,local_ticks,use_duration_us",
                 rows.str());
    m_events.clear();
}

// ============================================================================
// --wall-test: the distance from standstill to the target speed (80% of max
// speed) is measured at race start. After a wall crash the kart reverses that
// distance * 1.3 + 2 m, then accelerates again: crash -> back at the target
// speed. After every event the kart is reset to its start position.
WallCrashLogger::State     WallCrashLogger::m_state = S_LEARN;
AbstractKart*              WallCrashLogger::m_kart  = NULL;
std::ostringstream         WallCrashLogger::m_rows;
int                        WallCrashLogger::m_count = 0;
bool                       WallCrashLogger::m_race = false, WallCrashLogger::m_reset = false,
                           WallCrashLogger::m_start_set = false;
float                      WallCrashLogger::m_accel_dist = -1.0f;
Vec3                       WallCrashLogger::m_start_xyz, WallCrashLogger::m_crash_xyz,
                           WallCrashLogger::m_last_xyz;
int                        WallCrashLogger::m_crash_tick = -1, WallCrashLogger::m_release_tick = -1;
float                      WallCrashLogger::m_max_speed = 0.0f, WallCrashLogger::m_back_dist = 0.0f;
bool                       WallCrashLogger::m_fast = false;
std::string                WallCrashLogger::m_pending;
int                        WallCrashLogger::m_pending_start = -1;
double                     WallCrashLogger::m_pending_crash_us = -1.0;
double                     WallCrashLogger::m_crash_start_ms = -1.0;
double                     WallCrashLogger::m_crash_us = -1.0;

void WallCrashLogger::brake(bool on)
{
    m_kart->getController()->action(PA_BRAKE, on ? Input::MAX_VALUE : 0);
}

bool WallCrashLogger::isMeasuring()
{
    return m_state == S_CRASHED || m_state == S_BACKING ||
           m_state == S_RECOVERING;
}

/** recover_tick < 0: the kart did not get back to the target speed. */
void WallCrashLogger::closeEvent(const char* status, int recover_tick)
{
    if (m_state == S_BACKING) brake(false);
    const bool ok = recover_tick >= 0;
    const int recovery_ticks = ok ? recover_tick - m_release_tick : -1;
    std::ostringstream row;
    clearRows(row);
    row << ++m_count << "," << status << "," << recovery_ticks;
    // local_ticks (release -> shown at target speed) and crash_us (this
    // event's Kart::crashed() time) are added after, in onFrame
    if (ok)
    {
        m_pending          = row.str();
        m_pending_start    = m_release_tick;
        m_pending_crash_us = m_crash_us;
    }
    else
        m_rows << row.str() << ",-1," << m_crash_us << "\n";
    m_state = S_IDLE;
    m_reset = std::string(status) != "race_end";
}

void WallCrashLogger::onCrash(AbstractKart* kart, const Vec3& normal,
                              float speed_before)
{
    // T1 of crash_us, consumed by onCrashEnd(); -1 unless accepted below
    m_crash_start_ms = -1.0;
    // Only a new crash into a wall (not floor / ceiling) while driving
    const btVector3 up = kart->getTrans().getBasis().getColumn(1);
    if (!m_race || isMeasuring() || m_reset || !isPlayerInRace(kart) ||
        std::fabs(normal.dot(up)) > 0.7f || speed_before < 1.0f)
        return;
    m_max_speed      = kart->getCurrentMaxSpeed();
    m_crash_tick     = tick();
    m_crash_start_ms = ms();
    m_release_tick   = -1;
    m_fast           = speed_before >= m_max_speed * TARGET_FRACTION;
    m_back_dist      = m_accel_dist > 0.0f
                     ? m_accel_dist * BACK_FACTOR + BACK_MARGIN
                     : DEFAULT_BACK_DIST;
    m_kart           = kart;
    m_crash_xyz      = m_last_xyz = kart->getXYZ();
    m_state          = S_CRASHED;
}

void WallCrashLogger::onCrashEnd()
{
    if (m_crash_start_ms >= 0.0)
        m_crash_us = (ms() - m_crash_start_ms) * 1000.0;
    m_crash_start_ms = -1.0;
}

void WallCrashLogger::onSpeedUpdate(AbstractKart* kart, float speed)
{
    if (!m_race || !isPlayerInRace(kart)) return;
    m_kart = kart;
    if (m_reset) return;

    // --auto-accel presses only once per race and the kart reset releases
    // all keys, so accelerate here (except while reversing)
    if (m_state != S_CRASHED && m_state != S_BACKING)
        kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);

    if (m_state == S_LEARN)
    {
        if (!m_start_set && speed > 0.2f)
        {
            m_start_xyz = kart->getXYZ();
            m_start_set = true;
        }
        if (m_start_set &&
            speed >= kart->getCurrentMaxSpeed() * TARGET_FRACTION)
        {
            m_accel_dist = (kart->getXYZ() - m_start_xyz).length();
            m_state = S_IDLE;
            m_reset = true;
        }
        return;
    }
    if (m_state == S_IDLE) return;

    // A rescue teleports the kart, more than 3 m in one tick
    if ((kart->getXYZ() - m_last_xyz).length() > 3.0f)
        return closeEvent("reset");
    m_last_xyz = kart->getXYZ();

    if (m_state == S_CRASHED)
    {
        brake(true);
        m_state = S_BACKING;
    }
    else if (m_state == S_BACKING)
    {
        const bool far_enough =
            (kart->getXYZ() - m_crash_xyz).length() >= m_back_dist;
        // Pressed again every tick, a key repeat of accel would cancel it
        brake(!far_enough);
        if (far_enough)
        {
            m_release_tick = tick();
            m_state        = S_RECOVERING;
        }
    }
    else if (speed >= m_max_speed * TARGET_FRACTION)
        return closeEvent(m_fast ? "ok" : "slow", tick());
    if (tick() - m_crash_tick > stk_config->time2Ticks(TIMEOUT_SECONDS))
        closeEvent("timeout");
}

/** Done before the race update of this tick, not inside Kart::update(). */
void WallCrashLogger::onTick(bool race_active)
{
    if (!m_enabled || !race_active || !m_reset || !m_kart ||
        !World::getWorld()->isActiveRacePhase())
        return;
    m_kart->reset();
    m_kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);
    m_reset = false;
}

void WallCrashLogger::onFrame(bool race_active)
{
    if (!m_enabled) return;
    if (!m_pending.empty())
    {
        const int shown = frameTick();
        m_rows << m_pending << ","
               << (shown >= 0 ? shown - m_pending_start : -1) << ","
               << m_pending_crash_us << "\n";
        m_pending.clear();
    }
    if (race_active == m_race) return;
    m_race = race_active;
    if (!race_active)
    {
        if (isMeasuring()) closeEvent("race_end");
        writeCsv("wall_recovery", m_log_dir,
                 "event_id,status,recovery_ticks,local_ticks,crash_us",
                 m_rows.str());
    }
    clearRows(m_rows);
    m_count      = 0;
    m_state      = S_LEARN;
    m_kart       = NULL;
    m_start_set  = m_reset = false;
    m_accel_dist = -1.0f;
    m_crash_start_ms = m_crash_us = m_pending_crash_us = -1.0;
}

// ============================================================================
// --fall-test: the first rescue of a race, track exit -> rescue animation
// over. The kart then drives on 10 m, the result is saved and the game quits.
FallTestLogger::State FallTestLogger::m_state = S_DRIVING;
bool                  FallTestLogger::m_race  = false;
Vec3                  FallTestLogger::m_respawn_xyz;
int                   FallTestLogger::m_start_tick = -1, FallTestLogger::m_respawn_ticks = -1,
                      FallTestLogger::m_shown_tick = -1;
double                FallTestLogger::m_rescue_start_ms = -1.0, FallTestLogger::m_rescue_us = -1.0;

void FallTestLogger::save(const std::string& dir, const char* status, bool quit)
{
    std::ostringstream row;
    clearRows(row);
    row << status << "," << m_respawn_ticks << ","
        << (m_shown_tick >= 0 ? m_shown_tick - m_start_tick : -1) << ","
        << m_rescue_us << "\n";
    writeCsv("fall_respawn", dir, "status,respawn_ticks,local_ticks,rescue_us",
             row.str());
    m_state = S_DONE;
    if (quit) main_loop->requestAbort();
}

// T1, called directly from the RescueAnimation::create() call sites
// (kart.cpp, physics.cpp, player_controller.cpp) instead of being inferred
// a tick later by polling getKartAnimation() in onKartUpdate.
void FallTestLogger::onRescueStart(AbstractKart* kart)
{
    if (!m_race || m_state != S_DRIVING || !isPlayerInRace(kart)) return;
    m_start_tick = tick();
    m_state      = S_RESCUING;
}

// T1/T2 of rescue_us, around the RescueAnimation(kart, bool) constructor
// body. Only the target kart's first (measured) rescue of the race counts;
// other RescueAnimation constructions (AI karts, a later rescue) leave
// m_rescue_us untouched since m_rescue_start_ms stays < 0 for them.
void FallTestLogger::onRescueBegin(AbstractKart* kart)
{
    m_rescue_start_ms = (m_race && m_state == S_DRIVING && isPlayerInRace(kart))
                       ? ms() : -1.0;
}

void FallTestLogger::onRescueEnd()
{
    if (m_rescue_start_ms >= 0.0)
        m_rescue_us = (ms() - m_rescue_start_ms) * 1000.0;
    m_rescue_start_ms = -1.0;
}

void FallTestLogger::onKartUpdate(AbstractKart* kart, float speed)
{
    if (!m_race || m_state == S_DONE || !isPlayerInRace(kart)) return;

    const bool rescuing =
        dynamic_cast<RescueAnimation*>(kart->getKartAnimation()) != NULL;
    if (!rescuing)
        kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);

    if (m_state == S_DRIVING && !rescuing) return;

    if (m_state == S_DRIVING && rescuing)
    {
        m_start_tick = tick();
        m_state      = S_RESCUING;
    }
    else if (m_state == S_RESCUING && !rescuing)
    {
        m_respawn_ticks = tick() - m_start_tick;
        m_respawn_xyz   = kart->getXYZ();
        m_state         = S_DRIVING_ON;
    }
    else if (m_state == S_RESCUING)
    {
        if (tick() - m_start_tick > stk_config->time2Ticks(TIMEOUT_SECONDS))
            save(m_log_dir, "timeout", true);
    }
    else if ((kart->getXYZ() - m_respawn_xyz).length() >= DRIVE_DISTANCE)
        save(m_log_dir, "ok", true);
    else if (tick() - (m_start_tick + m_respawn_ticks) >
             stk_config->time2Ticks(DRIVE_TIMEOUT_SECONDS))
        save(m_log_dir, "no_drive", true);
}

void FallTestLogger::onFrame(bool race_active)
{
    if (!m_enabled) return;
    // Respawn over: the next rendered frame shows the kart back on track
    if (m_state == S_DRIVING_ON && m_shown_tick < 0)
        m_shown_tick = frameTick();
    if (race_active == m_race) return;
    m_race = race_active;
    if (!race_active)
    {
        if (m_state == S_RESCUING)   save(m_log_dir, "race_end", false);
        if (m_state == S_DRIVING_ON) save(m_log_dir, "no_drive", false);
        m_state = S_DONE;
        return;
    }
    m_state      = S_DRIVING;
    m_start_tick = m_respawn_ticks = -1;
    m_shown_tick = -1;
    m_rescue_start_ms = m_rescue_us = -1.0;
}

// ============================================================================
// --banana-test: a banana always attaches a parachute. banana hit (T1) ->
// speed starts to drop (T2), and a check that the speed drops to 70%. After
// every hit the kart drives on until max speed, then it is reset to its
// start position.
std::vector<BananaTestLogger::Sample> BananaTestLogger::m_samples;
std::ostringstream   BananaTestLogger::m_rows;
int                  BananaTestLogger::m_count = 0;
bool                 BananaTestLogger::m_race  = false;
AbstractKart*        BananaTestLogger::m_kart  = NULL;
AbstractKart*        BananaTestLogger::m_reset_kart = NULL, *BananaTestLogger::m_recover_kart = NULL;
int                  BananaTestLogger::m_recover_start = -1, BananaTestLogger::m_hit_tick = -1;
double               BananaTestLogger::m_apply_us = -1.0;
float                BananaTestLogger::m_speed_before = 0.0f;
std::vector<int>     BananaTestLogger::m_frame_ticks;

void BananaTestLogger::closeEvent(std::string status)
{
    int    react_tick = -1;
    const char* result = NULL;
    float prev = m_speed_before;
    for (const Sample& s : m_samples)
    {
        if (react_tick < 0 && s.speed < m_speed_before - REACT_DROP)
            react_tick = s.tick;
        if (!result && prev - s.speed > DISTURB_DROP)
            result = "disturbed";   // sudden drop: not the parachute
        else if (!result && s.speed <= m_speed_before * TARGET_FRACTION)
            result = "ok";
        prev = s.speed;
    }
    if (status == "ok" && !m_samples.empty())
        status = result ? result : "not_reached";
    const bool ok = status == "ok" && react_tick >= 0;
    const int react_ticks = ok ? react_tick - m_hit_tick : -1;
    // First frame that draws the react tick
    int shown = -1;
    for (int f : m_frame_ticks)
        if (f > react_tick) { shown = f; break; }
    m_rows << ++m_count << "," << status << ","
           << react_ticks << ","
           << (ok && shown >= 0 ? shown - m_hit_tick : -1) << ","
           << m_apply_us << "\n";
    m_samples.clear();
    m_frame_ticks.clear();
    if (status != "race_end")
    {
        m_recover_kart  = m_kart;
        m_recover_start = tick();
    }
    m_kart = NULL;
}

bool BananaTestLogger::onBananaHit(AbstractKart* kart, float speed)
{
    if (!kart->getController() || kart->getController()->isPlayerController() ||
        RewindManager::get()->isRewinding())
        return false;
    // Only the first banana after a reset is measured
    if (m_reset_kart || m_recover_kart || m_kart ||
        kart->getAttachment()->getType() != Attachment::ATTACH_NOTHING)
        return true;

    const double hit_ms = ms();
    kart->getAttachment()->set(Attachment::ATTACH_PARACHUTE,
        stk_config->time2Ticks(kart->getKartProperties()->getParachuteDuration()));
    const double apply_us = (ms() - hit_ms) * 1000.0;
    if (!m_race) return true;
    m_apply_us     = apply_us;
    m_hit_tick     = tick();
    m_speed_before = speed;
    m_kart         = kart;
    m_samples.clear();
    m_frame_ticks.clear();
    return true;
}

void BananaTestLogger::onKartUpdate(AbstractKart* kart, float speed)
{
    if (RewindManager::get()->isRewinding()) return;
    if (kart == m_recover_kart)
    {
        if ((kart->getAttachment()->getType() == Attachment::ATTACH_NOTHING &&
             speed >= kart->getCurrentMaxSpeed() * MAX_SPEED_TOLERANCE) ||
            tick() - m_recover_start > stk_config->time2Ticks(RECOVER_SECONDS))
        {
            m_reset_kart   = kart;
            m_recover_kart = NULL;
        }
        return;
    }
    if (kart != m_kart) return;
    if (kart->getAttachment()->getType() != Attachment::ATTACH_PARACHUTE)
        return closeEvent("ok");
    m_samples.push_back({tick(), speed});
    if (tick() - m_hit_tick > stk_config->time2Ticks(TIMEOUT_SECONDS))
        closeEvent("timeout");
}

/** Done before the race update of this tick, not inside Kart::update(). */
void BananaTestLogger::onTick(bool race_active)
{
    if (!m_enabled || !race_active || !m_reset_kart ||
        !World::getWorld()->isActiveRacePhase())
        return;
    m_reset_kart->reset();
    m_reset_kart = NULL;
}

void BananaTestLogger::onFrame(bool race_active)
{
    if (!m_enabled) return;
    if (m_kart && frameTick() >= 0) m_frame_ticks.push_back(frameTick());
    if (race_active == m_race) return;
    m_race = race_active;
    if (!race_active)
    {
        if (m_kart) closeEvent("race_end");
        writeCsv("banana_debuff", m_log_dir,
                 "event_id,status,react_ticks,local_ticks,apply_us",
                 m_rows.str());
    }
    clearRows(m_rows);
    m_count = 0;
    m_samples.clear();
    m_frame_ticks.clear();
    m_kart = m_reset_kart = m_recover_kart = NULL;
}
