//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2007-2015 Joerg Henrichs
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

#include "config/user_config.hpp"
#include "items/bowling.hpp"
#include "network/network_config.hpp"
#include "audio/sfx_base.hpp"
#include "audio/sfx_manager.hpp"
#include "graphics/hit_sfx.hpp"
#include "graphics/material.hpp"
#include "io/xml_node.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/linear_world.hpp"
#include "modes/free_for_all.hpp"
#include "network/network_string.hpp"

#include "utils/log.hpp" //TODO: remove after debugging is done
#include "utils/timing_logger.hpp"
#include "utils/profiler.hpp"

float Bowling::m_st_max_distance;   // maximum distance for a bowling ball to be attracted
float Bowling::m_st_max_distance_squared;
float Bowling::m_st_force_to_target;

// -----------------------------------------------------------------------------
Bowling::Bowling(AbstractKart *kart)
        : Flyable(kart, PowerupManager::POWERUP_BOWLING, 50.0f /* mass */)
{
    m_has_hit_kart = false;
    m_roll_sfx = SFXManager::get()->createSoundSource("bowling_roll");
    fixSFXSplitscreen(m_roll_sfx);
    m_roll_sfx->play();
    m_roll_sfx->setLoop(true);
}   // Bowling

// ----------------------------------------------------------------------------
/** Destructor, removes any playing sfx.
 */
Bowling::~Bowling()
{
    // This will stop the sfx and delete the object.
    removeRollSfx();
}   // ~RubberBall

// -----------------------------------------------------------------------------
/** Initialises this object with data from the power.xml file.
 *  \param node XML Node
 *  \param bowling The bowling ball mesh
 */
void Bowling::init(const XMLNode &node, scene::IMesh *bowling)
{
    Flyable::init(node, bowling, PowerupManager::POWERUP_BOWLING);
    m_st_max_distance         = 20.0f;
    m_st_max_distance_squared = 20.0f * 20.0f;
    m_st_force_to_target      = 10.0f;

    node.get("max-distance",    &m_st_max_distance   );
    m_st_max_distance_squared = m_st_max_distance*m_st_max_distance;

    node.get("force-to-target", &m_st_force_to_target);
}   // init

// ----------------------------------------------------------------------------
/** Updates the bowling ball ineach frame. If this function returns true, the
 *  object will be removed by the projectile manager.
 *  \param dt Time step size.
 *  \returns True of this object should be removed.
 */
bool Bowling::updateAndDelete(int ticks)
{
    bool can_be_deleted = Flyable::updateAndDelete(ticks);
    if (can_be_deleted)
    {
        removeRollSfx();
        return true;
    }

    const AbstractKart *kart=0;
    Vec3        direction;
    float       minDistance;
    getClosestKart(&kart, &minDistance, &direction);
    if(kart && minDistance<m_st_max_distance_squared)   // move bowling towards kart
    {
        // limit angle, so that the bowling ball does not turn
        // around to hit a kart behind
        if(fabs(m_body->getLinearVelocity().angle(direction)) < 1.3)
        {
            direction*=1/direction.length()*m_st_force_to_target;
            m_body->applyCentralForce(direction);
        }
    }
    
   
    // Bowling balls lose energy (e.g. when hitting the track), so increase
    // the speed if the ball is too slow, but only if it's not too high (if
    // the ball is too high, it is 'pushed down', which can reduce the
    // speed, which causes the speed to increase, which in turn causes
    // the ball to fly higher and higher.
    //btTransform trans = getTrans();
    float hat = (getXYZ() - getHitPoint()).length();
    if(hat-0.5f*m_extend.getY()<0.01f)
    {
        const Material *material = getMaterial();
        if(!material || material->isDriveReset())
        {
            hit(NULL);
            removeRollSfx();
            return true;
        }
    }
    btVector3 v       = m_body->getLinearVelocity();
    float vlen        = v.length2();
    if (hat<= m_max_height)
    {
        if(vlen<0.8*m_speed*m_speed)
        {   // bowling lost energy (less than 80%), i.e. it's too slow - speed it up:
            if(vlen==0.0f) {
                v = btVector3(.5f, .0, 0.5f);  // avoid 0 div.
            }
 //           m_body->setLinearVelocity(v*(m_speed/sqrt(vlen)));
        }   // vlen < 0.8*m_speed*m_speed
    }   // hat< m_max_height

    if(vlen<0.1)
    {
        hit(NULL);
        removeRollSfx();
        return true;
    }

    if (m_roll_sfx && m_roll_sfx->getStatus()==SFXBase::SFX_PLAYING)
        m_roll_sfx->setPosition(getXYZ());

    return false;
}   // updateAndDelete

// -----------------------------------------------------------------------------
/** Callback from the physics in case that a kart or physical object is hit.
 *  The bowling ball triggers an explosion when hit.
 *  \param kart The kart hit (NULL if no kart was hit).
 *  \param object The object that was hit (NULL if none).
 *  \returns True if there was actually a hit (i.e. not owner, and target is
 *           not immune), false otherwise.
 */
