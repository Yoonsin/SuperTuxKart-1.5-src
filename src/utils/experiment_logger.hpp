#ifndef HEADER_EXPERIMENT_LOGGER_HPP
#define HEADER_EXPERIMENT_LOGGER_HPP

#include <string>
#include <fstream>
#include <mutex>

class ExperimentLogger {
private:
    std::ofstream m_log_file;
    std::mutex m_mutex;
    int m_event_counter;
    bool m_enabled;

    ExperimentLogger();

public:
    static ExperimentLogger* get();
    ~ExperimentLogger();

    void setEnable(bool enable) { m_enabled = enable; }
    bool isEnabled() const { return m_enabled; }

    void logEvent(int roundId,
        long long serverTimestamp,
        const std::string& eventType,
        const std::string& platform,
        int actorId,
        int victimId,
        int scoreDelta);
};

#endif // HEADER_EXPERIMENT_LOGGER_HPP