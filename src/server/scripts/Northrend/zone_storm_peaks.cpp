/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "CombatAI.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellHistory.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include "WorldSession.h"

/*######
## npc_brunnhildar_prisoner
######*/

enum BrunnhildarPrisoner
{
    SPELL_ICE_PRISON           = 54894,
    SPELL_ICE_LANCE            = 55046,
    SPELL_FREE_PRISONER        = 55048,
    SPELL_RIDE_DRAKE           = 55074,
    SPELL_SHARD_IMPACT         = 55047
};

struct npc_brunnhildar_prisoner : public ScriptedAI
{
    npc_brunnhildar_prisoner(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        freed = false;
    }

    bool freed;

    void Reset() override
    {
        Initialize();
        me->CastSpell(me, SPELL_ICE_PRISON, true);
    }

    void JustAppeared() override
    {
        Reset();
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!freed)
            return;

        if (!me->GetVehicle())
            me->DespawnOrUnsummon();
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Unit* unitCaster = caster->ToUnit();
        if (!unitCaster)
            return;

        if (spellInfo->Id != SPELL_ICE_LANCE)
            return;

        if (unitCaster->GetVehicleKit()->GetAvailableSeatCount() != 0)
        {
            me->CastSpell(me, SPELL_FREE_PRISONER, true);
            me->CastSpell(unitCaster, SPELL_RIDE_DRAKE, true);
            me->CastSpell(me, SPELL_SHARD_IMPACT, true);
            freed = true;
        }
    }
};

// 55048 - Free Brunnhildar Prisoner
class spell_storm_peaks_free_brunnhildar_prisoner : public SpellScript
{
    PrepareSpellScript(spell_storm_peaks_free_brunnhildar_prisoner);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_ICE_PRISON });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->RemoveAurasDueToSpell(SPELL_ICE_PRISON);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_storm_peaks_free_brunnhildar_prisoner::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## npc_freed_protodrake
######*/

enum FreedProtoDrake
{
    NPC_DRAKE                           = 29709,

    AREA_VALLEY_OF_ANCIENT_WINTERS      = 4437,

    TEXT_EMOTE                          = 0,

    SPELL_KILL_CREDIT_PRISONER          = 55144,
    SPELL_SUMMON_LIBERATED              = 55073,
    SPELL_KILL_CREDIT_DRAKE             = 55143,

    EVENT_CHECK_AREA                    = 1,
    EVENT_REACHED_HOME                  = 2,
};

struct npc_freed_protodrake : public VehicleAI
{
    npc_freed_protodrake(Creature* creature) : VehicleAI(creature) { }

    EventMap events;

    void Reset() override
    {
        events.ScheduleEvent(EVENT_CHECK_AREA, 5s);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != WAYPOINT_MOTION_TYPE)
            return;

        if (id == 15)
        // drake reached village
        events.ScheduleEvent(EVENT_REACHED_HOME, 2s);
    }

    void UpdateAI(uint32 diff) override
    {
        VehicleAI::UpdateAI(diff);
        events.Update(diff);

        switch (events.ExecuteEvent())
        {
            case EVENT_CHECK_AREA:
                if (me->GetAreaId() == AREA_VALLEY_OF_ANCIENT_WINTERS)
                {
                    if (Vehicle* vehicle = me->GetVehicleKit())
                        if (Unit* passenger = vehicle->GetPassenger(0))
                        {
                            Talk(TEXT_EMOTE, passenger);
                            me->GetMotionMaster()->MovePath(NPC_DRAKE, false);
                        }
                }
                else
                    events.ScheduleEvent(EVENT_CHECK_AREA, 5s);
                break;
            case EVENT_REACHED_HOME:
                if (Vehicle* vehicle = me->GetVehicleKit())
                    if (Unit* player = vehicle->GetPassenger(0))
                        if (player->GetTypeId() == TYPEID_PLAYER)
                        {
                            // for each prisoner on drake, give credit
                            for (uint8 i = 1; i < 4; ++i)
                                if (Unit* prisoner = me->GetVehicleKit()->GetPassenger(i))
                                {
                                    if (prisoner->GetTypeId() != TYPEID_UNIT)
                                        return;
                                    prisoner->CastSpell(player, SPELL_KILL_CREDIT_PRISONER, true);
                                    prisoner->CastSpell(prisoner, SPELL_SUMMON_LIBERATED, true);
                                    prisoner->ExitVehicle();
                                }
                            me->CastSpell(me, SPELL_KILL_CREDIT_DRAKE, true);
                            player->ExitVehicle();
                        }
                break;
        }
    }
};

struct npc_icefang : public EscortAI
{
    npc_icefang(Creature* creature) : EscortAI(creature) { }

    void AttackStart(Unit* /*who*/) override { }
    void JustEngagedWith(Unit* /*who*/) override { }
    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void PassengerBoarded(Unit* who, int8 /*seatId*/, bool apply) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
        {
            if (apply)
                Start(false, true, who->GetGUID());
        }
    }

    void JustDied(Unit* /*killer*/) override { }
    void OnCharmed(bool /*isNew*/) override { }

    void UpdateAI(uint32 diff) override
    {
        EscortAI::UpdateAI(diff);

        if (!UpdateVictim())
            return;
    }
};

