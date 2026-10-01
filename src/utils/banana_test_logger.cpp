//  SuperTuxKart - a fun racing game with go-kart
//
//  [banana-test] See banana_test_logger.hpp. All code here only runs
//  when --banana-test is given.

#include "utils/banana_test_logger.hpp"

#include "config/stk_config.hpp"
#include "io/file_manager.hpp"
#include "items/attachment.hpp"
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

#include <ctime>
#include <fstream>
#include <iomanip>
#include <vector>

bool        BananaTestLogger::m_enabled = false;
std::string BananaTestLogger::m_log_dir = "";

// ============================================================================
namespace
{
    /** Debuff counts as applied when the speed has dropped to this fraction
     *  of the speed at the banana hit. The parachute alone always gets the
     *  kart below it (about 68% of max speed), so every measurement ends at
     *  the same point of the slowdown curve. */
    const float TARGET_FRACTION = 0.70f;
    /** Debuff counts as reflected (T2) at the first physics tick in which
     *  the speed is lower than the speed at the hit by more than this. */
    const float REACT_DROP      = 0.001f;
    /** A drop larger than this (m/s) within one physics tick before the
     *  target is reached is not the parachute (e.g. wall hit). */
    const float DISTURB_DROP    = 1.0f;
    /** Give up if the parachute is still attached after this time. */
    const float TIMEOUT_SECONDS = 15.0f;
    /** After the debuff the kart drives on until it is back at max speed
     *  (at least this fraction of its current max speed), then it is reset
     *  to the start position. */
    const float MAX_SPEED_TOLERANCE = 0.995f;
    /** Reset anyway if max speed is not reached within this time. */
    const float RECOVER_TIMEOUT_SECONDS = 15.0f;

    struct Sample
    {
        int    tick;
        double ms;
        float  speed;
    };

    struct Record
    {
        int         id           = 0;
        int         hit_tick     = -1;
        double      hit_ms       = 0.0;
        float       speed_before = 0.0f;
        float       speed_min    = 0.0f;
        int         reach_tick   = -1;
        double      reach_ms     = 0.0;
        int         react_tick   = -1;    // T2: speed starts to drop
        double      react_ms     = 0.0;
        double      apply_us     = -1.0;  // processing time of the hit
        std::string status;
    };

    bool                g_measuring   = false;
    AbstractKart*       g_kart        = NULL;
    Record              g_current;
    std::vector<Sample> g_samples;
    std::vector<Record> g_records;
    int                 g_next_id     = 1;
    bool                g_race_active = false;
    AbstractKart*       g_reset_kart  = NULL;   // kart to reset (onTick)
    AbstractKart*       g_recover_kart = NULL;  // waiting for max speed
    int                 g_recover_start_tick = -1;
    std::string         g_track       = "unknown";

    // ------------------------------------------------------------------------
    int currentTick()
    {
        World* w = World::getWorld();
        return w ? w->getTicksSinceStart() : -1;
    }   // currentTick

    // ------------------------------------------------------------------------
    bool isTargetKart(AbstractKart* kart)
    {
        // AI karts only (the AI drives over bananas when --banana-test
        // is given, see skidding_ai.cpp)
        return kart && kart->getController() &&
               !kart->getController()->isPlayerController();
    }   // isTargetKart

    // ------------------------------------------------------------------------
    /** Parachute is gone: find the slowest speed and the first tick at
     *  which the kart got (within tolerance) down to it. */
    void closeCurrent(const char* status)
    {
        g_current.status = status;
        if (!g_samples.empty())
        {
            float min_speed = g_samples[0].speed;
            for (const Sample& s : g_samples)
                if (s.speed < min_speed) min_speed = s.speed;
            g_current.speed_min = min_speed;

            // T2: first tick in which the speed starts to drop
            for (const Sample& s : g_samples)
            {
                if (s.speed < g_current.speed_before - REACT_DROP)
                {
                    g_current.react_tick = s.tick;
                    g_current.react_ms   = s.ms;
                    break;
                }
            }

            const float target = g_current.speed_before * TARGET_FRACTION;
            float prev = g_current.speed_before;
            for (const Sample& s : g_samples)
            {
                if (prev - s.speed > DISTURB_DROP)
                {
                    // Sudden drop before the target: not the parachute
                    if (g_current.status == "ok")
                        g_current.status = "disturbed";
                    break;
                }
                if (s.speed <= target)
                {
                    g_current.reach_tick = s.tick;
                    g_current.reach_ms   = s.ms;
                    break;
                }
                prev = s.speed;
            }
            if (g_current.reach_tick < 0 && g_current.status == "ok")
                g_current.status = "not_reached";
        }
        g_records.push_back(g_current);
        g_samples.clear();
        g_measuring = false;
        // Same start condition for the next hit: after the debuff the kart
        // drives on until it is back at max speed, then it is reset to the
        // start position (see onKartUpdate / onTick). So every hit is the
        // first banana, hit at the same spot and speed.
        if (g_current.status != "race_end")
        {
            g_recover_kart       = g_kart;
            g_recover_start_tick = currentTick();
        }
        g_kart      = NULL;
    }   // closeCurrent

    // ------------------------------------------------------------------------
    void writeFile(const std::string& dir)
    {
        if (g_records.empty())
        {
            Log::info("BananaTest", "No banana hits recorded in this race.");
            return;
        }

        StkTime::TimeType t = StkTime::getTimeSinceEpoch();
        struct tm* now = std::localtime(&t);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", now);
        const std::string name = "banana_debuff_" + g_track + "_" +
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
            Log::error("BananaTest", "Cannot open '%s'", path.c_str());
            return;
        }