bool Bowling::hit(AbstractKart* kart, PhysicalObject* obj)
{
    if (!Flyable::hit(kart, obj))
        return false;

    bool log_hit = kart && kart->isShielded();
    auto timing_type = TimingLogger::TimingType::Hit_Gum;
    if (log_hit)
    {
        kart->decreaseShieldTime();
    }
    else
    {
        auto* ffa = dynamic_cast<FreeForAll*>(World::getWorld());
        const int owner_id = m_owner->getWorldKartId();
        const int old_score = ffa ? ffa->getKartScore(owner_id) : 0;
        m_has_hit_kart = kart != NULL;
        explode(kart, obj, /*hit_secondary*/false);
        // Record only an actual score increase, using this ball's cycle.
        log_hit = ffa && ffa->getKartScore(owner_id) > old_score;
        timing_type = TimingLogger::TimingType::Hit_Bowling;
    }

    if (log_hit && m_timing_cycle_id != 0 &&
        UserConfigParams::m_auto_item_fire && UserConfigParams::m_timing_log &&
        (!NetworkConfig::get()->isNetworking() || NetworkConfig::get()->isServer()))
    {
        const char* platform = "Unknown";
#if defined(__ANDROID__)
        platform = "Android";
#elif defined(_WIN32) || defined(_WIN64)
        platform = "Windows";
#elif defined(__linux__)
        platform = "Linux";
#endif
        const char* topology = NetworkConfig::get()->isNetworking() ? "SERVER" : "CLIENT";
        TimingLogger::get()->logEvent(getTimeMilliseconds(),
            World::getWorld()->getTicksSinceStart(), -1, timing_type,
            topology, platform, m_timing_cycle_id);
    }
    return true;
}   // hit

// -----------------------------------------------------------------------------
BareNetworkString* Bowling::saveState(std::vector<std::string>* ru)
{
    BareNetworkString* buffer = Flyable::saveState(ru);
    if (buffer && m_timing_cycle_id)
        buffer->addUInt32(m_timing_cycle_id);
    return buffer;
}

// -----------------------------------------------------------------------------
void Bowling::restoreState(BareNetworkString* buffer, int count)
{
    const int start = buffer->getCurrentOffset();
    Flyable::restoreState(buffer, count);
    m_timing_cycle_id = buffer->getCurrentOffset() - start + 4 <= count
        ? buffer->getUInt32() : 0;
}

// ----------------------------------------------------------------------------
void Bowling::removeRollSfx()
{
    if (m_roll_sfx)
    {
        m_roll_sfx->deleteSFX();
        m_roll_sfx = NULL;
    }
}   // removeRollSfx

// ----------------------------------------------------------------------------
/** Returns the hit effect object to use when this objects hits something.
 *  \returns The hit effect object, or NULL if no hit effect should be played.
 */
HitEffect* Bowling::getHitEffect() const
{
    if (GUIEngine::isNoGraphics())
        return NULL;
    if (m_deleted_once)
        return NULL;
    if(m_has_hit_kart)
        return new HitSFX(getXYZ(), "strike");
    else
        return new HitSFX(getXYZ(), "crash");
}   // getHitEffect

// ----------------------------------------------------------------------------
void Bowling::onFireFlyable()
{
    Flyable::onFireFlyable();
    m_timing_cycle_id = m_owner->getControls().getTimingCycleId();

    m_has_hit_kart = false;
    float y_offset = 0.5f*m_owner->getKartLength() + m_extend.getZ()*0.5f;

    // if the kart is looking backwards, release from the back
    if( m_owner->getControls().getLookBack())
    {
        y_offset   = -y_offset;
        m_speed    = -m_speed*2;
    }
    else
    {
        float min_speed = m_speed*4.0f;
        /* make it go faster when throwing forward
           so the player doesn't catch up with the ball
           and explode by touching it */
        m_speed = m_owner->getSpeed() + m_speed;
        if(m_speed < min_speed) m_speed = min_speed;
    }

    const Vec3& normal = m_owner->getNormal();
    createPhysics(y_offset, btVector3(0.0f, 0.0f, m_speed*2),
                  new btSphereShape(0.5f*m_extend.getY()),
                  0.4f /*restitution*/,
                  -70.0f*normal /*gravity*/,
                  true /*rotates*/);
    // Even if the ball is fired backwards, m_speed must be positive,
    // otherwise the ball can start to vibrate when energy is added.
    m_speed = fabsf(m_speed);
    // Do not adjust the up velociy depending on height above terrain, since
    // this would disable gravity.
    setAdjustUpVelocity(false);

    // should not live forever, auto-destruct after 20 seconds
    m_max_lifespan = stk_config->time2Ticks(20);
}   // onFireFlyable
