//  SuperTuxKart - a fun racing game with go-kart
//
//  [fall-test] See fall_test_logger.hpp. All code here only runs
//  when --fall-test is given.

#include "utils/fall_test_logger.hpp"

#include "config/stk_config.hpp"
#include "io/file_manager.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "input/input.hpp"
#include "karts/rescue_animation.hpp"
#include "main_loop.hpp"
#include "modes/world.hpp"
#include "network/rewind_manager.hpp"
#include "tracks/track.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"
#include "utils/profiler.hpp"   // getTimeMilliseconds()
#include "utils/time.hpp"
#include "utils/vec3.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>

bool        FallTestLogger::m_enabled = false;
std::string FallTestLogger::m_log_dir = "";

// ============================================================================
namespace
{
    /** Give up if the respawn takes longer than this. */
    const float TIMEOUT_SECONDS = 10.0f;
    /** After the respawn the kart drives on (--auto-accel) this far,
     *  then the result is saved and the game quits. */
    const float DRIVE_DISTANCE  = 10.0f;
    /** Give up driving on after this time (status "no_drive"). */
    const float DRIVE_TIMEOUT_SECONDS = 10.0f;

    enum State
    {
        S_DRIVING,      // waiting for the first rescue
        S_RESCUING,     // rescue started (T1), waiting for the end (T2)
        S_DRIVING_ON,   // respawn done, driving on before saving
        S_DONE          // first rescue measured, nothing more this race
    };

    State       g_state        = S_DRIVING;
    bool        g_race_active  = false;
    std::string g_track        = "unknown";

    // Last values while driving normally (= just before the track exit)
    float       g_last_speed   = 0.0f;
    Vec3        g_last_xyz;

    // Current measurement
    int         g_start_tick   = -1;
    double      g_start_ms     = 0.0;
    float       g_speed_before = 0.0f;
    Vec3        g_fall_xyz;

    // Result kept until the kart has driven on after the respawn
    int         g_respawn_ticks = -1;
    double      g_respawn_ms    = -1.0;
    int         g_respawn_tick  = -1;   // tick the respawn was done
    Vec3        g_respawn_xyz;

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
    void resetAll()
    {
        g_state      = S_DRIVING;
        g_last_speed = 0.0f;
        g_start_tick = -1;
    }   // resetAll

    // ------------------------------------------------------------------------
    /** Writes the (single) result of this race. */
    void writeFile(const std::string& dir, const char* status,
                   double respawn_ms, int respawn_ticks)
    {
        StkTime::TimeType t = StkTime::getTimeSinceEpoch();
        struct tm* now = std::localtime(&t);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", now);
        const std::string name = "fall_respawn_" + g_track + "_" +
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
            Log::error("FallTest", "Cannot open '%s'", path.c_str());
            return;
        }

        // status          : use only "ok"
        // respawn_ticks   : track exit -> respawn done, physics ticks
        // respawn_ms      : same interval in real time
        // speed_before    : speed just before the track exit (m/s)
        // fall_x, fall_z  : position of the track exit (same spot check)
        out << std::fixed << std::setprecision(3);
        out << "status,respawn_ticks,respawn_ms,speed_before,"
               "fall_x,fall_z\n";
        out << status                                              << ","
            << (respawn_ticks >= 0 ? respawn_ticks : -1)           << ","
            << respawn_ms                                          << ","
            << g_speed_before                                      << ","
            << g_fall_xyz.getX()                                   << ","
            << g_fall_xyz.getZ()                                   << "\n";
        out.close();
        Log::info("FallTest", "Saved respawn time to '%s'", path.c_str());
    }   // writeFile
}   // namespace

// ============================================================================
void FallTestLogger::onKartUpdate(AbstractKart* kart, float speed)
{
    if (!m_enabled || !g_race_active || !isTargetKart(kart))
        return;
    if (g_state == S_DONE)
        return;
    if (RewindManager::get()->isRewinding())
        return;
    World* w = World::getWorld();
    if (!w || !w->isActiveRacePhase())
        return;

    const bool rescuing =
        dynamic_cast<RescueAnimation*>(kart->getKartAnimation()) != NULL;

    // Drive forward by itself (does not depend on the input device, so it
    // also works on Android with --race-now)
    if (!rescuing && kart->getController())
        kart->getController()->action(PA_ACCEL, Input::MAX_VALUE);

    if (g_state == S_DRIVING)
    {
        if (!rescuing)
        {
            // Remember the values just before a possible track exit
            if (!kart->getKartAnimation())
            {
                g_last_speed = speed;
                g_last_xyz   = kart->getXYZ();
            }
            return;
        }
        // T1: rescue (respawn) started = track exit detected
        g_start_tick   = currentTick();
        g_start_ms     = getTimeMilliseconds();
        g_speed_before = g_last_speed;
        g_fall_xyz     = g_last_xyz;
        g_state        = S_RESCUING;
        return;
    }

    if (g_state == S_DRIVING_ON)
    {
        // Drive on with --auto-accel, then save and quit the game
        const bool driven = (kart->getXYZ() - g_respawn_xyz).length()
                            >= DRIVE_DISTANCE;
        const bool too_long = currentTick() - g_respawn_tick >
                              stk_config->time2Ticks(DRIVE_TIMEOUT_SECONDS);
        if (driven || too_long)
        {
            writeFile(m_log_dir, driven ? "ok" : "no_drive",
                      g_respawn_ms, g_respawn_ticks);
            g_state = S_DONE;
            // One measurement per run: quit the game
            if (main_loop)
                main_loop->requestAbort();
        }
        return;
    }

    // S_RESCUING
    if (!rescuing)
    {
        // T2: rescue animation over, kart can be controlled again.
        // Keep the result and let the kart drive on first.
        g_respawn_ticks = currentTick() - g_start_tick;
        g_respawn_ms    = getTimeMilliseconds() - g_start_ms;
        g_respawn_tick  = currentTick();
        g_respawn_xyz   = kart->getXYZ();
        g_state         = S_DRIVING_ON;
        return;
    }

    if (currentTick() - g_start_tick >
        stk_config->time2Ticks(TIMEOUT_SECONDS))
    {
        writeFile(m_log_dir, "timeout", -1.0, -1);
        g_state = S_DONE;
        if (main_loop)
            main_loop->requestAbort();
    }
}   // onKartUpdate

// ----------------------------------------------------------------------------
void FallTestLogger::onFrame(bool race_active)
{
    if (!m_enabled)
        return;

    if (race_active && !g_race_active)
    {
        resetAll();
        g_track = Track::getCurrentTrack() ?
                  Track::getCurrentTrack()->getIdent() : "unknown";
    }
    else if (!race_active && g_race_active)
    {
        if (g_state == S_RESCUING)
            writeFile(m_log_dir, "race_end", -1.0, -1);
        else if (g_state == S_DRIVING_ON)
            writeFile(m_log_dir, "no_drive", g_respawn_ms, g_respawn_ticks);
        g_state = S_DONE;
    }
    g_race_active = race_active;
}   // onFrame

// ----------------------------------------------------------------------------
void FallTestLogger::finish()
{
    if (!m_enabled)
        return;
    if (g_race_active && g_state == S_RESCUING)
        writeFile(m_log_dir, "race_end", -1.0, -1);
    else if (g_race_active && g_state == S_DRIVING_ON)
        writeFile(m_log_dir, "no_drive", g_respawn_ms, g_respawn_ticks);
    g_state       = S_DONE;
    g_race_active = false;
}   // finish
