//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "modes/free_for_all.hpp"
#include "config/user_config.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "karts/controller/ai_base_controller.hpp"
#include "network/network_config.hpp"
#include "network/network_string.hpp"
#include "network/rewind_manager.hpp"
#include "network/protocols/game_events_protocol.hpp"
#include "network/stk_host.hpp"
#include "tracks/track.hpp"
#include "utils/string_utils.hpp"
#include "utils/experiment_logger.hpp"
#include "utils/timing_logger.hpp"
#include "utils/profiler.hpp"

#include "items/powerup_manager.hpp"
#include "items/powerup.hpp"
#include "items/attachment.hpp"
#include "karts/kart_properties.hpp"
#include "utils/vec3.hpp"

#include <limits>
#include <algorithm>
#include <utility>
#include <sstream>
#include <ctime>
#include <iostream>

#include "karts/controller/player_controller.hpp"   // getSteerInput()
#include "physics/btKart.hpp"                        // updateAllWheelPositions()

// ====================================================================
// [auto-item-avoid] 회피 실험 설정값 — 실험할 때는 여기 숫자만 바꾸세요
// ====================================================================
static const float AVOID_DISTANCE     = 3.0f; // 회피 거리 (미터)
static const int   AVOID_PRESS_DELAY  = 29;   // 발사 후 몇 틱 뒤에 회피 버튼을 누를지
static const int   AVOID_HOLD_BUTTON  = 20;   // 회피 버튼을 몇 틱 동안 누르고 있을지
static const int   AVOID_MOVE_TICKS   = 20;   // 옆으로 이동하는 시간 (틱)
static const int   AVOID_STAY_TICKS   = 60;   // 옆에 머무는 시간 (틱)
static const int   AVOID_RETURN_TICKS = 20;   // 원래 자리로 돌아오는 시간 (틱)
// ====================================================================

// ----------------------------------------------------------------------------
/** Constructor. Sets up the clock mode etc.
 */
FreeForAll::FreeForAll() : WorldWithRank()
{
    if (RaceManager::get()->hasTimeTarget())
    {
        WorldStatus::setClockMode(WorldStatus::CLOCK_COUNTDOWN,
            RaceManager::get()->getTimeTarget());
    }
    else
    {
        WorldStatus::setClockMode(CLOCK_CHRONO);
    }
}   // FreeForAll

// ----------------------------------------------------------------------------
FreeForAll::~FreeForAll()
{
    TimingLogger::get()->saveCsv();
    ExperimentLogger::get()->saveCsv();
}   // ~FreeForAll