enum NPCs
{
    NPC_HYLDSMEET_DRAKERIDER = 29694
};

class npc_hyldsmeet_protodrake : public CreatureAI
{
public:
    npc_hyldsmeet_protodrake(Creature* creature) : CreatureAI(creature), _accessoryRespawnTimer(0) { }

    void PassengerBoarded(Unit* who, int8 /*seat*/, bool apply) override
    {
        if (apply)
            return;

        if (who->GetEntry() == NPC_HYLDSMEET_DRAKERIDER)
            _accessoryRespawnTimer = 5 * MINUTE * IN_MILLISECONDS;
    }

    void UpdateAI(uint32 diff) override
    {
        //! We need to manually reinstall accessories because the vehicle itself is friendly to players,
        //! so EnterEvadeMode is never triggered. The accessory on the other hand is hostile and killable.
        Vehicle* _vehicleKit = me->GetVehicleKit();
        if (_accessoryRespawnTimer && _accessoryRespawnTimer <= diff && _vehicleKit)
        {
            _vehicleKit->InstallAllAccessories(true);
            _accessoryRespawnTimer = 0;
        }
        else
            _accessoryRespawnTimer -= diff;
    }

private:
    uint32 _accessoryRespawnTimer;
};

/*#####
# npc_brann_bronzebeard for Quest 13285 "Forging the Keystone"
#####*/

enum BrannBronzebeard
{
    NPC_BRANN_BRONZEBEARD = 31810,
    NPC_A_DISTANT_VOICE   = 31814,
    OBJECT_TOL_SIGNAL_1   = 193590,
    OBJECT_TOL_SIGNAL_2   = 193591,
    OBJECT_TOL_SIGNAL_3   = 193592,
    OBJECT_TOL_SIGNAL_4   = 193593,
    OBJECT_TOL_SIGNAL_5   = 193594,
    SPELL_RESURRECTION    = 58854,
    SAY_BRANN_1           = 0,
    SAY_BRANN_2           = 1,
    SAY_BRANN_3           = 2,
    SAY_VOICE_1           = 0,
    SAY_VOICE_2           = 1,
    SAY_VOICE_3           = 2,
    SAY_VOICE_4           = 3,
    SAY_VOICE_5           = 4,

    EVENT_SCRIPT_1        = 3,
    EVENT_SCRIPT_2        = 4,
    EVENT_SCRIPT_3        = 5,
    EVENT_SCRIPT_4        = 6,
    EVENT_SCRIPT_5        = 7,
    EVENT_SCRIPT_6        = 8,
    EVENT_SCRIPT_7        = 9,
    EVENT_SCRIPT_8        = 10,
    EVENT_SCRIPT_9        = 11,
    EVENT_SCRIPT_10       = 12,
    EVENT_SCRIPT_11       = 13,
    EVENT_SCRIPT_12       = 14,
    EVENT_SCRIPT_13       = 15
};

struct npc_brann_bronzebeard_keystone : public ScriptedAI
{
    npc_brann_bronzebeard_keystone(Creature* creature) : ScriptedAI(creature)
    {
        objectCounter = 0;
    }

