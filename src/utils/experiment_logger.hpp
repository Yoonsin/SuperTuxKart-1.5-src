#ifndef HEADER_EXPERIMENT_LOGGER_HPP
#define HEADER_EXPERIMENT_LOGGER_HPP

#include <string>
#include <vector>

class ExperimentLogger {
private:
    std::string m_log_prefix;
    unsigned int m_file_number = 0;
    bool m_enabled;

    ExperimentLogger();

public:
    enum class Result
    {
        Hit = 0,  // 득점
        Miss = 1, // 빗나감
    };

    struct ScoreData
    {
        //타임스탬프
        double timeStamp;

        //발사 라운드 (10초 주기)
        int roundId;

        //쏜 카트 번호, 플랫폼
        int shooterKart;
        std::string shooterPlatform;

        //맞은 카트 번호, 플랫폼 (MISS일 경우 -1, "")
        int victimKart;
        std::string victimPlatform;

        //결과
        Result result;

        //두 카트의 누적 점수
        int score0;
        int score1;
    };
    std::vector<ScoreData> m_score_data;

    static ExperimentLogger* get();
    ~ExperimentLogger();

    void setEnable(bool enable) { m_enabled = enable; }
    bool isEnabled() const { return m_enabled; }
    bool setLogDirectory(const std::string& directory);
    void logEvent(double timeStamp,
        int roundId,
        int shooterKart,
        const std::string& shooterPlatform,
        int victimKart,
        const std::string& victimPlatform,
        Result result,
        int score0,
        int score1);
    void logShot(int created_ticks, int shooterKart, int victimKart);
    bool saveCsv();

    static bool shouldLogShot();
    static int getRoundId(int created_ticks);
    static std::string getKartPlatform(int kart_id);
};

#endif // HEADER_EXPERIMENT_LOGGER_HPP