static int s_ffa_round_id = 0; // 판을 구분하기 위한 전역 변수
// --- 플랫폼 확인 헬퍼 함수 ---
static std::string getCurrentPlatform() {
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
// -----------------------------
// ----------------------------------------------------------------------------
void FreeForAll::init()
{
    WorldWithRank::init();
    m_display_rank = false;
    m_count_down_reached_zero = false;
    m_use_highscores = false;

    // 새 게임이 시작될 때마다 라운드 ID 증가
    s_ffa_round_id++;

    m_duel_ticks = 0;
    m_duel_fired = false;

    // C++ 하드코딩 스폰 강제 고정 코드는 삭제! 
    // 이제 위치와 방향은 오직 scene_score.xml 의 완벽한 설정만을 따릅니다.
}
// ----------------------------------------------------------------------------
/** Called when a battle is restarted.
 */
void FreeForAll::reset(bool restart)
{
    if (restart)
    {
        TimingLogger::get()->saveCsv();
        ExperimentLogger::get()->saveCsv();
    }
    WorldWithRank::reset(restart);
    if (UserConfigParams::m_auto_item_fire && getNumKarts() == 2)
    {
        for (int i = 0; i < 2; i++) {
            auto kart = getKart(i);
            kart->setPowerup(PowerupManager::POWERUP_BOWLING, 255);
            kart->makeKartRest();
            kart->getBody()->setLinearFactor(Vec3(0.0f));
            kart->getBody()->setAngularFactor(Vec3(0.0f));
            m_home_trans[i] = kart->getTrans();   // [auto-item-avoid] 지금 위치 = 원래 자리
        }
    }
    m_duel_ticks = 0;
    m_duel_fired = false;
    // [auto-item-avoid] 회피 상태 초기화
    for (int i = 0; i < 2; i++)
    {
        m_dodge_start_tick[i] = -1;
        m_dodge_tick_by_round[i].clear();
        m_prev_steer[i]       = false;
    }
    m_count_down_reached_zero = false;
    if (RaceManager::get()->hasTimeTarget())
    {
        WorldStatus::setClockMode(WorldStatus::CLOCK_COUNTDOWN,
            RaceManager::get()->getTimeTarget());
    }
    else
    {
        WorldStatus::setClockMode(CLOCK_CHRONO);
    }
    m_scores.clear();
    m_scores.resize(getNumKarts(), 0);
}   // reset

// ----------------------------------------------------------------------------
/** Called when the match time ends.
 */
void FreeForAll::countdownReachedZero()
{
    // Prevent negative time in network soccer when finishing
    m_time_ticks = 0;
    m_time = 0.0f;
    m_count_down_reached_zero = true;
}   // countdownReachedZero

// ----------------------------------------------------------------------------
/** Called when a kart is hit.
 *  \param kart_id The world kart id of the kart that was hit.
 *  \param hitter The world kart id of the kart who hit(-1 if none).
 */
bool FreeForAll::kartHit(int kart_id, int hitter)
{
    //std::cout << "[FFA_DEBUG] kartHit ENTER kart_id=" << kart_id
        //<< " hitter=" << hitter << std::endl;

    if (NetworkConfig::get()->isNetworking() &&
        NetworkConfig::get()->isClient())
        return false;

    if (isRaceOver())
        return false;

    handleScoreInServer(kart_id, hitter);
    return true;
}   // kartHit

// ----------------------------------------------------------------------------
/** Called when the score of kart needs updated.
 *  \param kart_id The world kart id of the kart that was hit.
 *  \param hitter The world kart id of the kart who hit(-1 if none).
 */
void FreeForAll::handleScoreInServer(int kart_id, int hitter)
{
    //std::cout << "[FFA_DEBUG] kartHit Triggered! Target (kart_id): " << kart_id << ", Attacker (hitter): " << hitter << std::endl;
    // 카트의 점수가 업데이트되어야 할 때 호출됩니다.
    // \param kart_id : 피격당한(맞은) 카트의 월드 카트 ID입니다.
    // \param hitter : 타격한(때린) 카트의 월드 카트 ID입니다(공격자가 없을 경우 - 1).

    int new_score = 0;
    int score_delta = 0;
    int ticks = World::getWorld()->getTicksSinceStart();
    std::string platform = getCurrentPlatform();

    if (kart_id == hitter || hitter == -1)
    {
        new_score = --m_scores[kart_id];
        score_delta = -1;
    }
    else
    {
        //점수 득점일 때만 체크
        if (UserConfigParams::m_auto_item_fire && UserConfigParams::m_timing_log) {
            TimingLogger::get()->logEvent(getTimeMilliseconds(), ticks, TimingLogger::TimingType::Hit, "name", platform);
        }
        new_score = ++m_scores[hitter]; 
        score_delta = 1;
    }

    if (NetworkConfig::get()->isNetworking() && NetworkConfig::get()->isServer())
    {
        NetworkString p(PROTOCOL_GAME_EVENTS);
        p.setSynchronous(true);
        p.addUInt8(GameEventsProtocol::GE_BATTLE_KART_SCORE);
        if (kart_id == hitter || hitter == -1)
            p.addUInt8((uint8_t)kart_id).addUInt16((int16_t)new_score);
        else
            p.addUInt8((uint8_t)hitter).addUInt16((int16_t)new_score);
        STKHost::get()->sendPacketToAllPeers(&p, true);
    }
} // handleScoreInServer

// ----------------------------------------------------------------------------
void FreeForAll::setKartScoreFromServer(NetworkString& ns)
{
    int kart_id = ns.getUInt8();
    int16_t score = ns.getUInt16();

    m_scores.at(kart_id) = score;
}
// ----------------------------------------------------------------------------
/** Returns the internal identifier for this race.
 */
const std::string& FreeForAll::getIdent() const
{
    return IDENT_FFA;
}   // getIdent

// ------------------------------------------------------------------------
void FreeForAll::update(int ticks)
{
    WorldWithRank::update(ticks);
    WorldWithRank::updateTrack(ticks);
    if (Track::getCurrentTrack()->hasNavMesh())
        updateSectorForKarts();

    std::vector<std::pair<int, int> > ranks;
    for (unsigned i = 0; i < m_scores.size(); i++)
    {
        int cur_score = getKart(i)->isEliminated() ?
            std::numeric_limits<int>::min() : m_scores[i];
        ranks.emplace_back(i, cur_score);
    }
    std::sort(ranks.begin(), ranks.end(),
        [](const std::pair<int, int>& a, const std::pair<int, int>& b)
        {
            return a.second > b.second;
        });
    beginSetKartPositions();
    for (unsigned i = 0; i < ranks.size(); i++)
        setKartPosition(ranks[i].first, i + 1);
    endSetKartPositions();

    // ====================================================================
    // [2] 듀얼 모드: 무한 반복 장전 및 동시 발사 (--auto-item-fire 플래그 전용)
    if (UserConfigParams::m_auto_item_fire && getNumKarts() == 2)
    {
        if (NetworkConfig::get()->isNetworking()) 
        {
            if(NetworkConfig::get()->isClient())
                setKartTimingEvent(ticks);
            else if (UserConfigParams::m_auto_item_avoid ||
                     UserConfigParams::m_manual_item_avoid)   // [auto/manual-item-avoid] 서버
                updateDodge();
        }
        else
        {
            setKartTimingEvent(ticks);
            if (UserConfigParams::m_auto_item_avoid ||
                UserConfigParams::m_manual_item_avoid)      // [auto/manual-item-avoid] 오프라인 테스트용
                updateDodge();
        }
    }
    // ====================================================================
}   // update

void FreeForAll::setKartTimingEvent(int ticks)
{
    if (RewindManager::get()->isRewinding()) // rewind 중일 때는 재생 X
        return;

    if (isActiveRacePhase())
    {
        int old_ticks = m_duel_ticks;
        m_duel_ticks += ticks;
        int shoot_tick = 240; // 2초 (240틱) 단위로 반복
        int reset_tick = 1200; // 10초 (1200틱) 단위로 반복
        int release_tick = shoot_tick + 120; // 누른 뒤 120틱(1초) 뒤에 뗌
        int dodge_press_tick   = shoot_tick + AVOID_PRESS_DELAY;          // [auto-item-avoid] 회피 버튼 누름
        int dodge_release_tick = dodge_press_tick + AVOID_HOLD_BUTTON;    // [auto-item-avoid] 회피 버튼 뗌

        for (int kart_id = 0; kart_id < 2; kart_id++) {
            // 만약 네트워크 게임이고 내 로컬 플레이어가 아니면 입력 제외
            if (NetworkConfig::get()->isNetworking() && !getKart(kart_id)->getController()->isLocalPlayerController())
                continue;

            getKart(kart_id)->getControls().setSteer(0.0f);     // 조향(회전) 차단
            getKart(kart_id)->getControls().setAccel(false);    // 가속 차단
            getKart(kart_id)->getControls().setBrake(false);    // 브레이크 차단
            if (!UserConfigParams::m_manual_item_avoid)         // [manual-item-avoid] 수동 회피 모드에서는 회피 버튼(뒤 보기) 유지
                getKart(kart_id)->getControls().setLookBack(false); // 뒤보기 차단
            
            int player_kart_id = (getKart(kart_id)->getController()->isPlayerController()) ? kart_id : -1;
			//if (player_kart_id != kart_id) continue; //플레이어만 던지고 싶을 때 주석 해제
			if (!getKart(kart_id)->getController()) continue;

            // shoot_tick이 되는 순간에 딱 한 번 '발사' 시간 기록
            if (old_ticks < shoot_tick && m_duel_ticks >= shoot_tick)
            {
                if (dynamic_cast<AIBaseController*>(getKart(kart_id)->getController()))
                    getKart(kart_id)->getControls().setFire(true);
                else
                    getKart(kart_id)->getController()->action(PlayerAction::PA_FIRE, 1);
                
                if (UserConfigParams::m_timing_log) {
                    TimingLogger::get()->logEvent(getTimeMilliseconds(), World::getWorld()->getTicksSinceStart(), TimingLogger::TimingType::Throw, "name", getCurrentPlatform());
                }
                //Log::info("kart ID", "%s", getKart(i)->getController()->getControllerName());
                // //Log::info("ticks", "%d", m_duel_ticks);
            }

			// 누른 뒤 120틱이 지나면 발사 버튼을 뗌
            if (old_ticks < release_tick && m_duel_ticks >= release_tick)
            {
                if (dynamic_cast<AIBaseController*>(getKart(kart_id)->getController()))
                    getKart(kart_id)->getControls().setFire(false);
                else
                    getKart(kart_id)->getController()->action(PlayerAction::PA_FIRE, 0);
            }

            // [auto-item-avoid] 회피 버튼 누르고 떼기 (발사와 같은 모양)
            if (UserConfigParams::m_auto_item_avoid)
            {
                if (old_ticks < dodge_press_tick && m_duel_ticks >= dodge_press_tick)
                    getKart(kart_id)->getController()->action(PlayerAction::PA_STEER_LEFT, 32768);

                if (old_ticks < dodge_release_tick && m_duel_ticks >= dodge_release_tick)
                    getKart(kart_id)->getController()->action(PlayerAction::PA_STEER_LEFT, 0);
            }
        }
        if (m_duel_ticks >= reset_tick) m_duel_ticks = 0;
    }
}

// ----------------------------------------------------------------------------
/** [auto-item-avoid] 서버(또는 오프라인)에서 회피 버튼 입력을 감지하고,
 *  카트를 옆으로 옮겼다가 원래 자리로 되돌립니다. 모든 틱은 서버 틱입니다.
 */
void FreeForAll::updateDodge()
{
    if (RewindManager::get()->isRewinding() || !isActiveRacePhase())
        return;

    const int now = getTicksSinceStart();          // 서버 틱 (하나의 시간축)

    for (int i = 0; i < 2; i++)
    {
        AbstractKart* kart = getKart(i);

        // ① 회피 버튼이 눌렸는지 확인
        PlayerController* pc = dynamic_cast<PlayerController*>(kart->getController());
        // [manual-item-avoid] 수동 모드: 뒤 보기(D키 / 거울 버튼), 자동 모드: 조향 입력
        bool pressed = UserConfigParams::m_manual_item_avoid
                     ? kart->getControls().getLookBack()
                     : (pc && pc->getSteerInput() != 0);

        // ② "방금 눌렀다" → 회피 시작 (이미 회피 중이면 무시)
        if (pressed && !m_prev_steer[i] && m_dodge_start_tick[i] < 0)
        {
            m_dodge_start_tick[i] = now;
            m_dodge_tick_by_round[i][ExperimentLogger::getRoundId(now)] = now;  // 기록용 (라운드별 저장)
        }
        m_prev_steer[i] = pressed;

        // ③ 회피 중이 아니면 넘어감
        if (m_dodge_start_tick[i] < 0) continue;

        // ④ 맞아서 폭발 애니메이션 중이면 위치를 건드리지 않음
        if (kart->getKartAnimation()) continue;

        // ⑤ 지난 틱 수에 따라 옆으로 얼마나 비킬지 계산
        int e = now - m_dodge_start_tick[i];
        float offset;
        if (e < AVOID_MOVE_TICKS)                                              // 이동 중
            offset = AVOID_DISTANCE * e / AVOID_MOVE_TICKS;
        else if (e < AVOID_MOVE_TICKS + AVOID_STAY_TICKS)                      // 머무는 중
            offset = AVOID_DISTANCE;
        else if (e < AVOID_MOVE_TICKS + AVOID_STAY_TICKS + AVOID_RETURN_TICKS) // 복귀 중
            offset = AVOID_DISTANCE *
                (1.0f - float(e - AVOID_MOVE_TICKS - AVOID_STAY_TICKS) / AVOID_RETURN_TICKS);
        else                                                                   // 끝: 정확히 원래 자리
        {
            offset = 0.0f;
            m_dodge_start_tick[i] = -1;
        }
        placeKart(i, offset);
    }
}   // updateDodge

// ----------------------------------------------------------------------------
/** [auto-item-avoid] 카트를 원래 자리에서 옆 방향으로 offset(미터)만큼 옮깁니다.
 *  world.cpp 의 World::moveKartTo() 와 같은 방식으로 위치를 적용합니다.
 */
void FreeForAll::placeKart(int kart_id, float offset)
{
    AbstractKart* kart = getKart(kart_id);
    btTransform t = m_home_trans[kart_id];

    // 카트 기준 옆 방향(x축)으로 offset만큼 이동
    Vec3 side = t.getBasis().getColumn(0);
    t.setOrigin(t.getOrigin() + side * offset);

    kart->setXYZ(t.getOrigin());
    kart->setRotation(t.getRotation());
    kart->getBody()->setCenterOfMassTransform(t);
    kart->getVehicle()->updateAllWheelPositions();
}   // placeKart

// ----------------------------------------------------------------------------
/** The battle is over if only one kart is left, or no player kart.
 */
bool FreeForAll::isRaceOver()
{
    if (NetworkConfig::get()->isNetworking() &&
        NetworkConfig::get()->isClient())
        return false;

    if (!getKartAtPosition(1))
        return false;

    const int top_id = getKartAtPosition(1)->getWorldKartId();
    const int hit_capture_limit = RaceManager::get()->getHitCaptureLimit();

    return (m_count_down_reached_zero && RaceManager::get()->hasTimeTarget()) ||
        (hit_capture_limit != 0 && m_scores[top_id] >= hit_capture_limit);
}   // isRaceOver

// ----------------------------------------------------------------------------
/** Returns the data to display in the race gui.
 */
void FreeForAll::getKartsDisplayInfo(
    std::vector<RaceGUIBase::KartIconDisplayInfo>* info)
{
    const unsigned int kart_amount = getNumKarts();
    for (unsigned int i = 0; i < kart_amount; i++)
    {
        RaceGUIBase::KartIconDisplayInfo& rank_info = (*info)[i];
        rank_info.lap = -1;
        rank_info.m_outlined_font = true;
        rank_info.m_color = getColor(i);
        rank_info.m_text = getKart(i)->getController()->getName();
        if (RaceManager::get()->getKartGlobalPlayerId(i) > -1)
        {
            const core::stringw& flag = StringUtils::getCountryFlag(
                RaceManager::get()->getKartInfo(i).getCountryCode());
            if (!flag.empty())
            {
                rank_info.m_text += L" ";
                rank_info.m_text += flag;
            }
        }
        rank_info.m_text += core::stringw(L" (") +
            StringUtils::toWString(m_scores[i]) + L")";
    }
}   // getKartsDisplayInfo

//-----------------------------------------------------------------------------
std::pair<int, video::SColor> FreeForAll::getSpeedometerDigit(
    const AbstractKart* kart) const
{
    if (kart->isEliminated()) // m_scores[id] is INT_MIN
    {
        return std::make_pair(0, video::SColor(255, 128, 128, 128));
    }

    int id = kart->getWorldKartId();

    // Fade from green to red
    std::vector<int> sorted_scores;
    for (unsigned int i = 0; i < m_scores.size(); i++)
    {
        if (!getKart(i)->isEliminated())
        {
            sorted_scores.push_back(m_scores[i]);
        }
    }
    std::sort(sorted_scores.begin(), sorted_scores.end(), std::greater<int>());

    if (sorted_scores.size() == 1)
    {
        int s = sorted_scores[id];
        video::SColor color = video::SColor(255, s <= 0 ? 255 : 0, s >= 0 ? 255 : 0, 0);
        return std::make_pair(s, color);
    }

    int rank = std::lower_bound(
        sorted_scores.begin(), sorted_scores.end(),
        m_scores[id], std::greater<int>()) - sorted_scores.begin();

    float value = (float)rank / (sorted_scores.size() - 1);
    int r = std::min(int(value * 510), 255);
    int g = std::min(int((1.0 - value) * 510), 255);

    return std::make_pair(m_scores[id], video::SColor(255, r, g, 0));
}   // getSpeedometerDigit

void FreeForAll::terminateRace()
{
    TimingLogger::get()->saveCsv();
    ExperimentLogger::get()->saveCsv();
    const unsigned int kart_amount = getNumKarts();

    for (unsigned int i = 0; i < kart_amount; i++)
    {
        getKart(i)->finishedRace(0.0f, true/*from_server*/);
    }

    WorldWithRank::terminateRace();
}

// ----------------------------------------------------------------------------
video::SColor FreeForAll::getColor(unsigned int kart_id) const
{
    return GUIEngine::getSkin()->getColor("font::normal");
}   // getColor

// ----------------------------------------------------------------------------
bool FreeForAll::getKartFFAResult(int kart_id) const
{
    // the kart(s) which has the top score wins
    AbstractKart* k = getKartAtPosition(1);
    if (!k)
        return false;
    int top_score = getKartScore(k->getWorldKartId());
    return getKartScore(kart_id) == top_score;
}   // getKartFFAResult

// ----------------------------------------------------------------------------
void FreeForAll::saveCompleteState(BareNetworkString* bns, STKPeer* peer)
{
    for (unsigned i = 0; i < m_scores.size(); i++)
        bns->addUInt32(m_scores[i]);
}   // saveCompleteState

// ----------------------------------------------------------------------------
void FreeForAll::restoreCompleteState(const BareNetworkString& b)
{
    for (unsigned i = 0; i < m_scores.size(); i++)
        m_scores[i] = b.getUInt32();
}   // restoreCompleteState

// ----------------------------------------------------------------------------
std::pair<uint32_t, uint32_t> FreeForAll::getGameStartedProgress() const
{
    std::pair<uint32_t, uint32_t> progress(
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max());
    if (RaceManager::get()->hasTimeTarget())
    {
        progress.first = (uint32_t)m_time;
    }
    AbstractKart* k = getKartAtPosition(1);
    float score = -1.0f;
    if (k)
        score = (float)getKartScore(k->getWorldKartId());

    if (score >= 0.0f)
    {
        progress.second = (uint32_t)(score /
            (float)RaceManager::get()->getHitCaptureLimit() * 100.0f);
    }
    return progress;
}   // getGameStartedProgress