    void Reset() override
    {
        for (ObjectGuid& guid : objectGUID)
            guid.Clear();

        playerGUID.Clear();
        voiceGUID.Clear();
        objectCounter = 0;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
    {
        CloseGossipMenuFor(player);
        playerGUID = player->GetGUID();
        events.ScheduleEvent(EVENT_SCRIPT_1, 100ms);
        return false;
    }

    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SCRIPT_1:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                        Talk(SAY_BRANN_1, player);
                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
                    if (Creature* voice = me->SummonCreature(NPC_A_DISTANT_VOICE, 7863.43f, -1396.585f, 1538.076f, 2.949606f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 49s))
                        voiceGUID = voice->GetGUID();
                    events.ScheduleEvent(EVENT_SCRIPT_2, 4s);
                    break;
                case EVENT_SCRIPT_2:
                    me->SetWalk(true);
                    me->GetMotionMaster()->MovePoint(0, 7861.488f, -1396.376f, 1534.059f, false);
                    events.ScheduleEvent(EVENT_SCRIPT_3, 6s);
                    break;
                case EVENT_SCRIPT_3:
                    me->SetEmoteState(EMOTE_STATE_WORK_MINING);
                    events.ScheduleEvent(EVENT_SCRIPT_4, 6s);
                    break;
                case EVENT_SCRIPT_4:
                    me->SetEmoteState(EMOTE_ONESHOT_NONE);
                    if (Creature* voice = ObjectAccessor::GetCreature(*me, voiceGUID))
                    {
                        voice->CastSpell(voice, SPELL_RESURRECTION);
                        if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                            voice->AI()->Talk(SAY_VOICE_1, player);
                    }
                    if (GameObject* go = me->SummonGameObject(OBJECT_TOL_SIGNAL_1, 7860.273f, -1383.622f, 1538.302f, -1.658062f, QuaternionData(0.f, 0.f, -0.737277f, 0.6755905f), 0s))
                        objectGUID[objectCounter++] = go->GetGUID();
                    events.ScheduleEvent(EVENT_SCRIPT_5, 6s);
                    break;
                case EVENT_SCRIPT_5:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                        if (Creature* voice = ObjectAccessor::GetCreature(*me, voiceGUID))
                            voice->AI()->Talk(SAY_VOICE_2, player);
                    if (GameObject* go = me->SummonGameObject(OBJECT_TOL_SIGNAL_2, 7875.67f, -1387.266f, 1538.323f, -2.373644f, QuaternionData(0.f, 0.f, -0.9271832f, 0.3746083f), 0s))
                        objectGUID[objectCounter++] = go->GetGUID();
                    events.ScheduleEvent(EVENT_SCRIPT_6, 6s);
                    break;
                case EVENT_SCRIPT_6:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                        if (Creature* voice = ObjectAccessor::GetCreature(*me, voiceGUID))
                            voice->AI()->Talk(SAY_VOICE_3, player);
                    if (GameObject* go = me->SummonGameObject(OBJECT_TOL_SIGNAL_3, 7879.212f, -1401.175f, 1538.279f, 2.967041f, QuaternionData(0.f, 0.f, 0.9961939f, 0.08716504f), 0s))
                        objectGUID[objectCounter++] = go->GetGUID();
                    events.ScheduleEvent(EVENT_SCRIPT_7, 6s);
                    break;
                case EVENT_SCRIPT_7:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                        if (Creature* voice = ObjectAccessor::GetCreature(*me, voiceGUID))
                            voice->AI()->Talk(SAY_VOICE_4, player);
                    if (GameObject* go = me->SummonGameObject(OBJECT_TOL_SIGNAL_4, 7868.944f, -1411.18f, 1538.213f, 2.111848f, QuaternionData(0.f, 0.f, 0.8703556f, 0.4924237f), 0s))
                        objectGUID[objectCounter++] = go->GetGUID();
                    events.ScheduleEvent(EVENT_SCRIPT_8, 6s);
                    break;
                case EVENT_SCRIPT_8:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                        if (Creature* voice = ObjectAccessor::GetCreature(*me, voiceGUID))
                            voice->AI()->Talk(SAY_VOICE_5, player);
                    if (GameObject* go = me->SummonGameObject(OBJECT_TOL_SIGNAL_5, 7855.11f, -1406.839f, 1538.42f, 1.151916f, QuaternionData(0.f, 0.f, 0.5446386f, 0.8386708f), 0s))
                        objectGUID[objectCounter] = go->GetGUID();
                    events.ScheduleEvent(EVENT_SCRIPT_9, 6s);
                    break;
                case EVENT_SCRIPT_9:
                    if (Creature* voice = ObjectAccessor::GetCreature(*me, voiceGUID))
                        voice->CastSpell(voice, SPELL_RESURRECTION);
                    events.ScheduleEvent(EVENT_SCRIPT_10, 6s);
                    break;
                case EVENT_SCRIPT_10:
                    if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                    {
                        Talk(SAY_BRANN_2, player);
                        player->KilledMonsterCredit(me->GetEntry());
                    }
                    events.ScheduleEvent(EVENT_SCRIPT_11, 6s);
                    break;
                case EVENT_SCRIPT_11:
                    me->SetFacingTo(2.932153f);
                    if (Player* player = ObjectAccessor::GetPlayer(*me, playerGUID))
                        Talk(SAY_BRANN_3, player);

                    for (uint8 i = 0; i < 5; ++i)
                        if (GameObject* go = ObjectAccessor::GetGameObject(*me, objectGUID[i]))
                            go->Delete();

                    events.ScheduleEvent(EVENT_SCRIPT_12, 6s);
                    break;
                case EVENT_SCRIPT_12:
                    me->GetMotionMaster()->Clear();
                    me->SetWalk(false);
                    me->GetMotionMaster()->MovePoint(0, 7799.908f, -1413.561f, 1534.829f, false);
                    events.ScheduleEvent(EVENT_SCRIPT_13, 10s);
                    break;
                case EVENT_SCRIPT_13:
                    me->DisappearAndDie();
                    break;
            }
        }
    }

private:
    EventMap events;
    ObjectGuid playerGUID;
    ObjectGuid objectGUID[5];
    ObjectGuid voiceGUID;
    uint8 objectCounter;
};

/*#####
# Quest 13003 Thrusting Hodir's Spear
#####*/

enum WildWyrm
{
    PATH_WILD_WYRM                      = 30275 * 10,

