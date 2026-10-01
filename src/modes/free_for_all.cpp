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
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "network/network_config.hpp"
#include "network/network_string.hpp"
#include "network/protocols/game_events_protocol.hpp"
#include "network/stk_host.hpp"
#include "tracks/track.hpp"
#include "utils/string_utils.hpp"
#include "utils/experiment_logger.hpp"

#include "items/powerup_manager.hpp"
#include "items/attachment.hpp"
#include "karts/kart_properties.hpp"
#include "utils/vec3.hpp"

#include <limits>
#include <algorithm>
#include <utility>
#include <sstream>
#include <ctime>
#include <iostream>

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
}   // ~FreeForAll

static int s_ffa_round_id = 0; // 판을 구분하기 위한 전역 변수

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

    if (getNumKarts() >= 2)
    {
        // 첫 번째 카트 (인덱스 0)
        getKart(0)->setXYZ(Vec3(0.0f, 0.5f, 0.0f));
        getKart(0)->setRotation(btQuaternion(btVector3(0.0f, 1.0f, 0.0f), 0.0f));

        // 두 번째 카트 (인덱스 1)
        getKart(1)->setXYZ(Vec3(0.0f, 0.5f, 30.0f));
        getKart(1)->setRotation(btQuaternion(btVector3(0.0f, 1.0f, 0.0f), 3.14159265f));
    }
}   // init

// ----------------------------------------------------------------------------
/** Called when a battle is restarted.
 */
void FreeForAll::reset(bool restart)
{
    WorldWithRank::reset(restart);
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
    int new_score = 0;
    int score_delta = 0;
    if (kart_id == hitter || hitter == -1)
    {
        new_score = --m_scores[kart_id];
        score_delta = -1;
    }
    else
    {
        new_score = ++m_scores[hitter];
        score_delta = 1;
    }

    // 싱글플레이/멀티플레이 상관없이 점수가 변동될 때마다 로그를 기록합니다.
    long long timestamp = World::getWorld()->getTicksSinceStart();
#if defined(__ANDROID__)
    std::string platform = "Android";
    std::string targetPlatform = "Android"; // opponent platform can be derived similarly
#elif defined(_WIN32) || defined(_WIN64)
    std::string platform = "Windows";
    std::string targetPlatform = "Windows"; // opponent platform can be derived similarly
#else
    std::string platform = "Unknown";
    std::string targetPlatform = "Unknown";
#endif
// logEvent moved inside server‑only block

    if (NetworkConfig::get()->isNetworking() &&
        NetworkConfig::get()->isServer())
    {
        // 서버(호스트)일 때만 로그를 남깁니다.
        ExperimentLogger::get()->logEvent(s_ffa_round_id, timestamp, platform, targetPlatform, score_delta);
        NetworkString p(PROTOCOL_GAME_EVENTS);
        p.setSynchronous(true);
        p.addUInt8(GameEventsProtocol::GE_BATTLE_KART_SCORE);
        if (kart_id == hitter || hitter == -1)
            p.addUInt8((uint8_t)kart_id).addUInt16((int16_t)new_score);
        else
            p.addUInt8((uint8_t)hitter).addUInt16((int16_t)new_score);
        STKHost::get()->sendPacketToAllPeers(&p, true);
    }
}   // handleScoreInServer

// ----------------------------------------------------------------------------
void FreeForAll::setKartScoreFromServer(NetworkString& ns)
{
    int kart_id = ns.getUInt8();
    int16_t score = ns.getUInt16();
    m_scores.at(kart_id) = score;
}   // setKartScoreFromServer

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
 // [2] 듀얼 모드: 무한 반복 장전 및 동시 발사 (물리 고정 해제)
    if (getNumKarts() >= 2)
    {
        for (int i = 0; i < 2; i++)
        {
            getKart(i)->getControls().setSteer(0.0f);     // 조향(회전) 차단
            getKart(i)->getControls().setAccel(false);    // 가속 차단
            getKart(i)->getControls().setBrake(false);    // 브레이크 차단
            getKart(i)->getControls().setLookBack(false); // 뒤보기 차단
        }

        if (isActiveRacePhase())
        {
            int old_ticks = m_duel_ticks;
            m_duel_ticks += ticks;

            // [무한 장전] 8초(960틱) 시점에 양쪽에 볼링공 대신 컵케이크(미사일) 강제 장착
            if (old_ticks < 960 && m_duel_ticks >= 960)
            {
                for (int i = 0; i < 2; i++)
                {
                    getKart(i)->setPowerup(PowerupManager::POWERUP_CAKE, 1);
                }
            }

            // [확실한 발사] 10초 ~ 10.5초(1200~1230틱) 동안 발사 신호 지속 주입
            if (m_duel_ticks >= 1200 && m_duel_ticks <= 1230)
            {
                for (int i = 0; i < 2; i++)
                {
                    getKart(i)->getControls().setFire(true);
                    if (getKart(i)->getController())
                    {
                        getKart(i)->getController()->action(PlayerAction::PA_FIRE, 1);
                    }
                }
            }

            // 10.5초 이후 발사 버튼 완전 해제
            if (old_ticks <= 1230 && m_duel_ticks > 1230)
            {
                for (int i = 0; i < 2; i++)
                {
                    getKart(i)->getControls().setFire(false);
                    if (getKart(i)->getController())
                    {
                        getKart(i)->getController()->action(PlayerAction::PA_FIRE, 0);
                    }
                }
            }

            // [타이머 리셋] 15초(1800틱)가 되면 0으로 되돌아가 무한 반복
            if (m_duel_ticks >= 1800) m_duel_ticks = 0;
        }
    }
    // ====================================================================

    if (isRaceOver())
    {
        static bool already_saved = false;
        if (!already_saved)
        {
            already_saved = true;
            static int round_id = 0;
            round_id++;

            const char* platform = "Windows";

            std::ostringstream out;
            for (unsigned int i = 0; i < getNumKarts(); i++)
            {
                out << round_id << "," << platform << ","
                    << getKart(i)->getIdent() << ","
                    << getKartScore(i) << "\n";
            }
        }
    }
}   // update

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