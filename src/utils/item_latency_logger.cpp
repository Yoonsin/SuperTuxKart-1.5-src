//  SuperTuxKart - a fun racing game with go-kart
//
//  [item-latency] See item_latency_logger.hpp. All code here only runs
//  when --item-test is given.

#include "utils/item_latency_logger.hpp"

#include "config/stk_config.hpp"
#include "io/file_manager.hpp"
#include "items/powerup.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "modes/world.hpp"
#include "tracks/track.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"
#include "utils/profiler.hpp"   // getTimeMilliseconds()
#include "utils/time.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <vector>

bool        ItemLatencyLogger::m_enabled = false;
std::string ItemLatencyLogger::m_log_dir = "";

// ============================================================================
namespace
{
    enum State
    {
        S_IDLE,         // nothing in progress
        S_WAIT_LAUNCH   // fire pressed (T1), waiting for projectile (T2)
    };

    struct Record
    {
        int         id          = 0;
        int         pickup_tick = -1;
        int         input_tick  = -1;
        int         launch_tick = -1;
        double      input_ms    = 0.0;
        double      launch_ms   = 0.0;
        double      use_us      = -1.0;
        std::string status;
    };

    State               g_state            = S_IDLE;
    AbstractKart*       g_kart             = NULL;
    Record              g_current;
    std::vector<Record> g_records;
    int                 g_next_id          = 1;
    int                 g_wait_ticks       = 0;
    bool                g_race_active      = false;
    double              g_race_start_ms    = 0.0;
    std::string         g_track            = "unknown";
    int                 g_last_pickup_tick = -1;

    // ------------------------------------------------------------------------
    int currentTick()
    {
        World* w = World::getWorld();
        return w ? w->getTicksSinceStart() : -1;
    }   // currentTick

    // ------------------------------------------------------------------------
    void closeCurrent(const char* status)
    {
        g_current.status = status;
        g_records.push_back(g_current);
    }   // closeCurrent

    // ------------------------------------------------------------------------
    void resetState()
    {
        g_state      = S_IDLE;
        g_kart       = NULL;
        g_current    = Record();
        g_wait_ticks = 0;
    }   // resetState

    // ------------------------------------------------------------------------
    void writeFile(const std::string& dir)
    {
        if (g_records.empty())
        {
            Log::info("ItemLatency", "No item events recorded in this race.");
            return;
        }

        StkTime::TimeType t = StkTime::getTimeSinceEpoch();
        struct tm* now = std::localtime(&t);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", now);
        const std::string name = "item_latency_" + g_track + "_" +
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
            Log::error("ItemLatency", "Cannot open '%s'", path.c_str());
            return;
        }

        // status          : use only "ok"
        // delta_ticks     : fire input (T1) -> projectile launch (T2),
        //                   in physics ticks (game time)
        // delta_us        : same interval in real time
        // use_duration_us : processing time of Powerup::use()
        out << std::fixed << std::setprecision(3);
        out << "event_id,status,delta_ticks,delta_us,use_duration_us\n";
        for (const Record& r : g_records)
        {
            const bool launched = r.launch_tick >= 0;
            out << r.id                                              << ","
                << r.status                                          << ","
                << (launched ? r.launch_tick - r.input_tick : -1)    << ","
                << (launched ? (r.launch_ms - r.input_ms) * 1000.0
                             : -1.0)                                 << ","
                << r.use_us                                          << "\n";
        }
        out.close();
        Log::info("ItemLatency", "Saved %d item events to '%s'",
                  (int)g_records.size(), path.c_str());
        g_records.clear();
    }   // writeFile
}   // namespace

// ============================================================================
bool ItemLatencyLogger::isTargetKart(const AbstractKart* kart)
{
    AbstractKart* k = const_cast<AbstractKart*>(kart);
    // Local player kart (fires by hand) or AI kart (fires automatically
    // 1 s after the pickup, see skidding_ai.cpp)
    return k && k->getController() &&
           (k->getController()->isLocalPlayerController() ||
            !k->getController()->isPlayerController());
}   // isTargetKart