    // Phase 1
    SPELL_PLAYER_MOUNT_WYRM             = 56672,
    SPELL_FIGHT_WYRM                    = 56673,
    SPELL_SPEAR_OF_HODIR                = 56671,
    SPELL_GRIP                          = 56689,
    SPELL_GRAB_ON                       = 60533,
    SPELL_DODGE_CLAWS                   = 56704,
    SPELL_THRUST_SPEAR                  = 56690,
    SPELL_MIGHTY_SPEAR_THRUST           = 60586,
    SPELL_CLAW_SWIPE_PERIODIC           = 60689,
    SPELL_CLAW_SWIPE_DAMAGE             = 60776,
    SPELL_FULL_HEAL_MANA                = 32432,
    SPELL_LOW_HEALTH_TRIGGER            = 60596,

    // Phase 2
    SPELL_EJECT_PASSENGER_1             = 60603,
    SPELL_PRY_JAWS_OPEN                 = 56706,
    SPELL_FATAL_STRIKE                  = 60587,
    SPELL_FATAL_STRIKE_DAMAGE           = 60881,
    SPELL_JAWS_OF_DEATH_PERIODIC        = 56692,
    SPELL_FLY_STATE_VISUAL              = 60865,

    // Dead phase
    SPELL_WYRM_KILL_CREDIT              = 56703,
    SPELL_FALLING_DRAGON_FEIGN_DEATH    = 55795,
    SPELL_EJECT_ALL_PASSENGERS          = 50630,

    SAY_SWIPE                           = 0,
    SAY_DODGED                          = 1,
    SAY_PHASE_2                         = 2,
    SAY_GRIP_WARN                       = 3,
    SAY_STRIKE_MISS                     = 4,

    ACTION_CLAW_SWIPE_WARN              = 1,
    ACTION_CLAW_SWIPE_DODGE             = 2,
    ACTION_GRIP_FAILING                 = 3,
    ACTION_GRIP_LOST                    = 4,
    ACTION_FATAL_STRIKE_MISS            = 5,

    POINT_START_FIGHT                   = 1,
    POINT_FALL                          = 2,

    SEAT_INITIAL                        = 0,
    SEAT_MOUTH                          = 1,

    PHASE_INITIAL                       = 0,
    PHASE_MOUTH                         = 1,
    PHASE_DEAD                          = 2,
    PHASE_MAX                           = 3
};

uint8 const ControllableSpellsCount = 4;
uint32 const WyrmControlSpells[PHASE_MAX][ControllableSpellsCount] =
{
    { SPELL_GRAB_ON,       SPELL_DODGE_CLAWS, SPELL_THRUST_SPEAR, SPELL_MIGHTY_SPEAR_THRUST },
    { SPELL_PRY_JAWS_OPEN, 0,                 SPELL_FATAL_STRIKE, 0                         },
    { 0,                   0,                 0,                  0                         }
};