        // status       : use only "ok"
        // react_ticks  : banana hit (T1) -> speed starts to drop (T2),
        //                physics ticks
        // react_us     : same interval in real time
        // apply_us     : processing time of applying the debuff (parachute)
        // speed_before : speed when the banana was hit (m/s)
        // speed_min    : slowest speed under the parachute (m/s)
        out << std::fixed << std::setprecision(3);
        out << "event_id,status,react_ticks,react_us,apply_us,"
               "speed_before,speed_min\n";
        for (const Record& r : g_records)
        {
            const bool ok = r.status == "ok" && r.react_tick >= 0;
            out << r.id                                               << ","
                << r.status                                           << ","
                << (ok ? r.react_tick - r.hit_tick : -1)             << ","
                << (ok ? (r.react_ms - r.hit_ms) * 1000.0 : -1.0)     << ","
                << r.apply_us                                         << ","
                << r.speed_before                                     << ","
                << r.speed_min                                        << "\n";
        }
        out.close();
        Log::info("BananaTest", "Saved %d banana hits to '%s'",
                  (int)g_records.size(), path.c_str());
        g_records.clear();
    }   // writeFile
}   // namespace

// ============================================================================
bool BananaTestLogger::onBananaHit(AbstractKart* kart, float speed)
{
    if (!m_enabled || !isTargetKart(kart))
        return false;   // original banana code
    if (RewindManager::get()->isRewinding())
        return false;

    // Kart is recovering / about to be reset to the start position:
    // ignore bananas so every measured hit is the first banana
    if (g_reset_kart || g_recover_kart)
        return true;

    // Debuff still active (or being measured): ignore this banana, so a
    // second banana never changes or extends the current debuff.
    if (g_measuring ||
        kart->getAttachment()->getType() != Attachment::ATTACH_NOTHING)
        return true;

    // T1: banana hit
    const double hit_ms = getTimeMilliseconds();

    // Always a parachute (same duration as a banana parachute),
    // never bomb / anvil
    kart->getAttachment()->set(Attachment::ATTACH_PARACHUTE,
        stk_config->time2Ticks(
            kart->getKartProperties()->getParachuteDuration()));

    // Processing time of applying the debuff (parachute)
    const double apply_us = (getTimeMilliseconds() - hit_ms) * 1000.0;

    if (!g_race_active)
        return true;

    g_kart                 = kart;
    g_current              = Record();
    g_current.id           = g_next_id++;
    g_current.hit_tick     = currentTick();
    g_current.hit_ms       = hit_ms;
    g_current.apply_us     = apply_us;
    g_current.speed_before = speed;
    g_samples.clear();
    g_measuring            = true;
    return true;
}   // onBananaHit

// ----------------------------------------------------------------------------
void BananaTestLogger::onKartUpdate(AbstractKart* kart, float speed)
{
    if (!m_enabled)
        return;

    // Debuff over: wait until the kart is back at max speed, then reset it
    // to the start position (done in onTick)
    if (g_recover_kart && kart == g_recover_kart)
    {
        if (RewindManager::get()->isRewinding())
            return;
        const bool at_max =
            kart->getAttachment()->getType() == Attachment::ATTACH_NOTHING &&
            speed >= kart->getCurrentMaxSpeed() * MAX_SPEED_TOLERANCE;
        const bool too_long = currentTick() - g_recover_start_tick >
            stk_config->time2Ticks(RECOVER_TIMEOUT_SECONDS);
        if (at_max || too_long)
        {
            g_reset_kart   = kart;
            g_recover_kart = NULL;
        }
        return;
    }

    if (!g_measuring || kart != g_kart)
        return;
    if (RewindManager::get()->isRewinding())
        return;

    // Parachute removed (time over or kart slow enough): event finished
    if (kart->getAttachment()->getType() != Attachment::ATTACH_PARACHUTE)
    {
        closeCurrent("ok");
        return;
    }

    Sample s;
    s.tick  = currentTick();
    s.ms    = getTimeMilliseconds();
    s.speed = speed;
    g_samples.push_back(s);

    if (currentTick() - g_current.hit_tick >
        stk_config->time2Ticks(TIMEOUT_SECONDS))
    {
        closeCurrent("timeout");
    }
}   // onKartUpdate

// ----------------------------------------------------------------------------
/** Puts the kart back to its start position, standing still, after each
 *  measured hit. Done here (before the race update of this tick) and not
 *  inside Kart::update(). */
void BananaTestLogger::onTick(bool race_active)
{
    if (!m_enabled || !race_active || !g_reset_kart)
        return;
    World* w = World::getWorld();
    if (!w || !w->isActiveRacePhase())
        return;
    g_reset_kart->reset();   // start position, speed 0, physics reset
    g_reset_kart = NULL;
}   // onTick

// ----------------------------------------------------------------------------
void BananaTestLogger::onFrame(bool race_active)
{
    if (!m_enabled)
        return;

    if (race_active && !g_race_active)
    {
        g_records.clear();
        g_samples.clear();
        g_next_id   = 1;
        g_measuring = false;
        g_kart      = NULL;
        g_reset_kart = NULL;
        g_recover_kart = NULL;
        g_track = Track::getCurrentTrack() ?
                  Track::getCurrentTrack()->getIdent() : "unknown";
    }
    else if (!race_active && g_race_active)
    {
        if (g_measuring)
            closeCurrent("race_end");
        writeFile(m_log_dir);
    }
    g_race_active = race_active;
}   // onFrame

// ----------------------------------------------------------------------------
void BananaTestLogger::finish()
{
    if (!m_enabled)
        return;
    if (g_measuring)
        closeCurrent("race_end");
    if (g_race_active)
        writeFile(m_log_dir);
    g_race_active = false;
}   // finish
