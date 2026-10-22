#include "utils/timing_logger.hpp"
#include "config/user_config.hpp"
#include "io/file_manager.hpp"
#include "utils/file_utils.hpp"
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

TimingLogger* TimingLogger::get() {
    static TimingLogger instance;
    return &instance;
}

TimingLogger::TimingLogger() : m_enabled(true) {
    m_log_prefix = file_manager->getUserConfigFile("timing_log_");
}

TimingLogger::~TimingLogger() {
    saveCsv();
}

bool TimingLogger::setLogDirectory(const std::string& directory)
{
    std::string dir = directory;
#ifdef _WIN32
    std::replace(dir.begin(), dir.end(), '\\', '/');
#endif
    if (dir.empty())
    {
        m_log_prefix = file_manager->getUserConfigFile("timing_log_");
        return true;
    }
    if (!file_manager->checkAndCreateDirectoryP(dir) ||
        !FileManager::isDirectory(dir))
        return false;

    if (dir.back() != '/')
        dir += '/';
    m_log_prefix = dir + "timing_log_";
    return true;
}

void TimingLogger::logEvent(double timeStamp,
                            int real_tick,
                            int predict_tick,
                            TimingType type,
                            const std::string& accountName,
                            const std::string& platform) 
{
    if (!m_enabled) return;
    if (!UserConfigParams::m_auto_item_fire) return;
    
	m_timing_data.push_back({timeStamp,real_tick,predict_tick,type,accountName,platform});
}

void TimingLogger::update() {

}

bool TimingLogger::saveCsv() {
    if (m_timing_data.empty()) return true;

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
            out << "TimestampMs,RealTick,PredictTick,EventType,AccountName,Platform\n";
        out << std::setprecision(17);
        for (const auto& event : m_timing_data) {
            std::string type;
            switch(event.type){
            case TimingType::Ready_Bowling:
                type = "Ready_Bowling";
                break;
            case TimingType::Ready_Gum:
                type = "Ready_Gum";
                break;
            case TimingType::Fire_Bowling:
                type = "Fire_Bowling";
                break;
            case TimingType::Fire_Gum:
                type = "Fire_Gum";
                break;
            case TimingType::Hit_Bowling:
                type = "Hit_Bowling";
                break;
            case TimingType::Hit_Gum:
                type = "Hit_Gum";
                break;
            default:
                type = "Unknown";
                break;
            }
            out << event.timeStamp << ',' << event.real_tick << ',' << event.predict_tick << ',';
            writeCsvField(out, type);
            out << ',';
            writeCsvField(out, event.accountName);
            out << ',';
            writeCsvField(out, event.platform);
            out << '\n';
        }
        out.close();
        if (out) {
            m_timing_data.clear();
            return true;
        }
    }
    std::cerr << "[TimingLogger] Failed to save " << log_path << std::endl;
    return false;
}