struct npc_wild_wyrm : public VehicleAI
{
    npc_wild_wyrm(Creature* creature) : VehicleAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _phase = PHASE_INITIAL;
        _playerCheckTimer = 1 * IN_MILLISECONDS;
    }

    void InitSpellsForPhase()
    {
        ASSERT(_phase < PHASE_MAX);
        for (uint8 i = 0; i < ControllableSpellsCount; ++i)
            me->m_spells[i] = WyrmControlSpells[_phase][i];
    }

    void Reset() override
    {
        Initialize();

        _playerGuid.Clear();
        _scheduler.CancelAll();

        InitSpellsForPhase();

        me->SetImmuneToPC(false);
    }

    void DoAction(int32 action) override
    {
        Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid);
        if (!player)
            return;

        switch (action)
        {
            case ACTION_CLAW_SWIPE_WARN:
                Talk(SAY_SWIPE, player);
                break;
            case ACTION_CLAW_SWIPE_DODGE:
                Talk(SAY_DODGED, player);
                break;
            case ACTION_GRIP_FAILING:
                Talk(SAY_GRIP_WARN, player);
                break;
            case ACTION_GRIP_LOST:
                DoCastAOE(SPELL_EJECT_PASSENGER_1, true);
                EnterEvadeMode();
                break;
            case ACTION_FATAL_STRIKE_MISS:
                Talk(SAY_STRIKE_MISS, player);
                break;
            default:
                break;
        }
    }

    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        if (_playerGuid || spellInfo->Id != SPELL_SPEAR_OF_HODIR)
            return;

        _playerGuid = caster->GetGUID();
        DoCastAOE(SPELL_FULL_HEAL_MANA, true);
        me->SetImmuneToPC(true);

        me->GetMotionMaster()->MovePoint(POINT_START_FIGHT, *caster);
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE && type != EFFECT_MOTION_TYPE)
            return;

        switch (id)
        {
            case POINT_START_FIGHT:
            {
                Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid);
                if (!player)
                    return;

                DoCast(player, SPELL_PLAYER_MOUNT_WYRM);
                me->GetMotionMaster()->Clear();
                break;
            }
            case POINT_FALL:
                DoCastAOE(SPELL_EJECT_ALL_PASSENGERS);
                me->KillSelf();
                break;
            default:
                break;
        }
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage >= me->GetHealth())
        {
            damage = me->GetHealth() - 1;

            if (_phase == PHASE_DEAD)
                return;

            _phase = PHASE_DEAD;
            _scheduler.CancelAll()
                .Async([this]
            {
                InitSpellsForPhase();

                if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                    player->VehicleSpellInitialize();

                DoCastAOE(SPELL_WYRM_KILL_CREDIT);
                DoCastAOE(SPELL_FALLING_DRAGON_FEIGN_DEATH);

                me->RemoveAurasDueToSpell(SPELL_JAWS_OF_DEATH_PERIODIC);
                me->RemoveAurasDueToSpell(SPELL_PRY_JAWS_OPEN);

                me->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);

                me->GetMotionMaster()->MoveFall(POINT_FALL);
            });
        }
    }

    void PassengerBoarded(Unit* passenger, int8 seatId, bool apply) override
    {
        if (!apply || passenger->GetGUID() != _playerGuid)
            return;

        if (seatId != SEAT_INITIAL)
            return;

        me->CastSpell(nullptr, SPELL_GRIP, CastSpellExtraArgs().AddSpellMod(SPELLVALUE_AURA_STACK, 50));
        DoCastAOE(SPELL_CLAW_SWIPE_PERIODIC);

        _scheduler.Async([this]
        {
            me->GetMotionMaster()->MovePath(PATH_WILD_WYRM, true);
        })
            .Schedule(Milliseconds(500), [this](TaskContext context)
        {
            if (_phase == PHASE_MOUTH)
                return;

            if (me->HealthBelowPct(25))
            {
                _phase = PHASE_MOUTH;
                context.Async([this]
                {
                    InitSpellsForPhase();
                    DoCastAOE(SPELL_LOW_HEALTH_TRIGGER, true);
                    me->RemoveAurasDueToSpell(SPELL_CLAW_SWIPE_PERIODIC);
                    me->RemoveAurasDueToSpell(SPELL_GRIP);

                    if (Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid))
                        Talk(SAY_PHASE_2, player);

                    DoCastAOE(SPELL_EJECT_PASSENGER_1, true);
                    DoCastAOE(SPELL_JAWS_OF_DEATH_PERIODIC);
                    DoCastAOE(SPELL_FLY_STATE_VISUAL);
                });
                return;
            }

            context.Repeat();
        });
    }

    bool EvadeCheck() const
    {
        Player* player = ObjectAccessor::GetPlayer(*me, _playerGuid);
        if (!player)
            return false;

        switch (_phase)
        {
            case PHASE_INITIAL:
            case PHASE_MOUTH:
                if (!player->IsAlive())
                    return false;
                break;
            case PHASE_DEAD:
                break;
            default:
                ABORT();
                break;
        }

        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!_playerGuid)
        {
            if (UpdateVictim())
                DoMeleeAttackIfReady();
            return;
        }

        if (_playerCheckTimer <= diff)
        {
            if (!EvadeCheck())
                EnterEvadeMode(EVADE_REASON_NO_HOSTILES);

            _playerCheckTimer = 1 * IN_MILLISECONDS;
        }
        else
            _playerCheckTimer -= diff;

        _scheduler.Update(diff);
    }

private:
    uint8 _phase;
    uint32 _playerCheckTimer;
    ObjectGuid _playerGuid;
    TaskScheduler _scheduler;
};

/*#####
# Quest 13010 Krolmir, Hammer of Storms
#####*/

enum JokkumScriptcast
{
    NPC_KINGJOKKUM                   = 30331,
    NPC_THORIM                       = 30390,
    PATH_JOKKUM                      = 2072200,
    PATH_JOKKUM_END                  = 2072201,
    SAY_HOLD_ON                      = 0,
    SAY_JOKKUM_1                     = 1,
    SAY_JOKKUM_2                     = 2,
    SAY_JOKKUM_3                     = 3,
    SAY_JOKKUM_4                     = 4,
    SAY_JOKKUM_5                     = 5,
    SAY_JOKKUM_6                     = 6,
    SAY_JOKKUM_7                     = 7,
    SAY_JOKKUM_8                     = 8,
    SAY_THORIM_1                     = 0,
    SAY_THORIM_2                     = 1,
    SAY_THORIM_3                     = 2,
    SAY_THORIM_4                     = 3,
    SPELL_JOKKUM_SUMMON              = 56541,
    SPELL_JOKKUM_KILL_CREDIT         = 56545,
    SPELL_PLAYER_CAST_VERANUS_SUMMON = 56650,
    SPELL_SUMMON_VERANUS_AND_THORIM  = 56649,
    EVENT_KROLMIR_1                  = 16,
    EVENT_KROLMIR_2                  = 17,
    EVENT_KROLMIR_3                  = 18,
    EVENT_KROLMIR_4                  = 19,
    EVENT_KROLMIR_5                  = 20,
    EVENT_KROLMIR_6                  = 21,
    EVENT_KROLMIR_7                  = 22,
    EVENT_KROLMIR_8                  = 23,
    EVENT_KROLMIR_9                  = 24,
};

