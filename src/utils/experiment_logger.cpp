#include "utils/experiment_logger.hpp"
#include "config/user_config.hpp"
#include "network/network_config.hpp"
#include "network/stk_host.hpp"
#include "utils/log.hpp"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

#if defined(__ANDROID__)
#include <SDL_system.h>
#endif

// 열 구성이 바뀌면 이 문자열도 같이 바꿀 것 (기존 파일 헤더와 비교에 사용)
static const char* LOG_HEADER =
    "RoundID,EventID,Timestamp,NetTimeMs,EventType,Role,Platform,ActorID,VictimID,ScoreDelta";

ExperimentLogger* ExperimentLogger::get() {
    static ExperimentLogger instance;
    return &instance;
}

ExperimentLogger::ExperimentLogger() : m_event_counter(1), m_enabled(false) {
    std::string log_path_str;
#if defined(__ANDROID__)
    // 안드로이드 11+ 는 /sdcard 최상위에 파일을 만들 수 없으므로 앱 전용 폴더 사용
    // -> /storage/emulated/0/Android/data/<패키지명>/files/experiment_log.csv
    const char* app_dir = SDL_AndroidGetExternalStoragePath();
    if (app_dir == NULL)
        app_dir = SDL_AndroidGetInternalStoragePath();
    log_path_str = app_dir ? std::string(app_dir) + "/experiment_log.csv"
                           : "/sdcard/experiment_log.csv";
#elif defined(_WIN32) || defined(_WIN64)
    std::string file_path = __FILE__;
    size_t pos = file_path.find_last_of("\\/");
    if (pos != std::string::npos) {
        file_path = file_path.substr(0, pos);
        pos = file_path.find_last_of("\\/");
        if (pos != std::string::npos) {
            file_path = file_path.substr(0, pos);
            pos = file_path.find_last_of("\\/");
            if (pos != std::string::npos) {
                file_path = file_path.substr(0, pos);
            }
        }
    }
    log_path_str = file_path + "/experiment_log.csv";
#elif defined(__linux__)
    // 리눅스 서버를 위한 경로 설정
    log_path_str = "experiment_log.csv";
#else
    log_path_str = "experiment_log.csv";
#endif

    // 예전 열 구성으로 만든 파일이 있으면 섞이지 않게 백업 후 새로 시작
    {
        std::ifstream old_file(log_path_str);
        std::string first_line;
        if (old_file.is_open() && std::getline(old_file, first_line)) {
            if (!first_line.empty() && first_line.back() == '\r')
                first_line.pop_back();
            if (first_line != LOG_HEADER) {
                old_file.close();
                std::string backup = log_path_str + "." +
                    std::to_string((long long)std::time(NULL)) + ".old.csv";
                if (std::rename(log_path_str.c_str(), backup.c_str()) == 0)
                    Log::info("ExperimentLogger", "Old log moved to: %s", backup.c_str());
                else
                    Log::warn("ExperimentLogger", "Failed to back up old log: %s", log_path_str.c_str());
            }
        }
    }

    m_log_file.open(log_path_str, std::ios::app);
    if (m_log_file.is_open()) {
        m_log_file.seekp(0, std::ios::end);
        if (m_log_file.tellp() == 0) {
            m_log_file << LOG_HEADER << "\n";
            m_log_file.flush();
        }
        // std::cout 은 안드로이드에서 보이지 않으므로 Log 사용 (adb logcat 에 출력됨)
        Log::info("ExperimentLogger", "Log file created at: %s", log_path_str.c_str());
        m_enabled = true;
    }
    else {
        Log::error("ExperimentLogger", "Failed to open %s", log_path_str.c_str());
    }
}

ExperimentLogger::~ExperimentLogger() {
    if (m_log_file.is_open()) {
        m_log_file.close();
    }
}

// 이 프로그램이 서버인지 클라이언트인지 (온라인이 아니면 Local)
static const char* getRole() {
    if (!NetworkConfig::get()->isNetworking())
        return "Local";
    return NetworkConfig::get()->isServer() ? "Server" : "Client";
}

// 기기끼리 비교 가능한 ms 시각.
// 클라이언트는 접속 시 서버 시계에 맞춰지므로(서버 시각 + ping/2)
// 서버/클라이언트 값을 그대로 빼면 지연(ms)이 된다. 온라인이 아니면 -1.
static long long getNetTimeMs() {
    if (!NetworkConfig::get()->isNetworking() || !STKHost::existHost())
        return -1;
    return (long long)STKHost::get()->getNetworkTimer();
}

void ExperimentLogger::logEvent(int roundId,
    long long serverTimestamp,
    const std::string& eventType,
    const std::string& platform,
    int actorId,
    int victimId,
    int scoreDelta) {
    if (!UserConfigParams::m_score_log) return;
    if (!m_enabled) return;

    long long net_time_ms = getNetTimeMs();
    const char* role = getRole();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_log_file.is_open()) {
        m_log_file << roundId << ","
            << m_event_counter++ << ","
            << serverTimestamp << ","
            << net_time_ms << ","
            << eventType << ","
            << role << ","
            << platform << ","
            << actorId << ","
            << victimId << ","
            << scoreDelta << "\n";
        m_log_file.flush();
    }
}
