#include "utils/experiment_logger.hpp"
#include "config/stk_config.hpp"
#include "config/user_config.hpp"
#include "input/input.hpp"
#include "io/file_manager.hpp"
#include "modes/free_for_all.hpp"
#include "network/network_config.hpp"
#include "network/network_player_profile.hpp"
#include "network/remote_kart_info.hpp"
#include "network/stk_peer.hpp"
#include "race/race_manager.hpp"
#include "utils/file_utils.hpp"
#include "utils/profiler.hpp"
#include "utils/string_utils.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>

static void writeCsvField(std::ostream& out, const std::string& value) {
    out << '"';
    for (char c : value) {
        if (c == '"') out << '"';
        out << c;
    }
    out << '"';
}

// --- 이 기기의 플랫폼 (싱글플레이어 / 서버 정보가 없을 때 사용) ---
static std::string getLocalPlatform() {
#if defined(__ANDROID__)
    return "Android";
#elif defined(_WIN32) || defined(_WIN64)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

ExperimentLogger* ExperimentLogger::get() {
    static ExperimentLogger instance;
    return &instance;
}

ExperimentLogger::ExperimentLogger() : m_enabled(true) {
    m_log_prefix = file_manager->getUserConfigFile("score_log_");
    m_input_log_prefix = file_manager->getUserConfigFile("input_log_");
}

ExperimentLogger::~ExperimentLogger() {
    saveCsv();
}

bool ExperimentLogger::setLogDirectory(const std::string& directory)
{
    std::string dir = directory;
#ifdef _WIN32
    std::replace(dir.begin(), dir.end(), '\\', '/');
#endif
    if (dir.empty())
    {
        m_log_prefix = file_manager->getUserConfigFile("score_log_");
        m_input_log_prefix = file_manager->getUserConfigFile("input_log_");
        return true;
    }
    if (!file_manager->checkAndCreateDirectoryP(dir) ||
        !FileManager::isDirectory(dir))
        return false;

    if (dir.back() != '/')
        dir += '/';
    m_log_prefix = dir + "score_log_";
    m_input_log_prefix = dir + "input_log_";   // [input-log] score-log와 같은 폴더
    return true;
}

void ExperimentLogger::logEvent(double timeStamp,
                                int roundId,
                                int shooterKart,
                                const std::string& shooterPlatform,
                                int victimKart,
                                const std::string& victimPlatform,
                                Result result,
                                int score0,
                                int score1,
                                int shotTick,
                                int dodgeTick,
                                int dodgeDelay,
                                int endTick)
{
    if (!m_enabled) return;
    if (!UserConfigParams::m_score_log) return;
    if (!UserConfigParams::m_auto_item_fire) return;

    m_score_data.push_back({timeStamp, roundId, shooterKart, shooterPlatform,
                            victimKart, victimPlatform, result, score0, score1,
                            shotTick, dodgeTick, dodgeDelay, endTick});
}

// ----------------------------------------------------------------------------
/** 볼링공 한 발의 결과(HIT/MISS)를 기록합니다. 서버(또는 싱글플레이어)에서만
 *  호출되어야 합니다.
 *  \param created_ticks 볼링공이 만들어진 틱 (RoundId 계산용).
 *  \param shooterKart 쏜 카트 번호.
 *  \param victimKart 맞은 카트 번호 (MISS일 경우 -1).
 */
void ExperimentLogger::logShot(int created_ticks, int shooterKart, int victimKart)
{
    FreeForAll* ffa = dynamic_cast<FreeForAll*>(World::getWorld());
    if (!ffa || ffa->getNumKarts() != 2) return;

    const bool is_hit = victimKart >= 0;

    // [auto-item-avoid] 서버 틱 계산 (하나의 시간축)
    const int target_kart = 1 - shooterKart;              // 공을 맞아야 하는 쪽 (카트 2대뿐)
    const int end_tick    = World::getWorld()->getTicksSinceStart();
    // 공이 생긴 라운드의 회피 틱을 꺼냄 (그 라운드에 회피가 없었으면 -1)
    const int dodge_tick  = ffa->getDodgeTickInRound(target_kart, getRoundId(created_ticks));
    const int dodge_delay = (dodge_tick >= 0) ? dodge_tick - created_ticks : -1;

    logEvent(getTimeMilliseconds(),
             getRoundId(created_ticks),
             shooterKart,
             getKartPlatform(shooterKart),
             is_hit ? victimKart : -1,
             is_hit ? getKartPlatform(victimKart) : "",
             is_hit ? Result::Hit : Result::Miss,
             ffa->getKartScore(0),
             ffa->getKartScore(1),
             created_ticks, dodge_tick, dodge_delay, end_tick);
}

// ----------------------------------------------------------------------------
/** [input-log] 서버가 클라이언트의 발사·회피 "누름" 입력을 받을 때 호출됩니다.
 *  네트워크 스레드에서 불리므로 값만 저장하고, 계산은 저장할 때 합니다.
 *  \param kart 입력한 카트 번호.
 *  \param action PlayerAction (PA_FIRE / PA_STEER_LEFT / PA_LOOK_BACK).
 *  \param client_tick 입력에 적혀 온 틱 (클라이언트가 눌렀다는 틱).
 *  \param server_recv_tick 서버가 입력을 받은 순간의 서버 틱.
 */
void ExperimentLogger::logInput(int kart, int action, int client_tick,
                                int server_recv_tick)
{
    if (!m_enabled) return;
    std::lock_guard<std::mutex> lock(m_input_mutex);
    m_input_data.push_back({kart, action, client_tick, server_recv_tick});
}

// ----------------------------------------------------------------------------
/** [input-log] 입력을 기록해야 하는지 확인합니다. 네트워크 스레드에서
 *  호출되므로 World 에는 접근하지 않습니다. 서버 전용입니다. */
bool ExperimentLogger::shouldLogInput()
{
    return UserConfigParams::m_score_log && UserConfigParams::m_auto_item_fire &&
           NetworkConfig::get()->isNetworking() && NetworkConfig::get()->isServer();
}

// ----------------------------------------------------------------------------
/** [input-log] 모아 둔 입력을 input_log_*.csv 로 한 번에 저장합니다. */
bool ExperimentLogger::saveInputCsv()
{
    std::vector<InputData> data;
    {
        std::lock_guard<std::mutex> lock(m_input_mutex);
        if (m_input_data.empty()) return true;
        data.swap(m_input_data);
    }

    const std::time_t now = std::time(nullptr);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&now));
    std::string log_path;
    struct stat info;
    do {
        std::ostringstream name;
        name << m_input_log_prefix << timestamp << '_'
             << std::setfill('0') << std::setw(3) << ++m_input_file_number << ".csv";
        log_path = name.str();
    } while (FileUtils::statU8Path(log_path, &info) == 0);

    std::ofstream out(log_path, std::ios::app);
    if (out) {
        out << "RoundId,Kart,Platform,Action,ServerRecvTick,ExecTick,ClientTick,LateTicks\n";
        for (const auto& in : data) {
            const char* action = (in.action == PA_FIRE) ? "FIRE" : "DODGE";
            const int exec_tick = std::max(in.clientTick, in.serverRecvTick);
            out << getRoundId(in.serverRecvTick) << ',' << in.kart << ',';
            writeCsvField(out, getKartPlatform(in.kart));
            out << ',' << action << ',' << in.serverRecvTick << ',' << exec_tick
                << ',' << in.clientTick << ',' << (in.serverRecvTick - in.clientTick) << '\n';
        }
        out.close();
        if (out) return true;
    }
    std::cerr << "[ExperimentLogger] Failed to save " << log_path << std::endl;
    return false;
}