// 61319 - Jokkum Scriptcast
class spell_jokkum_scriptcast : public AuraScript
{
    PrepareAuraScript(spell_jokkum_scriptcast);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_JOKKUM_SUMMON });
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* target = GetTarget())
            target->CastSpell(target, SPELL_JOKKUM_SUMMON, true);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_jokkum_scriptcast::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 56650 - Player Cast Veranus Summon
class spell_veranus_summon : public AuraScript
{
    PrepareAuraScript(spell_veranus_summon);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_VERANUS_AND_THORIM });
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* target = GetTarget())
            target->CastSpell(target, SPELL_SUMMON_VERANUS_AND_THORIM, true);
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_veranus_summon::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

enum CloseRift
{
    SPELL_DESPAWN_RIFT          = 61665
};

// 56763 - Close Rift
class spell_close_rift : public AuraScript
{
    PrepareAuraScript(spell_close_rift);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_DESPAWN_RIFT });
    }

    void HandlePeriodic(AuraEffect const* /* aurEff */)
    {
        if (++_counter == 5)
            GetTarget()->CastSpell(nullptr, SPELL_DESPAWN_RIFT, true);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_close_rift::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }

private:
    uint8 _counter = 0;
};

// 56689 - Grip
class spell_grip : public AuraScript
{
    PrepareAuraScript(spell_grip);

    void DummyTick(AuraEffect const* /*aurEff*/)
    {
        ++_tickNumber;

        // each 15 ticks stack reduction increases by 2 (increases by 1 at each 7th and 15th tick)
        // except for the first 15 ticks that remove 1 stack each
        uint32 const period = ((_tickNumber - 1) % 15) + 1;
        uint32 const sequence = (_tickNumber - 1) / 15;

        uint32 stacksToRemove;
        if (sequence == 0)
            stacksToRemove = 1;
        else
        {
            stacksToRemove = sequence * 2;
            if (period > 7)
                ++stacksToRemove;
        }

        // while we could do ModStackAmount(-stacksToRemove), this is how it's done in sniffs :)
        for (uint32 i = 0; i < stacksToRemove; ++i)
            ModStackAmount(-1, AURA_REMOVE_BY_EXPIRE);

        if (GetStackAmount() < 15 && !_warning)
        {
            _warning = true;
            GetTarget()->GetAI()->DoAction(ACTION_GRIP_FAILING);
        }
        else if (GetStackAmount() > 30)
            _warning = false;
    }

    void HandleDrop(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;

        GetTarget()->GetAI()->DoAction(ACTION_GRIP_LOST);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_grip::DummyTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);

        AfterEffectRemove += AuraEffectRemoveFn(spell_grip::HandleDrop, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }

    // tick number in the AuraEffect gets reset each time we stack the aura, so keep track of it locally
    uint32 _tickNumber = 0;

    bool _warning = false;
};

// 60533 - Grab On
class spell_grab_on : public SpellScript
{
   PrepareSpellScript(spell_grab_on);

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Aura* grip = GetCaster()->GetAura(SPELL_GRIP, GetCaster()->GetGUID()))
            grip->ModStackAmount(GetEffectValue(), AURA_REMOVE_BY_DEFAULT, false);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_grab_on::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 56690 - Thrust Spear
// 60586 - Mighty Spear Thrust
template <int8 StacksToLose>
class spell_loosen_grip : public SpellScript
{
   PrepareSpellScript(spell_loosen_grip);

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Aura* grip = GetCaster()->GetAura(SPELL_GRIP))
            grip->ModStackAmount(-StacksToLose, AURA_REMOVE_BY_EXPIRE);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_loosen_grip::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 60596 - Low Health Trigger
