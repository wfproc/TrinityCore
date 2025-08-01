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
#include "Containers.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ruby_sanctum.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

enum SavianaTexts
{
    SAY_AGGRO                   = 0, // You will sssuffer for this intrusion! (17528)
    SAY_CONFLAGRATION           = 1, // Burn in the master's flame! (17532)
    EMOTE_ENRAGED               = 2, // %s becomes enraged!
    SAY_KILL                    = 3, // Halion will be pleased. (17530) - As it should be.... (17529)
};

enum SavianaSpells
{
    SPELL_CONFLAGRATION         = 74452,
    SPELL_FLAME_BEACON          = 74453,
    SPELL_CONFLAGRATION_2       = 74454, // Unknown dummy effect
    SPELL_ENRAGE                = 78722,
    SPELL_FLAME_BREATH          = 74403,
};

enum SavianaEvents
{
    EVENT_ENRAGE                = 1,
    EVENT_FLIGHT                = 2,
    EVENT_FLAME_BREATH          = 3,
    EVENT_CONFLAGRATION         = 4,
    EVENT_LAND_GROUND           = 5,
    EVENT_AIR_MOVEMENT          = 6,

    // Event group
    EVENT_GROUP_LAND_PHASE      = 1,
};

enum SavianaPoints
{
    POINT_FLIGHT                = 1,
    POINT_LAND                  = 2,
    POINT_TAKEOFF               = 3,
    POINT_LAND_GROUND           = 4
};

enum SavianaMisc
{
    SOUND_ID_DEATH              = 17531
};

Position const SavianaRagefireFlyOutPos  = {3155.51f, 683.844f, 95.0f,   4.69f};
Position const SavianaRagefireFlyInPos   = {3151.07f, 636.443f, 79.540f, 4.69f};
Position const SavianaRagefireLandPos    = {3151.07f, 636.443f, 78.649f, 4.69f};

// 39747 - Saviana Ragefire
struct boss_saviana_ragefire : public BossAI
{
    boss_saviana_ragefire(Creature* creature) : BossAI(creature, DATA_SAVIANA_RAGEFIRE) { }

    void Reset() override
    {
        _Reset();
        me->SetReactState(REACT_AGGRESSIVE);
        me->SetCanFly(false);
        me->SetDisableGravity(false);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        events.Reset();
        events.ScheduleEvent(EVENT_ENRAGE, 20s, EVENT_GROUP_LAND_PHASE);
        events.ScheduleEvent(EVENT_FLAME_BREATH, 14s, EVENT_GROUP_LAND_PHASE);
        events.ScheduleEvent(EVENT_FLIGHT, 60s, EVENT_GROUP_LAND_PHASE);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        DoPlaySoundToSet(me, SOUND_ID_DEATH);
    }

    void MovementInform(uint32 type, uint32 point) override
    {
        if (type != POINT_MOTION_TYPE && type != EFFECT_MOTION_TYPE)
            return;

        switch (point)
        {
            case POINT_FLIGHT:
                events.ScheduleEvent(EVENT_CONFLAGRATION, 1s);
                Talk(SAY_CONFLAGRATION);
                break;
            case POINT_LAND:
                events.ScheduleEvent(EVENT_LAND_GROUND, 1ms);
                break;
            case POINT_LAND_GROUND:
                me->SetCanFly(false);
                me->SetDisableGravity(false);
                me->SetReactState(REACT_AGGRESSIVE);
                events.ScheduleEvent(EVENT_ENRAGE, 1s, EVENT_GROUP_LAND_PHASE);
                events.ScheduleEvent(EVENT_FLAME_BREATH, 2s, 4s, EVENT_GROUP_LAND_PHASE);
                events.ScheduleEvent(EVENT_FLIGHT, 50s, EVENT_GROUP_LAND_PHASE);
                break;
            case POINT_TAKEOFF:
                events.ScheduleEvent(EVENT_AIR_MOVEMENT, 1ms);
                break;
            default:
                break;
        }
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        _DespawnAtEvade();
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FLIGHT:
                {
                    me->SetCanFly(true);
                    me->SetDisableGravity(true);
                    me->SetReactState(REACT_PASSIVE);
                    me->AttackStop();
                    Position pos;
                    pos.Relocate(me);
                    pos.m_positionZ += 10.0f;
                    me->GetMotionMaster()->MoveTakeoff(POINT_TAKEOFF, pos);
                    events.CancelEventGroup(EVENT_GROUP_LAND_PHASE);
                    break;
                }
                case EVENT_CONFLAGRATION:
                    DoCastSelf(SPELL_CONFLAGRATION, true);
                    break;
                case EVENT_ENRAGE:
                    DoCastSelf(SPELL_ENRAGE);
                    Talk(EMOTE_ENRAGED);
                    events.Repeat(24s);
                    break;
                case EVENT_FLAME_BREATH:
                    DoCastVictim(SPELL_FLAME_BREATH);
                    events.Repeat(20s, 30s);
                    break;
                case EVENT_AIR_MOVEMENT:
                    me->GetMotionMaster()->MovePoint(POINT_FLIGHT, SavianaRagefireFlyOutPos);
                    break;
                case EVENT_LAND_GROUND:
                    me->GetMotionMaster()->MoveLand(POINT_LAND_GROUND, SavianaRagefireLandPos);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }
};

class ConflagrationTargetSelector
{
    public:
        ConflagrationTargetSelector() { }

        bool operator()(WorldObject* unit) const
        {
            return unit->GetTypeId() != TYPEID_PLAYER;
        }
};

// 74452 - Conflagration
class spell_saviana_conflagration_init : public SpellScript
{
    PrepareSpellScript(spell_saviana_conflagration_init);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_FLAME_BEACON, SPELL_CONFLAGRATION_2 });
    }

    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if(ConflagrationTargetSelector());
        uint8 maxSize = uint8(GetCaster()->GetMap()->GetSpawnMode() & 1 ? 6 : 3);
        if (targets.size() > maxSize)
            Trinity::Containers::RandomResize(targets, maxSize);
    }

    void HandleDummy(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        GetCaster()->CastSpell(GetHitUnit(), SPELL_FLAME_BEACON, true);
        GetCaster()->CastSpell(GetHitUnit(), SPELL_CONFLAGRATION_2, false);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_saviana_conflagration_init::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnEffectHitTarget += SpellEffectFn(spell_saviana_conflagration_init::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 74455 - Conflagration
class spell_saviana_conflagration_throwback : public SpellScript
{
    PrepareSpellScript(spell_saviana_conflagration_throwback);

    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        GetHitUnit()->CastSpell(GetCaster(), uint32(GetEffectValue()), true);
        GetHitUnit()->GetMotionMaster()->MovePoint(POINT_LAND, SavianaRagefireFlyInPos);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_saviana_conflagration_throwback::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

void AddSC_boss_saviana_ragefire()
{
    RegisterRubySanctumCreatureAI(boss_saviana_ragefire);
    RegisterSpellScript(spell_saviana_conflagration_init);
    RegisterSpellScript(spell_saviana_conflagration_throwback);
}
