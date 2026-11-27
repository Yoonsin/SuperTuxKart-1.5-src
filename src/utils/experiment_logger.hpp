#ifndef HEADER_EXPERIMENT_LOGGER_HPP
#define HEADER_EXPERIMENT_LOGGER_HPP

#include <mutex>
#include <string>
#include <vector>

class ExperimentLogger {
private:
    std::string m_log_prefix;
    std::string m_input_log_prefix;   // [input-log] input_log_*.csv 저장 경로 접두어
    unsigned int m_input_file_number = 0;
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

        // [auto-item-avoid] 서버 틱 기록 (하나의 시간축)
        int shotTick;    // 공이 생긴 틱
        int dodgeTick;   // 맞는 쪽 카트의 회피 시작 틱 (-1 = 이번 라운드 회피 없음)
        int dodgeDelay;  // dodgeTick - shotTick (-1 = 회피 없음)
        int endTick;     // 맞은 틱(HIT) 또는 공이 사라진 틱(MISS)
    };
    std::vector<ScoreData> m_score_data;

    // [input-log] 서버가 받은 발사·회피 입력 1번 = 1줄
    struct InputData
    {
        int kart;            // 카트 번호
        int action;          // PlayerAction (PA_FIRE / PA_STEER_LEFT / PA_LOOK_BACK)
        int clientTick;      // 입력에 적혀 온 틱 (클라이언트가 눌렀다는 틱)
        int serverRecvTick;  // 서버가 입력을 받은 순간의 서버 틱
    };
    std::vector<InputData> m_input_data;
    std::mutex m_input_mutex;   // 입력은 네트워크 스레드에서 들어오므로 잠금 필요

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
        int score1,
        int shotTick,
        int dodgeTick,
        int dodgeDelay,
        int endTick);
    void logShot(int created_ticks, int shooterKart, int victimKart);
    // [input-log] 네트워크 스레드에서 호출됨 (서버 전용)
    void logInput(int kart, int action, int client_tick, int server_recv_tick);
    static bool shouldLogInput();
    bool saveCsv();
    bool saveInputCsv();

    static bool shouldLogShot();
    static int getRoundId(int created_ticks);
    static std::string getKartPlatform(int kart_id);
};

#endif // HEADER_EXPERIMENT_LOGGER_HPP