class spell_low_health_trigger : public SpellScript
{
    PrepareSpellScript(spell_low_health_trigger);

    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ static_cast<uint32>(spellInfo->GetEffect(EFFECT_0).CalcValue()) });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(nullptr, GetEffectValue(), true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_low_health_trigger::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 60776 - Claw Swipe
// 60864 - Jaws of Death
class spell_jaws_of_death_claw_swipe_pct_damage : public SpellScript
{
    PrepareSpellScript(spell_jaws_of_death_claw_swipe_pct_damage);

    void HandleDamage(SpellEffIndex /*effIndex*/)
    {
        SetEffectValue(static_cast<int32>(GetHitUnit()->CountPctFromMaxHealth(GetEffectValue())));
    }

    void Register() override
    {
        OnEffectLaunchTarget += SpellEffectFn(spell_jaws_of_death_claw_swipe_pct_damage::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

// 56705 - Claw Swipe
class spell_claw_swipe_check : public AuraScript
{
    PrepareAuraScript(spell_claw_swipe_check);

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->GetAI()->DoAction(ACTION_CLAW_SWIPE_WARN);
    }

    void OnRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        if (Vehicle* vehicle = GetTarget()->GetVehicleKit())
        {
            if (Unit* player = vehicle->GetPassenger(SEAT_INITIAL))
            {
                if (player->HasAura(SPELL_DODGE_CLAWS))
                {
                    GetTarget()->GetAI()->DoAction(ACTION_CLAW_SWIPE_DODGE);
                    return;
                }
            }
        }

        GetTarget()->CastSpell(nullptr, aurEff->GetAmount(), false);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_claw_swipe_check::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectApplyFn(spell_claw_swipe_check::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 60587 - Fatal Strike
class spell_fatal_strike : public SpellScript
{
    PrepareSpellScript(spell_fatal_strike);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FATAL_STRIKE_DAMAGE });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        int32 chance = 0;
        if (AuraEffect const* aurEff = GetCaster()->GetAuraEffect(SPELL_PRY_JAWS_OPEN, EFFECT_0))
            chance = aurEff->GetAmount();

        if (!roll_chance_i(chance))
        {
            GetCaster()->GetAI()->DoAction(ACTION_FATAL_STRIKE_MISS);
            return;
        }

        GetCaster()->CastSpell(nullptr, SPELL_FATAL_STRIKE_DAMAGE, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_fatal_strike::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 56672 - Player Mount Wyrm
class spell_player_mount_wyrm : public AuraScript
{
    PrepareAuraScript(spell_player_mount_wyrm);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_FIGHT_WYRM });
    }

    void HandleDummy(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->CastSpell(nullptr, SPELL_FIGHT_WYRM, true);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectApplyFn(spell_player_mount_wyrm::HandleDummy, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/*######
## Quest 12987: Mounting Hodir's Helm
######*/

enum MountingHodirsHelm
{
    TEXT_PRONOUNCEMENT_1     = 30906,
    TEXT_PRONOUNCEMENT_2     = 30907,
    NPC_HODIRS_HELM_KC       = 30210
};

// 56278 - Read Pronouncement
class spell_storm_peaks_read_pronouncement : public AuraScript
{
    PrepareAuraScript(spell_storm_peaks_read_pronouncement);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sObjectMgr->GetBroadcastText(TEXT_PRONOUNCEMENT_1) &&
            sObjectMgr->GetBroadcastText(TEXT_PRONOUNCEMENT_2) &&
            sObjectMgr->GetCreatureTemplate(NPC_HODIRS_HELM_KC);
    }

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
        {
            target->Unit::Whisper(TEXT_PRONOUNCEMENT_1, target, true);
            target->KilledMonsterCredit(NPC_HODIRS_HELM_KC);
            target->Unit::Whisper(TEXT_PRONOUNCEMENT_2, target, true);
        }
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_storm_peaks_read_pronouncement::OnApply, EFFECT_0, SPELL_AURA_NONE, AURA_EFFECT_HANDLE_REAL);
    }
};

/*######
## Quest 13011: Jormuttar is Soo Fat...
######*/

enum JormuttarIsSooFat
{
    SPELL_CREATE_BEAR_FLANK    = 56566,
    SPELL_BEAR_FLANK_FAIL      = 56569,
    TEXT_CARVE_FAIL            = 30986
};

// 56565 - Bear Flank Master
class spell_storm_peaks_bear_flank_master : public SpellScript
{
    PrepareSpellScript(spell_storm_peaks_bear_flank_master);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CREATE_BEAR_FLANK, SPELL_BEAR_FLANK_FAIL });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), roll_chance_i(20) ? SPELL_CREATE_BEAR_FLANK : SPELL_BEAR_FLANK_FAIL);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_storm_peaks_bear_flank_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 56569 - Bear Flank Fail
class spell_storm_peaks_bear_flank_fail : public AuraScript
{
    PrepareAuraScript(spell_storm_peaks_bear_flank_fail);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sObjectMgr->GetBroadcastText(TEXT_CARVE_FAIL);
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
            target->Unit::Whisper(TEXT_CARVE_FAIL, target, true);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_storm_peaks_bear_flank_fail::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/*######
## Quest 12828: Ample Inspiration
######*/

enum AmpleInspiration
{
    SPELL_QUIET_SUICIDE              = 3617,
    SPELL_SUMMON_MAIN_MAMMOTH_MEAT   = 57444,
    SPELL_MAMMOTH_SUMMON_OBJECT_1    = 54627,
    SPELL_MAMMOTH_SUMMON_OBJECT_2    = 54628,
    SPELL_MAMMOTH_SUMMON_OBJECT_3    = 54623,

    ITEM_EXPLOSIVE_DEVICE            = 40686
};

// 54581 - Mammoth Explosion Spell Spawner
class spell_storm_peaks_mammoth_explosion_master : public SpellScript
{
    PrepareSpellScript(spell_storm_peaks_mammoth_explosion_master);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_QUIET_SUICIDE,
            SPELL_SUMMON_MAIN_MAMMOTH_MEAT,
            SPELL_MAMMOTH_SUMMON_OBJECT_1,
            SPELL_MAMMOTH_SUMMON_OBJECT_2,
            SPELL_MAMMOTH_SUMMON_OBJECT_3
        });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_QUIET_SUICIDE);
        caster->CastSpell(caster, SPELL_SUMMON_MAIN_MAMMOTH_MEAT);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_1);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_1);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_1);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_2);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_2);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_2);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_3);
        caster->CastSpell(caster, SPELL_MAMMOTH_SUMMON_OBJECT_3);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_storm_peaks_mammoth_explosion_master::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 54892 - Unstable Explosive Detonation
