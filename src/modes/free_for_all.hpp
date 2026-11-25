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

#ifndef FREE_FOR_ALL_HPP
#define FREE_FOR_ALL_HPP

#include "modes/world_with_rank.hpp"
#include "states_screens/race_gui_base.hpp"

#include <map>
#include <vector>

class NetworkString;

class FreeForAll : public WorldWithRank
{
protected:
    bool m_count_down_reached_zero;

    std::vector<int> m_scores;

    int m_duel_ticks;
    bool m_duel_fired;

    // [auto-item-avoid] 회피용 상태
    btTransform m_home_trans[2];     // 카트 0, 1의 원래 자리
    int  m_dodge_start_tick[2];      // 진행 중인 회피의 시작 서버 틱 (-1 = 회피 중 아님)
    std::map<int, int> m_dodge_tick_by_round[2];   // 카트별: 라운드 번호 → 회피 시작 서버 틱 (기록용)
    bool m_prev_steer[2];            // 직전 틱 버튼 상태 (누른 "순간" 감지용)

    void updateDodge();                          // 서버: 회피 감지 + 이동
    void placeKart(int kart_id, float offset);   // 원래 자리에서 옆으로 offset만큼 옮기기

    // ------------------------------------------------------------------------
    void handleScoreInServer(int kart_id, int hitter);

private:
    // ------------------------------------------------------------------------
    virtual video::SColor getColor(unsigned int kart_id) const;

public:
    // ------------------------------------------------------------------------
    FreeForAll();
    // ------------------------------------------------------------------------
    virtual ~FreeForAll();
    // ------------------------------------------------------------------------
    virtual void init() OVERRIDE;
    // ------------------------------------------------------------------------
    virtual bool isRaceOver() OVERRIDE;
    // ------------------------------------------------------------------------
    virtual void reset(bool restart=false) OVERRIDE;
    // ------------------------------------------------------------------------
    virtual void getKartsDisplayInfo(
                 std::vector<RaceGUIBase::KartIconDisplayInfo> *info) OVERRIDE;
    // ------------------------------------------------------------------------
    virtual bool raceHasLaps() OVERRIDE                       { return false; }
    // ------------------------------------------------------------------------
    virtual bool shouldDrawSpeedometerDigit() const OVERRIDE   { return true; }
    // ------------------------------------------------------------------------
    virtual std::pair<int, video::SColor> 
                getSpeedometerDigit(const AbstractKart *kart) const OVERRIDE; 
    // ------------------------------------------------------------------------
    virtual const std::string& getIdent() const OVERRIDE;
    // ------------------------------------------------------------------------
    virtual bool kartHit(int kart_id, int hitter = -1) OVERRIDE;
    // ------------------------------------------------------------------------
    virtual void update(int ticks) OVERRIDE;
    // ------------------------------------------------------------------------
    virtual void countdownReachedZero() OVERRIDE;
    // ------------------------------------------------------------------------
    virtual void terminateRace() OVERRIDE;
    // ------------------------------------------------------------------------
    void setKartScoreFromServer(NetworkString& ns);
    // ------------------------------------------------------------------------
    int getKartScore(int kart_id) const        { return m_scores.at(kart_id); }
    // ------------------------------------------------------------------------
    bool getKartFFAResult(int kart_id) const;
    // ------------------------------------------------------------------------
    void setKartTimingEvent (int ticks);
    // ------------------------------------------------------------------------
    // [auto-item-avoid] 해당 라운드의 회피 시작 서버 틱 (회피가 없었으면 -1)
    int getDodgeTickInRound(int kart_id, int round_id) const
    {
        auto it = m_dodge_tick_by_round[kart_id].find(round_id);
        return (it == m_dodge_tick_by_round[kart_id].end()) ? -1 : it->second;
    }
    // ------------------------------------------------------------------------
    virtual std::pair<uint32_t, uint32_t> getGameStartedProgress() const
        OVERRIDE;
    // ------------------------------------------------------------------------
    virtual void addReservedKart(int kart_id) OVERRIDE
    {
        WorldWithRank::addReservedKart(kart_id);
        m_scores.at(kart_id) = 0;
    }
    // ------------------------------------------------------------------------
    virtual void saveCompleteState(BareNetworkString* bns,
                                   STKPeer* peer) OVERRIDE;
    // ------------------------------------------------------------------------
    virtual void restoreCompleteState(const BareNetworkString& b) OVERRIDE;
};   // FreeForAll


#endif