// ----------------------------------------------------------------------------
/** Only remembers when the bowling ball was picked up; the player fires. */
void ItemLatencyLogger::onBowlingCollected(AbstractKart* kart)
{
    if (!m_enabled || !isTargetKart(kart) || g_state != S_IDLE)
        return;
    if (kart->getPowerup()->getType() != PowerupManager::POWERUP_BOWLING)
        return;
    // Keep the first report of this pickup (item_manager.cpp and
    // skidding_ai.cpp may both report the same pickup)
    if (g_last_pickup_tick >= 0)
        return;
    g_last_pickup_tick = currentTick();
}   // onBowlingCollected

// ----------------------------------------------------------------------------
void ItemLatencyLogger::onFireInput(AbstractKart* kart)
{
    if (!m_enabled || !isTargetKart(kart))
        return;
    if (kart->getPowerup()->getType() != PowerupManager::POWERUP_BOWLING)
        return;
    if (g_state == S_WAIT_LAUNCH)
        return;

    g_kart                = kart;
    g_current             = Record();
    g_current.id          = g_next_id++;
    g_current.pickup_tick = g_last_pickup_tick;
    g_last_pickup_tick    = -1;
    g_current.input_tick  = currentTick();
    g_current.input_ms    = getTimeMilliseconds();
    g_wait_ticks          = 0;
    g_state               = S_WAIT_LAUNCH;
}   // onFireInput

// ----------------------------------------------------------------------------
void ItemLatencyLogger::onProjectileCreated(AbstractKart* kart,
                                            PowerupManager::PowerupType type)
{
    if (!m_enabled || g_state != S_WAIT_LAUNCH || kart != g_kart ||
        type != PowerupManager::POWERUP_BOWLING)
        return;

    g_current.launch_tick = currentTick();
    g_current.launch_ms   = getTimeMilliseconds();
    closeCurrent("ok");
    g_state = S_IDLE;
    g_kart  = NULL;
}   // onProjectileCreated

// ----------------------------------------------------------------------------
void ItemLatencyLogger::reportUseDuration(double duration_us)
{
    if (!g_records.empty() && g_records.back().id == g_current.id &&
        g_records.back().use_us < 0.0)
    {
        g_records.back().use_us = duration_us;
    }
}   // reportUseDuration

// ----------------------------------------------------------------------------
void ItemLatencyLogger::onTick(bool race_active)
{
    if (!m_enabled)
        return;

    if (!race_active)
    {
        if (g_state == S_WAIT_LAUNCH)
            closeCurrent("race_end");
        resetState();
        return;
    }

    if (g_state == S_WAIT_LAUNCH &&
        ++g_wait_ticks > stk_config->time2Ticks(1.0f))
    {
        closeCurrent("timeout");
        resetState();
    }
}   // onTick

// ----------------------------------------------------------------------------
void ItemLatencyLogger::onFrame(bool race_active)
{
    if (!m_enabled)
        return;

    if (race_active && !g_race_active)
    {
        g_records.clear();
        g_next_id          = 1;
        g_last_pickup_tick = -1;
        g_race_start_ms    = getTimeMilliseconds();
        g_track = Track::getCurrentTrack() ?
                  Track::getCurrentTrack()->getIdent() : "unknown";
    }
    else if (!race_active && g_race_active)
    {
        writeFile(m_log_dir);
    }
    g_race_active = race_active;
}   // onFrame

// ----------------------------------------------------------------------------
void ItemLatencyLogger::finish()
{
    if (m_enabled && g_race_active)
        writeFile(m_log_dir);
    g_race_active = false;
}   // finish

// ============================================================================
ItemLatencyLogger::UseScope::UseScope(const AbstractKart* kart,
                                      PowerupManager::PowerupType type)
    : m_active(false), m_start_ms(0.0)
{
    if (!ItemLatencyLogger::isActive() ||
        type != PowerupManager::POWERUP_BOWLING ||
        !ItemLatencyLogger::isTargetKart(kart))
        return;
    m_active   = true;
    m_start_ms = getTimeMilliseconds();
}   // UseScope

// ----------------------------------------------------------------------------
ItemLatencyLogger::UseScope::~UseScope()
{
    if (!m_active)
        return;
    const double us = (getTimeMilliseconds() - m_start_ms) * 1000.0;
    ItemLatencyLogger::reportUseDuration(us);
}   // ~UseScope
