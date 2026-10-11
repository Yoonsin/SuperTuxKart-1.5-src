#include "utils/experiment_logger.hpp"
#include "config/user_config.hpp"
#include <iostream>
#include <cstdlib>
#include <mutex>

ExperimentLogger* ExperimentLogger::get() {
    static ExperimentLogger instance;
    return &instance;
}

ExperimentLogger::ExperimentLogger() : m_event_counter(1), m_enabled(false) {
    std::string log_path_str;
#if defined(__ANDROID__)
    const char* external = std::getenv("EXTERNAL_STORAGE");
    log_path_str = external ? std::string(external) + "/experiment_log.csv" : "/sdcard/experiment_log.csv";
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

    m_log_file.open(log_path_str, std::ios::app);
    if (m_log_file.is_open()) {
        m_log_file.seekp(0, std::ios::end);
        if (m_log_file.tellp() == 0) {
            m_log_file << "RoundID,EventID,Timestamp,EventType,Platform,ActorID,VictimID,ScoreDelta\n";
            m_log_file.flush();
        }
        std::cout << "[ExperimentLogger] Log file created at: " << log_path_str << std::endl;
        m_enabled = true;
    }
    else {
        std::cerr << "[ExperimentLogger] Failed to open " << log_path_str << std::endl;
    }
}

ExperimentLogger::~ExperimentLogger() {
    if (m_log_file.is_open()) {
        m_log_file.close();
    }
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

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_log_file.is_open()) {
        m_log_file << roundId << ","
            << m_event_counter++ << ","
            << serverTimestamp << ","
            << eventType << ","
            << platform << ","
            << actorId << ","
            << victimId << ","
            << scoreDelta << "\n";
        m_log_file.flush();
    }
}
