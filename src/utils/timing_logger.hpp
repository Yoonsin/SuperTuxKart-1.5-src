#ifndef HEADER_TIMING_LOGGER_HPP
#define HEADER_TIMING_LOGGER_HPP

#include <string>
#include <vector>
#include <cstdint>

class TimingLogger {
private:
    std::string m_log_prefix;
    unsigned int m_file_number = 0;
    bool m_enabled;
public:
	enum class TimingType
	{
        //Bowling
		Ready_Bowling = 0,  // 장전
        Fire_Bowling = 1,   // 던지기
		Hit_Bowling = 2,    // 히트
        
        //Bubble Gum (= Shield)
        Ready_Gum = 3,      // 장전
        Fire_Gum = 4,       // 전개
        Hit_Gum = 5,        // 막음
	};

    struct TimingData
    {
        uint32_t cycle_id;

        //타임스탬프
        double timeStamp;
        
        //싱글플레이어 : 실제 결과가 반영되는 물리틱 + 입력 요청을 보낼 때 물리틱 
        //멀티플레이어 (서버측 측정 - 서버) : 실제 결과가 반영되는 (클라틱 <= 서버틱 일 때) or 최초로 수신 확인하는 (클라틱 >= 서버틱) 물리틱
        int real_tick;

        //멀티플레이어 (클라이언트측 측정 - 클라이언트) : 입력 요청을 보낼 때 물리틱 
        //멀티플레이어 (서버측 측정 - 클라이언트) : 입력 요청을 보낼 때 물리틱 
        int predict_tick;

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
    bool setLogDirectory(const std::string& directory);
    void logEvent(double timeStamp,
        int real_tick,
        int predict_tick,
        TimingType type,
        const std::string& accountName,
        const std::string& platform,
        uint32_t cycle_id = 0);
    void update();
    bool saveCsv();
};

#endif // HEADER_TIMING_LOGGER_HPP
