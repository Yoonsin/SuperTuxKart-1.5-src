#include "utils/experiment_logger.hpp"
#include <iostream>
#include <cstdlib>
#include <mutex>        // 필요하면 추가

ExperimentLogger* ExperimentLogger::get() {
    static ExperimentLogger instance;
    return &instance;
}

ExperimentLogger::ExperimentLogger() : m_event_counter(1), m_enabled(false) {
    std::string log_path_str;
#if defined(__ANDROID__)
    // Android: use external storage or app‑specific directory
    const char* external = std::getenv("EXTERNAL_STORAGE");
    log_path_str = external ? std::string(external) + "/experiment_log.csv" : "/sdcard/experiment_log.csv";
#elif defined(_WIN32) || defined(_WIN64)
    // Windows: use simple string manipulation of __FILE__ to avoid <filesystem> error
    std::string file_path = __FILE__;
    size_t pos = file_path.find_last_of("\\/");
    if (pos != std::string::npos) {
        file_path = file_path.substr(0, pos); // -> src/utils
        pos = file_path.find_last_of("\\/");
        if (pos != std::string::npos) {
            file_path = file_path.substr(0, pos); // -> src
            pos = file_path.find_last_of("\\/");
            if (pos != std::string::npos) {
                file_path = file_path.substr(0, pos); // -> project root
            }
        }
    }
    log_path_str = file_path + "/experiment_log.csv";
#else
    // Fallback: current working directory
    log_path_str = "experiment_log.csv";
#endif

    m_log_file.open(log_path_str, std::ios::app);
    if (m_log_file.is_open()) {
        // Write CSV header if file is new/empty
        m_log_file.seekp(0, std::ios::end);
        if (m_log_file.tellp() == 0) {
            m_log_file << "RoundID,EventID,ServerTimestamp,Platform,TargetPlatform,ScoreDelta\n";
            m_log_file.flush();
        }
        std::cout << "[ExperimentLogger] Log file created at: " << log_path_str << std::endl;
        m_enabled = true;          // enable logging after successful open
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
                                const std::string& platform,
                                const std::string& targetPlatform,
                                int scoreDelta) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_log_file.is_open()) {
        m_log_file << roundId << ","
                   << m_event_counter++ << ","
                   << serverTimestamp << ","
                   << platform << ","
                   << targetPlatform << ","
                   << scoreDelta << "\n";
        m_log_file.flush();
    }
}