bool ExperimentLogger::saveCsv() {
    saveInputCsv();   // [input-log] score-log와 같은 시점에 저장
    if (m_score_data.empty()) return true;

    const std::time_t now = std::time(nullptr);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&now));
    std::string log_path;
    struct stat info;
    do {
        std::ostringstream name;
        name << m_log_prefix << timestamp << '_'
             << std::setfill('0') << std::setw(3) << ++m_file_number << ".csv";
        log_path = name.str();
    } while (FileUtils::statU8Path(log_path, &info) == 0);

    std::ofstream out(log_path, std::ios::app);
    if (out) {
        out.seekp(0, std::ios::end);
        if (out.tellp() == 0)
            out << "Timestamp,RoundId,ShooterKart,ShooterPlatform,VictimKart,VictimPlatform,"
                   "Result,Score0,Score1,ShotTick,DodgeTick,DodgeDelay,EndTick\n";
        out << std::setprecision(17);
        for (const auto& event : m_score_data) {
            std::string result = (event.result == Result::Hit) ? "HIT" : "MISS";
            out << event.timeStamp << ',' << event.roundId << ',' << event.shooterKart << ',';
            writeCsvField(out, event.shooterPlatform);
            out << ',' << event.victimKart << ',';
            writeCsvField(out, event.victimPlatform);
            out << ',';
            writeCsvField(out, result);
            out << ',' << event.score0 << ',' << event.score1
                << ',' << event.shotTick << ',' << event.dodgeTick
                << ',' << event.dodgeDelay << ',' << event.endTick << '\n';
        }
        out.close();
        if (out) {
            m_score_data.clear();
            return true;
        }
    }
    std::cerr << "[ExperimentLogger] Failed to save " << log_path << std::endl;
    return false;
}

// ----------------------------------------------------------------------------
/** 볼링공 결과를 기록해야 하는지 확인합니다. 점수는 서버가 결정하므로
 *  온라인에서는 서버에서만, 싱글플레이어에서는 그 기기에서 기록합니다. */
bool ExperimentLogger::shouldLogShot()
{
    if (!UserConfigParams::m_score_log || !UserConfigParams::m_auto_item_fire)
        return false;
    if (NetworkConfig::get()->isNetworking() && !NetworkConfig::get()->isServer())
        return false;
    return dynamic_cast<FreeForAll*>(World::getWorld()) != NULL;
}

// ----------------------------------------------------------------------------
/** 발사 라운드 번호. 클라이언트는 10초(1200틱)마다 한 번 발사하므로
 *  볼링공이 만들어진 틱으로 몇 번째 발사인지 계산합니다. */
int ExperimentLogger::getRoundId(int created_ticks)
{
    const int round_ticks = stk_config->time2Ticks(3.0f);   // 발사 주기 3초
    return created_ticks / round_ticks + 1;
}

// ----------------------------------------------------------------------------
/** 카트를 조종하는 플레이어 기기의 플랫폼을 반환합니다. 서버에서는 접속한
 *  클라이언트의 user agent 에서 OS 를 꺼냅니다. */
std::string ExperimentLogger::getKartPlatform(int kart_id)
{
    if (!NetworkConfig::get()->isNetworking())
        return getLocalPlatform();

    std::shared_ptr<NetworkPlayerProfile> profile =
        RaceManager::get()->getKartInfo(kart_id).getNetworkPlayerProfile().lock();
    if (!profile)
        return "AI";
    std::shared_ptr<STKPeer> peer = profile->getPeer();
    if (!peer)
        return "Unknown";
    if (peer->isAIPeer())
        return "AI";
    std::string os = StringUtils::extractVersionOS(peer->getUserVersion()).second;
    return os.empty() ? "Unknown" : os;
}