class spell_storm_peaks_unstable_explosive_detonation : public SpellScript
{
    PrepareSpellScript(spell_storm_peaks_unstable_explosive_detonation);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return sObjectMgr->GetItemTemplate(ITEM_EXPLOSIVE_DEVICE);
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Player* caster = GetCaster()->ToPlayer())
            caster->DestroyItemCount(ITEM_EXPLOSIVE_DEVICE, 1, true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_storm_peaks_unstable_explosive_detonation::HandleScript, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 12915: Mending Fences
######*/

enum MendingFences
{
    SPELL_SUMMON_EARTHEN    = 55528
};

// 55512 - Call of Earth
class spell_storm_peaks_call_of_earth : public SpellScript
{
    PrepareSpellScript(spell_storm_peaks_call_of_earth);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_EARTHEN });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        uint8 count = urand(2, 6);
        for (uint8 i = 0; i < count; i++)
            GetCaster()->CastSpell(GetCaster(), SPELL_SUMMON_EARTHEN, true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_storm_peaks_call_of_earth::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 12851: Bearly Hanging On
######*/

enum BearlyHangingOn
{
    NPC_FROSTGIANT             = 29351,
    NPC_FROSTWORG              = 29358,
    SPELL_FROSTGIANT_CREDIT    = 58184,
    SPELL_FROSTWORG_CREDIT     = 58183,
    SPELL_IMMOLATION           = 54690,
    SPELL_ABLAZE               = 54683
};

// 54798 - FLAMING Arrow Triggered Effect
class spell_storm_peaks_flaming_arrow_triggered_effect : public AuraScript
{
    PrepareAuraScript(spell_storm_peaks_flaming_arrow_triggered_effect);

    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* caster = GetCaster())
        {
            Unit* target = GetTarget();
            // Already in fire
            if (target->HasAura(SPELL_ABLAZE))
                return;

            if (Player* player = caster->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                switch (target->GetEntry())
                {
                    case NPC_FROSTWORG:
                        target->CastSpell(player, SPELL_FROSTWORG_CREDIT, true);
                        target->CastSpell(target, SPELL_IMMOLATION, true);
                        target->CastSpell(target, SPELL_ABLAZE, true);
                        break;
                    case NPC_FROSTGIANT:
                        target->CastSpell(player, SPELL_FROSTGIANT_CREDIT, true);
                        target->CastSpell(target, SPELL_IMMOLATION, true);
                        target->CastSpell(target, SPELL_ABLAZE, true);
                        break;
                }
            }
        }
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_storm_peaks_flaming_arrow_triggered_effect::HandleEffectApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};

void AddSC_storm_peaks()
{
    RegisterCreatureAI(npc_brunnhildar_prisoner);
    RegisterSpellScript(spell_storm_peaks_free_brunnhildar_prisoner);
    RegisterCreatureAI(npc_freed_protodrake);
    RegisterCreatureAI(npc_icefang);
    RegisterCreatureAI(npc_hyldsmeet_protodrake);
    RegisterCreatureAI(npc_brann_bronzebeard_keystone);
    RegisterCreatureAI(npc_wild_wyrm);

    RegisterSpellScript(spell_jokkum_scriptcast);
    RegisterSpellScript(spell_veranus_summon);
    RegisterSpellScript(spell_close_rift);
    RegisterSpellScript(spell_grip);
    RegisterSpellScript(spell_grab_on);
    RegisterSpellScriptWithArgs(spell_loosen_grip<5>, "spell_thrust_spear");
    RegisterSpellScriptWithArgs(spell_loosen_grip<15>, "spell_mighty_spear_thrust");
    RegisterSpellScript(spell_low_health_trigger);
    RegisterSpellScript(spell_jaws_of_death_claw_swipe_pct_damage);
    RegisterSpellScript(spell_claw_swipe_check);
    RegisterSpellScript(spell_fatal_strike);
    RegisterSpellScript(spell_player_mount_wyrm);
    RegisterSpellScript(spell_storm_peaks_read_pronouncement);
    RegisterSpellScript(spell_storm_peaks_bear_flank_master);
    RegisterSpellScript(spell_storm_peaks_bear_flank_fail);
    RegisterSpellScript(spell_storm_peaks_mammoth_explosion_master);
    RegisterSpellScript(spell_storm_peaks_unstable_explosive_detonation);
    RegisterSpellScript(spell_storm_peaks_call_of_earth);
    RegisterSpellScript(spell_storm_peaks_flaming_arrow_triggered_effect);
}
