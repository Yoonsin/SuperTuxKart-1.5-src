#include "utils/experiment_logger.hpp"
#include "config/stk_config.hpp"
#include "config/user_config.hpp"
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
        return true;
    }
    if (!file_manager->checkAndCreateDirectoryP(dir) ||
        !FileManager::isDirectory(dir))
        return false;

    if (dir.back() != '/')
        dir += '/';
    m_log_prefix = dir + "score_log_";
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
                                int score1)
{
    if (!m_enabled) return;
    if (!UserConfigParams::m_score_log) return;
    if (!UserConfigParams::m_auto_item_fire) return;

    m_score_data.push_back({timeStamp, roundId, shooterKart, shooterPlatform,
                            victimKart, victimPlatform, result, score0, score1});
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
    logEvent(getTimeMilliseconds(),
             getRoundId(created_ticks),
             shooterKart,
             getKartPlatform(shooterKart),
             is_hit ? victimKart : -1,
             is_hit ? getKartPlatform(victimKart) : "",
             is_hit ? Result::Hit : Result::Miss,
             ffa->getKartScore(0),
             ffa->getKartScore(1));
}

bool ExperimentLogger::saveCsv() {
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
            out << "Timestamp,RoundId,ShooterKart,ShooterPlatform,VictimKart,VictimPlatform,Result,Score0,Score1\n";
        out << std::setprecision(17);
        for (const auto& event : m_score_data) {
            std::string result = (event.result == Result::Hit) ? "HIT" : "MISS";
            out << event.timeStamp << ',' << event.roundId << ',' << event.shooterKart << ',';
            writeCsvField(out, event.shooterPlatform);
            out << ',' << event.victimKart << ',';
            writeCsvField(out, event.victimPlatform);
            out << ',';
            writeCsvField(out, result);
            out << ',' << event.score0 << ',' << event.score1 << '\n';
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
    const int round_ticks = stk_config->time2Ticks(10.0f);
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
