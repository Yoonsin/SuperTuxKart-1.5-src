#ifndef HEADER_TIMING_LOGGER_HPP
#define HEADER_TIMING_LOGGER_HPP

#include <string>
#include <vector>

class TimingLogger {
private:
    std::string m_log_prefix;
    unsigned int m_file_number = 0;
    bool m_enabled;
public:
	enum class TimingType
	{
		Ready = 0, // 준비
        Throw = 1, // 던짐
		Hit = 2, // 히트
	};

    struct TimingData
    {
        //타임스탬프
        double timeStamp;

        //현재 물리 틱
        int tick;

		//이벤트 타입
        TimingType type;

        //계정 이름
        std::string accountName;

        //플랫폼
        std::string platform;
    };    
	std::vector<TimingData> m_timing_data;

    static TimingLogger* get();
    TimingLogger();
    ~TimingLogger();

    void setEnable(bool enable) { m_enabled = enable; }
    bool isEnabled() const { return m_enabled; }
    void logEvent(double timeStamp,
        int tick,
        TimingType type,
        const std::string& accountName,
        const std::string& platform);
    void update();
    bool saveCsv();
};

#endif // HEADER_TIMING_LOGGER_HPP
