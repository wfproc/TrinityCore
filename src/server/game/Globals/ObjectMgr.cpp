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

#include "ObjectMgr.h"
#include "AchievementMgr.h"
#include "ArenaTeamMgr.h"
#include "Bag.h"
#include "Chat.h"
#include "Containers.h"
#include "CreatureAIFactory.h"
#include "DatabaseEnv.h"
#include "DisableMgr.h"
#include "GameObject.h"
#include "GameObjectAIFactory.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "GroupMgr.h"
#include "GuildMgr.h"
#include "InstanceSaveMgr.h"
#include "InstanceScript.h"
#include "Language.h"
#include "LFGMgr.h"
#include "Log.h"
#include "LootMgr.h"
#include "Mail.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PoolMgr.h"
#include "QueryPackets.h"
#include "Random.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "StringConvert.h"
#include "TemporarySummon.h"
#include "ThreadPool.h"
#include "UpdateMask.h"
#include "Util.h"
#include "Vehicle.h"
#include "World.h"

ScriptMapMap sSpellScripts;
ScriptMapMap sEventScripts;
ScriptMapMap sWaypointScripts;

std::string GetScriptsTableNameByType(ScriptsType type)
{
    std::string res = "";
    switch (type)
    {
        case SCRIPTS_SPELL:         res = "spell_scripts";      break;
        case SCRIPTS_EVENT:         res = "event_scripts";      break;
        case SCRIPTS_WAYPOINT:      res = "waypoint_scripts";   break;
        default: break;
    }
    return res;
}

ScriptMapMap* GetScriptsMapByType(ScriptsType type)
{
    ScriptMapMap* res = nullptr;
    switch (type)
    {
        case SCRIPTS_SPELL:         res = &sSpellScripts;       break;
        case SCRIPTS_EVENT:         res = &sEventScripts;       break;
        case SCRIPTS_WAYPOINT:      res = &sWaypointScripts;    break;
        default: break;
    }
    return res;
}

std::string GetScriptCommandName(ScriptCommands command)
{
    std::string res = "";
    switch (command)
    {
        case SCRIPT_COMMAND_TALK: res = "SCRIPT_COMMAND_TALK"; break;
        case SCRIPT_COMMAND_EMOTE: res = "SCRIPT_COMMAND_EMOTE"; break;
        case SCRIPT_COMMAND_FIELD_SET: res = "SCRIPT_COMMAND_FIELD_SET"; break;
        case SCRIPT_COMMAND_MOVE_TO: res = "SCRIPT_COMMAND_MOVE_TO"; break;
        case SCRIPT_COMMAND_FLAG_SET: res = "SCRIPT_COMMAND_FLAG_SET"; break;
        case SCRIPT_COMMAND_FLAG_REMOVE: res = "SCRIPT_COMMAND_FLAG_REMOVE"; break;
        case SCRIPT_COMMAND_TELEPORT_TO: res = "SCRIPT_COMMAND_TELEPORT_TO"; break;
        case SCRIPT_COMMAND_QUEST_EXPLORED: res = "SCRIPT_COMMAND_QUEST_EXPLORED"; break;
        case SCRIPT_COMMAND_KILL_CREDIT: res = "SCRIPT_COMMAND_KILL_CREDIT"; break;
        case SCRIPT_COMMAND_RESPAWN_GAMEOBJECT: res = "SCRIPT_COMMAND_RESPAWN_GAMEOBJECT"; break;
        case SCRIPT_COMMAND_TEMP_SUMMON_CREATURE: res = "SCRIPT_COMMAND_TEMP_SUMMON_CREATURE"; break;
        case SCRIPT_COMMAND_OPEN_DOOR: res = "SCRIPT_COMMAND_OPEN_DOOR"; break;
        case SCRIPT_COMMAND_CLOSE_DOOR: res = "SCRIPT_COMMAND_CLOSE_DOOR"; break;
        case SCRIPT_COMMAND_ACTIVATE_OBJECT: res = "SCRIPT_COMMAND_ACTIVATE_OBJECT"; break;
        case SCRIPT_COMMAND_REMOVE_AURA: res = "SCRIPT_COMMAND_REMOVE_AURA"; break;
        case SCRIPT_COMMAND_CAST_SPELL: res = "SCRIPT_COMMAND_CAST_SPELL"; break;
        case SCRIPT_COMMAND_PLAY_SOUND: res = "SCRIPT_COMMAND_PLAY_SOUND"; break;
        case SCRIPT_COMMAND_CREATE_ITEM: res = "SCRIPT_COMMAND_CREATE_ITEM"; break;
        case SCRIPT_COMMAND_DESPAWN_SELF: res = "SCRIPT_COMMAND_DESPAWN_SELF"; break;
        case SCRIPT_COMMAND_LOAD_PATH: res = "SCRIPT_COMMAND_LOAD_PATH"; break;
        case SCRIPT_COMMAND_CALLSCRIPT_TO_UNIT: res = "SCRIPT_COMMAND_CALLSCRIPT_TO_UNIT"; break;
        case SCRIPT_COMMAND_KILL: res = "SCRIPT_COMMAND_KILL"; break;
        // TrinityCore only
        case SCRIPT_COMMAND_ORIENTATION: res = "SCRIPT_COMMAND_ORIENTATION"; break;
        case SCRIPT_COMMAND_EQUIP: res = "SCRIPT_COMMAND_EQUIP"; break;
        case SCRIPT_COMMAND_MODEL: res = "SCRIPT_COMMAND_MODEL"; break;
        case SCRIPT_COMMAND_CLOSE_GOSSIP: res = "SCRIPT_COMMAND_CLOSE_GOSSIP"; break;
        case SCRIPT_COMMAND_PLAYMOVIE: res = "SCRIPT_COMMAND_PLAYMOVIE"; break;
        case SCRIPT_COMMAND_MOVEMENT: res = "SCRIPT_COMMAND_MOVEMENT"; break;
        default:
        {
            char sz[32];
            sprintf(sz, "Unknown command: %d", command);
            res = sz;
            break;
        }
    }
    return res;
}

std::string ScriptInfo::GetDebugInfo() const
{
    char sz[256];
    sprintf(sz, "%s ('%s' script id: %u)", GetScriptCommandName(command).c_str(), GetScriptsTableNameByType(type).c_str(), id);
    return std::string(sz);
}

bool normalizePlayerName(std::string& name)
{
    if (name.empty())
        return false;

    std::wstring tmp;
    if (!Utf8toWStr(name, tmp))
        return false;

    wstrToLower(tmp);
    if (!tmp.empty())
        tmp[0] = wcharToUpper(tmp[0]);

    if (!WStrToUtf8(tmp, name))
        return false;

    return true;
}

LanguageDesc lang_description[LANGUAGES_COUNT] =
{
    { LANG_ADDON,           0, 0                       },
    { LANG_UNIVERSAL,       0, 0                       },
    { LANG_ORCISH,        669, SKILL_LANG_ORCISH       },
    { LANG_DARNASSIAN,    671, SKILL_LANG_DARNASSIAN   },
    { LANG_TAURAHE,       670, SKILL_LANG_TAURAHE      },
    { LANG_DWARVISH,      672, SKILL_LANG_DWARVEN      },
    { LANG_COMMON,        668, SKILL_LANG_COMMON       },
    { LANG_DEMONIC,       815, SKILL_LANG_DEMON_TONGUE },
    { LANG_TITAN,         816, SKILL_LANG_TITAN        },
    { LANG_THALASSIAN,    813, SKILL_LANG_THALASSIAN   },
    { LANG_DRACONIC,      814, SKILL_LANG_DRACONIC     },
    { LANG_KALIMAG,       817, SKILL_LANG_OLD_TONGUE   },
    { LANG_GNOMISH,      7340, SKILL_LANG_GNOMISH      },
    { LANG_TROLL,        7341, SKILL_LANG_TROLL        },
    { LANG_GUTTERSPEAK, 17737, SKILL_LANG_GUTTERSPEAK  },
    { LANG_DRAENEI,     29932, SKILL_LANG_DRAENEI      },
    { LANG_ZOMBIE,          0, 0                       },
    { LANG_GNOMISH_BINARY,  0, 0                       },
    { LANG_GOBLIN_BINARY,   0, 0                       }
};

LanguageDesc const* GetLanguageDescByID(uint32 lang)
{
    for (uint8 i = 0; i < LANGUAGES_COUNT; ++i)
    {
        if (uint32(lang_description[i].lang_id) == lang)
            return &lang_description[i];
    }

    return nullptr;
}

bool SpellClickInfo::IsFitToRequirements(Unit const* clicker, Unit const* clickee) const
{
    Player const* playerClicker = clicker->ToPlayer();
    if (!playerClicker)
        return true;

    Unit const* summoner = nullptr;
    // Check summoners for party
    if (clickee->IsSummon())
        summoner = clickee->ToTempSummon()->GetSummonerUnit();
    if (!summoner)
        summoner = clickee;

    // This only applies to players
    switch (userType)
    {
        case SPELL_CLICK_USER_FRIEND:
            if (!playerClicker->IsFriendlyTo(summoner))
                return false;
            break;
        case SPELL_CLICK_USER_RAID:
            if (!playerClicker->IsInRaidWith(summoner))
                return false;
            break;
        case SPELL_CLICK_USER_PARTY:
            if (!playerClicker->IsInPartyWith(summoner))
                return false;
            break;
        default:
            break;
    }

    return true;
}

ObjectMgr::ObjectMgr():
    _auctionId(1),
    _equipmentSetGuid(1),
    _mailId(1),
    _hiPetNumber(1),
    _creatureSpawnId(1),
    _gameObjectSpawnId(1),
    DBCLocaleIndex(LOCALE_enUS)
{
}

ObjectMgr* ObjectMgr::instance()
{
    static ObjectMgr instance;
    return &instance;
}

ObjectMgr::~ObjectMgr()
{
}

void ObjectMgr::AddLocaleString(std::string&& value, LocaleConstant localeConstant, std::vector<std::string>& data)
{
    if (!value.empty())
    {
        if (data.size() <= size_t(localeConstant))
            data.resize(localeConstant + 1);

        data[localeConstant] = std::move(value);
    }
}

void ObjectMgr::LoadCreatureLocales()
{
    uint32 oldMSTime = getMSTime();

    _creatureLocaleStore.clear();                              // need for reload case

    //                                               0      1       2     3
    QueryResult result = WorldDatabase.Query("SELECT entry, locale, Name, Title FROM creature_template_locale");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id               = fields[0].GetUInt32();
        std::string localeName  = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        CreatureLocale& data = _creatureLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.Name);
        AddLocaleString(fields[3].GetString(), locale, data.Title);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature locale strings in {} ms", uint32(_creatureLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadGossipMenuItemsLocales()
{
    uint32 oldMSTime = getMSTime();

    _gossipMenuItemsLocaleStore.clear();                              // need for reload case

    //                                               0       1            2       3           4
    QueryResult result = WorldDatabase.Query("SELECT MenuID, OptionID, Locale, OptionText, BoxText FROM gossip_menu_option_locale");

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint16 menuId           = fields[0].GetUInt16();
        uint16 optionId         = fields[1].GetUInt16();
        std::string localeName  = fields[2].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        GossipMenuItemsLocale& data = _gossipMenuItemsLocaleStore[std::make_pair(menuId, optionId)];
        AddLocaleString(fields[3].GetString(), locale, data.OptionText);
        AddLocaleString(fields[4].GetString(), locale, data.BoxText);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gossip_menu_option locale strings in {} ms", _gossipMenuItemsLocaleStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadPointOfInterestLocales()
{
    uint32 oldMSTime = getMSTime();

    _pointOfInterestLocaleStore.clear();                              // need for reload case

    //                                               0   1       2
    QueryResult result = WorldDatabase.Query("SELECT ID, locale, Name FROM points_of_interest_locale");

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id               = fields[0].GetUInt32();
        std::string localeName  = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        PointOfInterestLocale& data = _pointOfInterestLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.Name);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} points_of_interest locale strings in {} ms", uint32(_pointOfInterestLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureTemplates()
{
    uint32 oldMSTime = getMSTime();

    // Steps to update the counter below without doing it 1 by 1 manually
    // 1. Using Notepad++ copy the query from "SELECT" to last field
    // 2. Run this regex
    //  a.find     "\r\n[ ]+\/\/[ ]+[0-9]+
    //  b.replace "\/\/
    // 3. Alt + Left Click and vertical select all columns enough on the right of the file to be after // in all lines
    // 4. Select "Edit" in the menu and then "Column Editor.."
    // 5. Select "Number to Insert", Initial number 1, Increase by 1
    // 6. Run this regex
    //  a.find    "\/\/[ ]+
    //  b.replace "\r\n\t\t\/\/ (not that there is a space at the end of the regex, it's needed)

    QueryResult result = WorldDatabase.Query(
        //  0
        "SELECT entry,"
        //  1
        "difficulty_entry_1,"
        //  2
        "difficulty_entry_2,"
        //  3
        "difficulty_entry_3,"
        //  4
        "KillCredit1,"
        //  5
        "KillCredit2,"
        //  6
        "modelid1,"
        //  7
        "modelid2,"
        //  8
        "modelid3,"
        //  9
        "modelid4,"
        // 10
        "name,"
        // 11
        "subname,"
        // 12
        "IconName,"
        // 13
        "gossip_menu_id,"
        // 14
        "minlevel,"
        // 15
        "maxlevel,"
        // 16
        "exp,"
        // 17
        "faction,"
        // 18
        "npcflag,"
        // 19
        "speed_walk,"
        // 20
        "speed_run,"
        // 21
        "scale,"
        // 22
        "`rank`,"
        // 23
        "dmgschool,"
        // 24
        "BaseAttackTime,"
        // 25
        "RangeAttackTime,"
        // 26
        "BaseVariance,"
        // 27
        "RangeVariance,"
        // 28
        "unit_class,"
        // 29
        "unit_flags,"
        // 30
        "unit_flags2,"
        // 31
        "dynamicflags,"
        // 32
        "family,"
        // 33
        "type,"
        // 34
        "type_flags,"
        // 35
        "lootid,"
        // 36
        "pickpocketloot,"
        // 37
        "skinloot,"
        // 38
        "PetSpellDataId,"
        // 39
        "VehicleId,"
        // 40
        "mingold,"
        // 41
        "maxgold,"
        // 42
        "AIName,"
        // 43
        "MovementType,"
        // 44
        "ctm.Ground,"
        // 45
        "ctm.Swim,"
        // 46
        "ctm.Flight,"
        // 47
        "ctm.Rooted,"
        // 48
        "ctm.Chase,"
        // 49
        "ctm.Random,"
        // 50
        "ctm.InteractionPauseTimer,"
        // 51
        "HoverHeight,"
        // 52
        "HealthModifier,"
        // 53
        "ManaModifier,"
        // 54
        "ArmorModifier,"
        // 55
        "DamageModifier,"
        // 56
        "ExperienceModifier,"
        // 57
        "RacialLeader,"
        // 58
        "movementId,"
        // 59
        "RegenHealth,"
        // 60
        "mechanic_immune_mask,"
        // 61
        "spell_school_immune_mask,"
        // 62
        "flags_extra,"
        // 63
        "ScriptName,"
        // 64
        "StringId"
        " FROM creature_template ct"
        " LEFT JOIN creature_template_movement ctm ON ct.entry = ctm.CreatureId");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature template definitions. DB table `creature_template` is empty.");
        return;
    }

    _creatureTemplateStore.reserve(result->GetRowCount());
    do
    {
        Field* fields = result->Fetch();
        LoadCreatureTemplate(fields);
    } while (result->NextRow());

    LoadCreatureTemplateResistances();
    LoadCreatureTemplateSpells();

    // Checking needs to be done after loading because of the difficulty self referencing
    for (auto const& ctPair : _creatureTemplateStore)
        CheckCreatureTemplate(&ctPair.second);

    TC_LOG_INFO("server.loading", ">> Loaded {} creature definitions in {} ms", _creatureTemplateStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureTemplate(Field* fields)
{
    uint32 entry = fields[0].GetUInt32();
    CreatureTemplate& creatureTemplate = _creatureTemplateStore[entry];

    creatureTemplate.Entry = entry;

    for (uint8 i = 0; i < MAX_DIFFICULTY - 1; ++i)
        creatureTemplate.DifficultyEntry[i] = fields[1 + i].GetUInt32();

    for (uint8 i = 0; i < MAX_KILL_CREDIT; ++i)
        creatureTemplate.KillCredit[i] = fields[4 + i].GetUInt32();

    creatureTemplate.Modelid1         = fields[6].GetUInt32();
    creatureTemplate.Modelid2         = fields[7].GetUInt32();
    creatureTemplate.Modelid3         = fields[8].GetUInt32();
    creatureTemplate.Modelid4         = fields[9].GetUInt32();
    creatureTemplate.Name             = fields[10].GetString();
    creatureTemplate.Title            = fields[11].GetString();
    creatureTemplate.IconName         = fields[12].GetString();
    creatureTemplate.GossipMenuId     = fields[13].GetUInt32();
    creatureTemplate.minlevel         = fields[14].GetUInt8();
    creatureTemplate.maxlevel         = fields[15].GetUInt8();
    creatureTemplate.expansion        = uint32(fields[16].GetInt16());
    creatureTemplate.faction          = fields[17].GetUInt16();
    creatureTemplate.npcflag          = fields[18].GetUInt32();
    creatureTemplate.speed_walk       = fields[19].GetFloat();
    creatureTemplate.speed_run        = fields[20].GetFloat();
    creatureTemplate.scale            = fields[21].GetFloat();
    creatureTemplate.rank             = fields[22].GetUInt8();
    creatureTemplate.dmgschool        = uint32(fields[23].GetInt8());
    creatureTemplate.BaseAttackTime   = fields[24].GetUInt32();
    creatureTemplate.RangeAttackTime  = fields[25].GetUInt32();
    creatureTemplate.BaseVariance     = fields[26].GetFloat();
    creatureTemplate.RangeVariance    = fields[27].GetFloat();
    creatureTemplate.unit_class       = fields[28].GetUInt8();
    creatureTemplate.unit_flags       = fields[29].GetUInt32();
    creatureTemplate.unit_flags2      = fields[30].GetUInt32();
    creatureTemplate.dynamicflags     = fields[31].GetUInt32();
    creatureTemplate.family           = CreatureFamily(fields[32].GetUInt8());
    creatureTemplate.type             = fields[33].GetUInt8();
    creatureTemplate.type_flags       = fields[34].GetUInt32();
    creatureTemplate.lootid           = fields[35].GetUInt32();
    creatureTemplate.pickpocketLootId = fields[36].GetUInt32();
    creatureTemplate.SkinLootId       = fields[37].GetUInt32();

    for (uint8 i = SPELL_SCHOOL_HOLY; i < MAX_SPELL_SCHOOL; ++i)
        creatureTemplate.resistance[i] = 0;

    for (uint8 i = 0; i < MAX_CREATURE_SPELLS; ++i)
        creatureTemplate.spells[i] = 0;

    creatureTemplate.PetSpellDataId = fields[38].GetUInt32();
    creatureTemplate.VehicleId      = fields[39].GetUInt32();
    creatureTemplate.mingold        = fields[40].GetUInt32();
    creatureTemplate.maxgold        = fields[41].GetUInt32();
    creatureTemplate.AIName         = fields[42].GetString();
    creatureTemplate.MovementType   = fields[43].GetUInt8();
    if (!fields[44].IsNull())
        creatureTemplate.Movement.Ground = static_cast<CreatureGroundMovementType>(fields[44].GetUInt8());

    if (!fields[45].IsNull())
        creatureTemplate.Movement.Swim = fields[45].GetBool();

    if (!fields[46].IsNull())
        creatureTemplate.Movement.Flight = static_cast<CreatureFlightMovementType>(fields[46].GetUInt8());

    if (!fields[47].IsNull())
        creatureTemplate.Movement.Rooted = fields[47].GetBool();

    if (!fields[48].IsNull())
        creatureTemplate.Movement.Chase = static_cast<CreatureChaseMovementType>(fields[48].GetUInt8());

    if (!fields[49].IsNull())
        creatureTemplate.Movement.Random = static_cast<CreatureRandomMovementType>(fields[49].GetUInt8());

    if (!fields[50].IsNull())
        creatureTemplate.Movement.InteractionPauseTimer = fields[50].GetUInt32();

    creatureTemplate.HoverHeight    = fields[51].GetFloat();
    creatureTemplate.ModHealth      = fields[52].GetFloat();
    creatureTemplate.ModMana        = fields[53].GetFloat();
    creatureTemplate.ModArmor       = fields[54].GetFloat();
    creatureTemplate.ModDamage      = fields[55].GetFloat();
    creatureTemplate.ModExperience  = fields[56].GetFloat();
    creatureTemplate.RacialLeader   = fields[57].GetBool();

    creatureTemplate.movementId            = fields[58].GetUInt32();
    creatureTemplate.RegenHealth           = fields[59].GetBool();
    creatureTemplate.MechanicImmuneMask    = fields[60].GetUInt32();
    creatureTemplate.SpellSchoolImmuneMask = fields[61].GetUInt32();
    creatureTemplate.flags_extra           = fields[62].GetUInt32();
    creatureTemplate.ScriptID              = GetScriptId(fields[63].GetString());
    creatureTemplate.StringId              = fields[64].GetString();
}

void ObjectMgr::LoadCreatureTemplateResistances()
{
    uint32 oldMSTime = getMSTime();

    //                                               0           1       2
    QueryResult result = WorldDatabase.Query("SELECT CreatureID, School, Resistance FROM creature_template_resistance");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature template resistance definitions. DB table `creature_template_resistance` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 creatureID = fields[0].GetUInt32();
        uint8 school      = fields[1].GetUInt8();

        if (school == SPELL_SCHOOL_NORMAL || school >= MAX_SPELL_SCHOOL)
        {
            TC_LOG_ERROR("sql.sql", "creature_template_resistance has resistance definitions for creature {} but this school {} doesn't exist", creatureID, school);
            continue;
        }

        CreatureTemplateContainer::iterator itr = _creatureTemplateStore.find(creatureID);
        if (itr == _creatureTemplateStore.end())
        {
            TC_LOG_ERROR("sql.sql", "creature_template_resistance has resistance definitions for creature {} but this creature doesn't exist", creatureID);
            continue;
        }

        CreatureTemplate& creatureTemplate = itr->second;
        creatureTemplate.resistance[school] = fields[2].GetInt16();

        ++count;

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature template resistances in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureTemplateSpells()
{
    uint32 oldMSTime = getMSTime();

    //                                               0           1       2
    QueryResult result = WorldDatabase.Query("SELECT CreatureID, `Index`, Spell FROM creature_template_spell");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature template spell definitions. DB table `creature_template_spell` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 creatureID = fields[0].GetUInt32();
        uint8 index       = fields[1].GetUInt8();

        if (index >= MAX_CREATURE_SPELLS)
        {
            TC_LOG_ERROR("sql.sql", "creature_template_spell has spell definitions for creature {} with a incorrect index {}", creatureID, index);
            continue;
        }

        CreatureTemplateContainer::iterator itr = _creatureTemplateStore.find(creatureID);
        if (itr == _creatureTemplateStore.end())
        {
            TC_LOG_ERROR("sql.sql", "creature_template_spell has spell definitions for creature {} but this creature doesn't exist", creatureID);
            continue;
        }

        CreatureTemplate& creatureTemplate = itr->second;
        creatureTemplate.spells[index] = fields[2].GetUInt32();;

        ++count;

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature template spells in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureTemplateAddons()
{
    uint32 oldMSTime = getMSTime();

    //                                               0      1        2      3           4         5         6            7         8      9                       10
    QueryResult result = WorldDatabase.Query("SELECT entry, path_id, mount, StandState, AnimTier, VisFlags, SheathState, PvPFlags, emote, visibilityDistanceType, auras FROM creature_template_addon");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature template addon definitions. DB table `creature_template_addon` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(entry))
        {
            TC_LOG_ERROR("sql.sql", "Creature template (Entry: {}) does not exist but has a record in `creature_template_addon`", entry);
            continue;
        }

        CreatureAddon& creatureAddon = _creatureTemplateAddonStore[entry];

        creatureAddon.path_id                   = fields[1].GetUInt32();
        creatureAddon.mount                     = fields[2].GetUInt32();
        creatureAddon.standState                = fields[3].GetUInt8();
        creatureAddon.animTier                  = fields[4].GetUInt8();
        creatureAddon.visFlags                  = fields[5].GetUInt8();
        creatureAddon.sheathState               = fields[6].GetUInt8();
        creatureAddon.pvpFlags                  = fields[7].GetUInt8();
        creatureAddon.emote                     = fields[8].GetUInt32();
        creatureAddon.visibilityDistanceType    = VisibilityDistanceType(fields[9].GetUInt8());

        for (std::string_view aura : Trinity::Tokenize(fields[10].GetStringView(), ' ', false))
        {

            SpellInfo const* spellInfo = nullptr;
            if (Optional<uint32> spellId = Trinity::StringTo<uint32>(aura))
                spellInfo = sSpellMgr->GetSpellInfo(*spellId);

            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has wrong spell '{}' defined in `auras` field in `creature_template_addon`.", entry, std::string(aura));
                continue;
            }

            if (spellInfo->HasAura(SPELL_AURA_CONTROL_VEHICLE))
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has SPELL_AURA_CONTROL_VEHICLE aura {} defined in `auras` field in `creature_template_addon`.", entry, spellInfo->Id);

            if (std::find(creatureAddon.auras.begin(), creatureAddon.auras.end(), spellInfo->Id) != creatureAddon.auras.end())
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has duplicate aura (spell {}) in `auras` field in `creature_template_addon`.", entry, spellInfo->Id);
                continue;
            }

            if (spellInfo->GetDuration() > 0)
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has temporary aura (spell {}) in `auras` field in `creature_template_addon`.", entry, spellInfo->Id);
                continue;
            }

            creatureAddon.auras.push_back(spellInfo->Id);
        }

        if (creatureAddon.mount)
        {
            if (!sCreatureDisplayInfoStore.LookupEntry(creatureAddon.mount))
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid displayInfoId ({}) for mount defined in `creature_template_addon`", entry, creatureAddon.mount);
                creatureAddon.mount = 0;
            }
        }

        if (creatureAddon.standState >= MAX_UNIT_STAND_STATE)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid unit stand state ({}) defined in `creature_template_addon`. Truncated to 0.", entry, creatureAddon.standState);
            creatureAddon.standState = 0;
        }

        if (AnimTier(creatureAddon.animTier) >= AnimTier::Max)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid animation tier ({}) defined in `creature_template_addon`. Truncated to 0.", entry, creatureAddon.animTier);
            creatureAddon.animTier = 0;
        }

        if (creatureAddon.sheathState >= MAX_SHEATH_STATE)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid sheath state ({}) defined in `creature_template_addon`. Truncated to 0.", entry, creatureAddon.sheathState);
            creatureAddon.sheathState = 0;
        }

        // PvPFlags don't need any checking for the time being since they cover the entire range of a byte

        if (!sEmotesStore.LookupEntry(creatureAddon.emote))
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid emote ({}) defined in `creature_template_addon`.", entry, creatureAddon.emote);
            creatureAddon.emote = 0;
        }

        if (creatureAddon.visibilityDistanceType >= VisibilityDistanceType::Max)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid visibilityDistanceType ({}) defined in `creature_template_addon`.",
                entry, AsUnderlyingType(creatureAddon.visibilityDistanceType));
            creatureAddon.visibilityDistanceType = VisibilityDistanceType::Normal;
        }

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature template addons in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::CheckCreatureTemplate(CreatureTemplate const* cInfo)
{
    if (!cInfo)
        return;

    bool ok = true;                                     // bool to allow continue outside this loop
    for (uint32 diff = 0; diff < MAX_DIFFICULTY - 1 && ok; ++diff)
    {
        if (!cInfo->DifficultyEntry[diff])
            continue;
        ok = false;                                     // will be set to true at the end of this loop again

        CreatureTemplate const* difficultyInfo = GetCreatureTemplate(cInfo->DifficultyEntry[diff]);
        if (!difficultyInfo)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has `difficulty_entry_{}`={} but creature entry {} does not exist.",
                cInfo->Entry, diff + 1, cInfo->DifficultyEntry[diff], cInfo->DifficultyEntry[diff]);
            continue;
        }

        bool ok2 = true;
        for (uint32 diff2 = 0; diff2 < MAX_DIFFICULTY - 1 && ok2; ++diff2)
        {
            ok2 = false;
            if (_difficultyEntries[diff2].find(cInfo->Entry) != _difficultyEntries[diff2].end())
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) is listed as `difficulty_entry_{}` of another creature, but itself lists {} in `difficulty_entry_{}`.",
                    cInfo->Entry, diff2 + 1, cInfo->DifficultyEntry[diff], diff + 1);
                continue;
            }

            if (_difficultyEntries[diff2].find(cInfo->DifficultyEntry[diff]) != _difficultyEntries[diff2].end())
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) already listed as `difficulty_entry_{}` for another entry.", cInfo->DifficultyEntry[diff], diff2 + 1);
                continue;
            }

            if (_hasDifficultyEntries[diff2].find(cInfo->DifficultyEntry[diff]) != _hasDifficultyEntries[diff2].end())
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has `difficulty_entry_{}`={} but creature entry {} has itself a value in `difficulty_entry_{}`.",
                    cInfo->Entry, diff + 1, cInfo->DifficultyEntry[diff], cInfo->DifficultyEntry[diff], diff2 + 1);
                continue;
            }
            ok2 = true;
        }

        if (!ok2)
            continue;

        if (cInfo->expansion > difficultyInfo->expansion)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, exp: {}) has different `exp` in difficulty {} mode (Entry: {}, exp: {}).",
                cInfo->Entry, cInfo->expansion, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->expansion);
        }

        if (cInfo->minlevel > difficultyInfo->minlevel)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, minlevel: {}) has lower `minlevel` in difficulty {} mode (Entry: {}, minlevel: {}).",
                cInfo->Entry, cInfo->minlevel, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->minlevel);
        }

        if (cInfo->maxlevel > difficultyInfo->maxlevel)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, maxlevel: {}) has lower `maxlevel` in difficulty {} mode (Entry: {}, maxlevel: {}).",
                cInfo->Entry, cInfo->maxlevel, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->maxlevel);
        }

        if (cInfo->faction != difficultyInfo->faction)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, faction: {}) has different `faction` in difficulty {} mode (Entry: {}, faction: {}).",
                cInfo->Entry, cInfo->faction, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->faction);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `faction`={} WHERE `entry`={};",
                cInfo->faction, cInfo->DifficultyEntry[diff]);
        }

        if (cInfo->unit_class != difficultyInfo->unit_class)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, class: {}) has different `unit_class` in difficulty {} mode (Entry: {}, class: {}).",
                cInfo->Entry, cInfo->unit_class, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->unit_class);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `unit_class`={} WHERE `entry`={};",
                cInfo->unit_class, cInfo->DifficultyEntry[diff]);
            continue;
        }

        uint32 differenceMask = cInfo->npcflag ^ difficultyInfo->npcflag;
        if (cInfo->npcflag != difficultyInfo->npcflag)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, `npcflag`: {}) has different `npcflag` in difficulty {} mode (Entry: {}, `npcflag`: {}).",
                cInfo->Entry, cInfo->npcflag, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->npcflag);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `npcflag`=`npcflag`^{} WHERE `entry`={};",
                differenceMask, cInfo->DifficultyEntry[diff]);
        }

        if (cInfo->dmgschool != difficultyInfo->dmgschool)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, `dmgschool`: {}) has different `dmgschool` in difficulty {} mode (Entry: {}, `dmgschool`: {}).",
                cInfo->Entry, cInfo->dmgschool, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->dmgschool);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `dmgschool`={} WHERE `entry`={};",
                cInfo->dmgschool, cInfo->DifficultyEntry[diff]);
        }

        differenceMask = cInfo->unit_flags2 ^ difficultyInfo->unit_flags2;
        if (cInfo->unit_flags2 != difficultyInfo->unit_flags2)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, `unit_flags2`: {}) has different `unit_flags2` in difficulty {} mode (Entry: {}, `unit_flags2`: {}).",
                cInfo->Entry, cInfo->unit_flags2, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->unit_flags2);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `unit_flags2`=`unit_flags2`^{} WHERE `entry`={};",
                differenceMask, cInfo->DifficultyEntry[diff]);
        }

        if (cInfo->family != difficultyInfo->family)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, family: {}) has different `family` in difficulty {} mode (Entry: {}, family: {}).",
                cInfo->Entry, cInfo->family, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->family);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `family`={} WHERE `entry`={};",
                cInfo->family, cInfo->DifficultyEntry[diff]);
        }

        if (cInfo->type != difficultyInfo->type)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, type: {}) has different `type` in difficulty {} mode (Entry: {}, type: {}).",
                cInfo->Entry, cInfo->type, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->type);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `type`={} WHERE `entry`={};",
                cInfo->type, cInfo->DifficultyEntry[diff]);
        }

        if (!cInfo->VehicleId && difficultyInfo->VehicleId)
        {
            TC_LOG_ERROR("sql.sql", "Non-vehicle Creature (Entry: {}, VehicleId: {}) has `VehicleId` set in difficulty {} mode (Entry: {}, VehicleId: {}).",
                cInfo->Entry, cInfo->VehicleId, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->VehicleId);
        }

        if (cInfo->RegenHealth != difficultyInfo->RegenHealth)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, RegenHealth: {}) has different `RegenHealth` in difficulty {} mode (Entry: {}, RegenHealth: {}).",
                cInfo->Entry, cInfo->RegenHealth, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->RegenHealth);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `RegenHealth`={} WHERE `entry`={};",
                cInfo->RegenHealth, cInfo->DifficultyEntry[diff]);
        }

        differenceMask = cInfo->MechanicImmuneMask & (~difficultyInfo->MechanicImmuneMask);
        if (differenceMask)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, mechanic_immune_mask: {}) has weaker immunities in difficulty {} mode (Entry: {}, mechanic_immune_mask: {}).",
                cInfo->Entry, cInfo->MechanicImmuneMask, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->MechanicImmuneMask);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `mechanic_immune_mask`=`mechanic_immune_mask`|{} WHERE `entry`={};",
                differenceMask, cInfo->DifficultyEntry[diff]);
        }

        differenceMask = (cInfo->flags_extra ^ difficultyInfo->flags_extra) & (~CREATURE_FLAG_EXTRA_INSTANCE_BIND);
        if (differenceMask)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}, flags_extra: {}) has different `flags_extra` in difficulty {} mode (Entry: {}, flags_extra: {}).",
                cInfo->Entry, cInfo->flags_extra, diff + 1, cInfo->DifficultyEntry[diff], difficultyInfo->flags_extra);
            TC_LOG_ERROR("sql.sql", "Possible FIX: UPDATE `creature_template` SET `flags_extra`=`flags_extra`^{} WHERE `entry`={};",
                differenceMask, cInfo->DifficultyEntry[diff]);
        }

        if (!difficultyInfo->AIName.empty())
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) lists difficulty {} mode entry {} with `AIName` filled in. `AIName` of difficulty 0 mode creature is always used instead.",
                cInfo->Entry, diff + 1, cInfo->DifficultyEntry[diff]);
            continue;
        }

        if (difficultyInfo->ScriptID)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) lists difficulty {} mode entry {} with `ScriptName` filled in. `ScriptName` of difficulty 0 mode creature is always used instead.",
                cInfo->Entry, diff + 1, cInfo->DifficultyEntry[diff]);
            continue;
        }

        _hasDifficultyEntries[diff].insert(cInfo->Entry);
        _difficultyEntries[diff].insert(cInfo->DifficultyEntry[diff]);
        ok = true;
    }

    if (cInfo->mingold > cInfo->maxgold)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has `mingold` {} which is greater than `maxgold` {}, setting `maxgold` to {}.",
            cInfo->Entry, cInfo->mingold, cInfo->maxgold, cInfo->mingold);
        const_cast<CreatureTemplate*>(cInfo)->maxgold = cInfo->mingold;
    }

    if (!cInfo->AIName.empty())
    {
        auto registryItem = sCreatureAIRegistry->GetRegistryItem(cInfo->AIName);
        if (!registryItem)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has non-registered `AIName` '{}' set, removing", cInfo->Entry, cInfo->AIName);
            const_cast<CreatureTemplate*>(cInfo)->AIName.clear();
        }
        else
        {
            DBPermit const* permit = dynamic_cast<DBPermit const*>(registryItem);
            if (!ASSERT_NOTNULL(permit)->IsScriptNameAllowedInDB())
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has not-allowed `AIName` '{}' set, removing", cInfo->Entry, cInfo->AIName);
                const_cast<CreatureTemplate*>(cInfo)->AIName.clear();
            }
        }
    }

    FactionTemplateEntry const* factionTemplate = sFactionTemplateStore.LookupEntry(cInfo->faction);
    if (!factionTemplate)
    {
        TC_LOG_FATAL("sql.sql", "Creature (Entry: {}) has non-existing faction template ({}). This can lead to crashes, aborting.", cInfo->Entry, cInfo->faction);
        ABORT();
    }

    // used later for scale
    CreatureDisplayInfoEntry const* displayScaleEntry = nullptr;

    if (cInfo->Modelid1)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid1);
        if (!displayEntry)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) lists non-existing Modelid1 id ({}), this can crash the client.", cInfo->Entry, cInfo->Modelid1);
            const_cast<CreatureTemplate*>(cInfo)->Modelid1 = 0;
        }
        else
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid1);
        if (!modelInfo)
            TC_LOG_ERROR("sql.sql", "No model data exist for `Modelid1` = {} listed by creature (Entry: {}).", cInfo->Modelid1, cInfo->Entry);
    }

    if (cInfo->Modelid2)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid2);
        if (!displayEntry)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) lists non-existing Modelid2 id ({}), this can crash the client.", cInfo->Entry, cInfo->Modelid2);
            const_cast<CreatureTemplate*>(cInfo)->Modelid2 = 0;
        }
        else if (!displayScaleEntry)
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid2);
        if (!modelInfo)
            TC_LOG_ERROR("sql.sql", "No model data exist for `Modelid2` = {} listed by creature (Entry: {}).", cInfo->Modelid2, cInfo->Entry);
    }

    if (cInfo->Modelid3)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid3);
        if (!displayEntry)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) lists non-existing Modelid3 id ({}), this can crash the client.", cInfo->Entry, cInfo->Modelid3);
            const_cast<CreatureTemplate*>(cInfo)->Modelid3 = 0;
        }
        else if (!displayScaleEntry)
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid3);
        if (!modelInfo)
            TC_LOG_ERROR("sql.sql", "No model data exist for `Modelid3` = {} listed by creature (Entry: {}).", cInfo->Modelid3, cInfo->Entry);
    }

    if (cInfo->Modelid4)
    {
        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(cInfo->Modelid4);
        if (!displayEntry)
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) lists non-existing Modelid4 id ({}), this can crash the client.", cInfo->Entry, cInfo->Modelid4);
            const_cast<CreatureTemplate*>(cInfo)->Modelid4 = 0;
        }
        else if (!displayScaleEntry)
            displayScaleEntry = displayEntry;

        CreatureModelInfo const* modelInfo = GetCreatureModelInfo(cInfo->Modelid4);
        if (!modelInfo)
            TC_LOG_ERROR("sql.sql", "No model data exist for `Modelid4` = {} listed by creature (Entry: {}).", cInfo->Modelid4, cInfo->Entry);
    }

    if (!displayScaleEntry)
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) does not have any existing display id in Modelid1/Modelid2/Modelid3/Modelid4.", cInfo->Entry);

    for (uint8 k = 0; k < MAX_KILL_CREDIT; ++k)
    {
        if (cInfo->KillCredit[k])
        {
            if (!GetCreatureTemplate(cInfo->KillCredit[k]))
            {
                TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) lists non-existing creature entry {} in `KillCredit{}`.", cInfo->Entry, cInfo->KillCredit[k], k + 1);
                const_cast<CreatureTemplate*>(cInfo)->KillCredit[k] = 0;
            }
        }
    }

    if (!cInfo->unit_class || ((1 << (cInfo->unit_class-1)) & CLASSMASK_ALL_CREATURES) == 0)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid unit_class ({}) in creature_template. Set to 1 (UNIT_CLASS_WARRIOR).", cInfo->Entry, cInfo->unit_class);
        const_cast<CreatureTemplate*>(cInfo)->unit_class = UNIT_CLASS_WARRIOR;
    }

    if (cInfo->dmgschool >= MAX_SPELL_SCHOOL)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid spell school value ({}) in `dmgschool`.", cInfo->Entry, cInfo->dmgschool);
        const_cast<CreatureTemplate*>(cInfo)->dmgschool = SPELL_SCHOOL_NORMAL;
    }

    if (cInfo->BaseAttackTime == 0)
        const_cast<CreatureTemplate*>(cInfo)->BaseAttackTime  = BASE_ATTACK_TIME;

    if (cInfo->RangeAttackTime == 0)
        const_cast<CreatureTemplate*>(cInfo)->RangeAttackTime = BASE_ATTACK_TIME;

    if (cInfo->speed_walk == 0.0f)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has wrong value ({}) in speed_walk, set to 1.", cInfo->Entry, cInfo->speed_walk);
        const_cast<CreatureTemplate*>(cInfo)->speed_walk = 1.0f;
    }

    if (cInfo->speed_run == 0.0f)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has wrong value ({}) in speed_run, set to 1.14286.", cInfo->Entry, cInfo->speed_run);
        const_cast<CreatureTemplate*>(cInfo)->speed_run = 1.14286f;
    }

    if (cInfo->type && !sCreatureTypeStore.LookupEntry(cInfo->type))
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid creature type ({}) in `type`.", cInfo->Entry, cInfo->type);
        const_cast<CreatureTemplate*>(cInfo)->type = CREATURE_TYPE_HUMANOID;
    }

    // must exist or used hidden but used in data horse case
    if (cInfo->family && !sCreatureFamilyStore.LookupEntry(cInfo->family) && cInfo->family != CREATURE_FAMILY_HORSE_CUSTOM)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has invalid creature family ({}) in `family`.", cInfo->Entry, cInfo->family);
        const_cast<CreatureTemplate*>(cInfo)->family = CREATURE_FAMILY_NONE;
    }

    CheckCreatureMovement("creature_template_movement", cInfo->Entry, const_cast<CreatureTemplate*>(cInfo)->Movement);

    if (cInfo->HoverHeight < 0.0f)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has wrong value ({}) in `HoverHeight`", cInfo->Entry, cInfo->HoverHeight);
        const_cast<CreatureTemplate*>(cInfo)->HoverHeight = 1.0f;
    }

    if (cInfo->VehicleId)
    {
        VehicleEntry const* vehId = sVehicleStore.LookupEntry(cInfo->VehicleId);
        if (!vehId)
        {
             TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has a non-existing VehicleId ({}). This *WILL* cause the client to freeze!", cInfo->Entry, cInfo->VehicleId);
             const_cast<CreatureTemplate*>(cInfo)->VehicleId = 0;
        }
    }

    if (cInfo->PetSpellDataId)
    {
        CreatureSpellDataEntry const* spellDataId = sCreatureSpellDataStore.LookupEntry(cInfo->PetSpellDataId);
        if (!spellDataId)
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has non-existing PetSpellDataId ({}).", cInfo->Entry, cInfo->PetSpellDataId);
    }

    for (uint8 j = 0; j < MAX_CREATURE_SPELLS; ++j)
    {
        if (cInfo->spells[j] && !sSpellMgr->GetSpellInfo(cInfo->spells[j]))
        {
            TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has non-existing Spell{} ({}), set to 0.", cInfo->Entry, j+1, cInfo->spells[j]);
            const_cast<CreatureTemplate*>(cInfo)->spells[j] = 0;
        }
    }

    if (cInfo->MovementType >= MAX_DB_MOTION_TYPE)
    {
        TC_LOG_ERROR("sql.sql", "Creature (Entry: {}) has wrong movement generator type ({}), ignored and set to IDLE.", cInfo->Entry, cInfo->MovementType);
        const_cast<CreatureTemplate*>(cInfo)->MovementType = IDLE_MOTION_TYPE;
    }

    /// if not set custom creature scale then load scale from CreatureDisplayInfo.dbc
    if (cInfo->scale <= 0.0f)
    {
        if (displayScaleEntry)
            const_cast<CreatureTemplate*>(cInfo)->scale = displayScaleEntry->CreatureModelScale;
        else
            const_cast<CreatureTemplate*>(cInfo)->scale = 1.0f;
    }

    if (cInfo->expansion > (MAX_EXPANSIONS - 1))
    {
        TC_LOG_ERROR("sql.sql", "Table `creature_template` lists creature (Entry: {}) with expansion {}. Ignored and set to 0.", cInfo->Entry, cInfo->expansion);
        const_cast<CreatureTemplate*>(cInfo)->expansion = 0;
    }

    if (uint32 badFlags = (cInfo->flags_extra & ~CREATURE_FLAG_EXTRA_DB_ALLOWED))
    {
        TC_LOG_ERROR("sql.sql", "Table `creature_template` lists creature (Entry: {}) with disallowed `flags_extra` {}, removing incorrect flag.", cInfo->Entry, badFlags);
        const_cast<CreatureTemplate*>(cInfo)->flags_extra &= CREATURE_FLAG_EXTRA_DB_ALLOWED;
    }

    if (uint32 disallowedUnitFlags = (cInfo->unit_flags & ~UNIT_FLAG_ALLOWED))
    {
        TC_LOG_ERROR("sql.sql", "Table `creature_template` lists creature (Entry: {}) with disallowed `unit_flags` {}, removing incorrect flag.", cInfo->Entry, disallowedUnitFlags);
        const_cast<CreatureTemplate*>(cInfo)->unit_flags &= UNIT_FLAG_ALLOWED;
    }

    if (uint32 disallowedUnitFlags2 = (cInfo->unit_flags2 & ~UNIT_FLAG2_ALLOWED))
    {
        TC_LOG_ERROR("sql.sql", "Table `creature_template` lists creature (Entry: {}) with disallowed `unit_flags2` {}, removing incorrect flag.", cInfo->Entry, disallowedUnitFlags2);
        const_cast<CreatureTemplate*>(cInfo)->unit_flags2 &= UNIT_FLAG2_ALLOWED;
    }

    if (cInfo->dynamicflags)
    {
        TC_LOG_ERROR("sql.sql", "Table `creature_template` lists creature (Entry: {}) with `dynamicflags` > 0. Ignored and set to 0.", cInfo->Entry);
        const_cast<CreatureTemplate*>(cInfo)->dynamicflags = 0;
    }

    const_cast<CreatureTemplate*>(cInfo)->ModDamage *= Creature::_GetDamageMod(cInfo->rank);

    if (cInfo->GossipMenuId && !(cInfo->npcflag & UNIT_NPC_FLAG_GOSSIP))
        TC_LOG_INFO("sql.sql", "Creature (Entry: {}) has assigned gossip menu {}, but npcflag does not include UNIT_NPC_FLAG_GOSSIP.", cInfo->Entry, cInfo->GossipMenuId);
    else if (!cInfo->GossipMenuId && cInfo->npcflag & UNIT_NPC_FLAG_GOSSIP)
        TC_LOG_INFO("sql.sql", "Creature (Entry: {}) has npcflag UNIT_NPC_FLAG_GOSSIP, but gossip menu is unassigned.", cInfo->Entry);
}

void ObjectMgr::CheckCreatureMovement(char const* table, uint64 id, CreatureMovementData& creatureMovement)
{
    if (creatureMovement.Ground >= CreatureGroundMovementType::Max)
    {
        TC_LOG_ERROR("sql.sql", "`{}`.`Ground` wrong value ({}) for Id {}, setting to Run.",
            table, uint32(creatureMovement.Ground), id);
        creatureMovement.Ground = CreatureGroundMovementType::Run;
    }

    if (creatureMovement.Flight >= CreatureFlightMovementType::Max)
    {
        TC_LOG_ERROR("sql.sql", "`{}`.`Flight` wrong value ({}) for Id {}, setting to None.",
            table, uint32(creatureMovement.Flight), id);
        creatureMovement.Flight = CreatureFlightMovementType::None;
    }

    if (creatureMovement.Chase >= CreatureChaseMovementType::Max)
    {
        TC_LOG_ERROR("sql.sql", "`{}`.`Chase` wrong value ({}) for Id {}, setting to Run.",
                     table, uint32(creatureMovement.Chase), id);
        creatureMovement.Chase = CreatureChaseMovementType::Run;
    }

    if (creatureMovement.Random >= CreatureRandomMovementType::Max)
    {
        TC_LOG_ERROR("sql.sql", "`{}`.`Random` wrong value ({}) for Id {}, setting to Walk.",
                     table, uint32(creatureMovement.Random), id);
        creatureMovement.Random = CreatureRandomMovementType::Walk;
    }
}

void ObjectMgr::LoadCreatureAddons()
{
    uint32 oldMSTime = getMSTime();

    //                                               0     1        2      3           4         5         6            7         8      9                       10
    QueryResult result = WorldDatabase.Query("SELECT guid, path_id, mount, StandState, AnimTier, VisFlags, SheathState, PvPFlags, emote, visibilityDistanceType, auras FROM creature_addon");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature addon definitions. DB table `creature_addon` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        ObjectGuid::LowType guid = fields[0].GetUInt32();

        CreatureData const* creData = GetCreatureData(guid);
        if (!creData)
        {
            TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) does not exist but has a record in `creature_addon`", guid);
            continue;
        }

        CreatureAddon& creatureAddon = _creatureAddonStore[guid];

        creatureAddon.path_id = fields[1].GetUInt32();
        if (creData->movementType == WAYPOINT_MOTION_TYPE && !creatureAddon.path_id)
        {
            const_cast<CreatureData*>(creData)->movementType = IDLE_MOTION_TYPE;
            TC_LOG_ERROR("sql.sql", "Creature (GUID {}) has movement type set to WAYPOINT_MOTION_TYPE but no path assigned", guid);
        }

        creatureAddon.mount                     = fields[2].GetUInt32();
        creatureAddon.standState                = fields[3].GetUInt8();
        creatureAddon.animTier                  = fields[4].GetUInt8();
        creatureAddon.visFlags                  = fields[5].GetUInt8();
        creatureAddon.sheathState               = fields[6].GetUInt8();
        creatureAddon.pvpFlags                  = fields[7].GetUInt8();
        creatureAddon.emote                     = fields[8].GetUInt32();
        creatureAddon.visibilityDistanceType    = VisibilityDistanceType(fields[9].GetUInt8());

        for (std::string_view aura : Trinity::Tokenize(fields[10].GetStringView(), ' ', false))
        {
            SpellInfo const* spellInfo = nullptr;
            if (Optional<uint32> spellId = Trinity::StringTo<uint32>(aura))
                spellInfo = sSpellMgr->GetSpellInfo(*spellId);

            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has wrong spell '{}' defined in `auras` field in `creature_addon`.", guid, std::string(aura));
                continue;
            }

            if (spellInfo->HasAura(SPELL_AURA_CONTROL_VEHICLE))
                TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has SPELL_AURA_CONTROL_VEHICLE aura {} defined in `auras` field in `creature_addon`.", guid, spellInfo->Id);

            if (std::find(creatureAddon.auras.begin(), creatureAddon.auras.end(), spellInfo->Id) != creatureAddon.auras.end())
            {
                TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has duplicate aura (spell {}) in `auras` field in `creature_addon`.", guid, spellInfo->Id);
                continue;
            }

            if (spellInfo->GetDuration() > 0)
            {
                TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has temporary aura (spell {}) in `auras` field in `creature_addon`.", guid, spellInfo->Id);
                continue;
            }

            creatureAddon.auras.push_back(spellInfo->Id);
        }

        if (creatureAddon.mount)
        {
            if (!sCreatureDisplayInfoStore.LookupEntry(creatureAddon.mount))
            {
                TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has invalid displayInfoId ({}) for mount defined in `creature_addon`", guid, creatureAddon.mount);
                creatureAddon.mount = 0;
            }
        }

        if (creatureAddon.standState >= MAX_UNIT_STAND_STATE)
        {
            TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has invalid unit stand state ({}) defined in `creature_addon`. Truncated to 0.", guid, creatureAddon.standState);
            creatureAddon.standState = 0;
        }

        if (AnimTier(creatureAddon.animTier) >= AnimTier::Max)
        {
            TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has invalid animation tier ({}) defined in `creature_addon`. Truncated to 0.", guid, creatureAddon.animTier);
            creatureAddon.animTier = 0;
        }

        if (creatureAddon.sheathState >= MAX_SHEATH_STATE)
        {
            TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has invalid sheath state ({}) defined in `creature_addon`. Truncated to 0.", guid, creatureAddon.sheathState);
            creatureAddon.sheathState = 0;
        }

        // PvPFlags don't need any checking for the time being since they cover the entire range of a byte

        if (!sEmotesStore.LookupEntry(creatureAddon.emote))
        {
            TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has invalid emote ({}) defined in `creature_addon`.", guid, creatureAddon.emote);
            creatureAddon.emote = 0;
        }

        if (creatureAddon.visibilityDistanceType >= VisibilityDistanceType::Max)
        {
            TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) has invalid visibilityDistanceType ({}) defined in `creature_addon`.",
                guid, AsUnderlyingType(creatureAddon.visibilityDistanceType));
            creatureAddon.visibilityDistanceType = VisibilityDistanceType::Normal;
        }

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature addons in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadGameObjectAddons()
{
    uint32 oldMSTime = getMSTime();

    //                                               0     1                 2                 3                 4                 5                 6
    QueryResult result = WorldDatabase.Query("SELECT guid, parent_rotation0, parent_rotation1, parent_rotation2, parent_rotation3, invisibilityType, invisibilityValue FROM gameobject_addon");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gameobject addon definitions. DB table `gameobject_addon` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        ObjectGuid::LowType guid = fields[0].GetUInt32();

        GameObjectData const* goData = GetGameObjectData(guid);
        if (!goData)
        {
            TC_LOG_ERROR("sql.sql", "GameObject (GUID: {}) does not exist but has a record in `gameobject_addon`", guid);
            continue;
        }

        GameObjectAddon& gameObjectAddon = _gameObjectAddonStore[guid];
        gameObjectAddon.ParentRotation = QuaternionData(fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat());
        gameObjectAddon.invisibilityType = InvisibilityType(fields[5].GetUInt8());
        gameObjectAddon.InvisibilityValue = fields[6].GetUInt32();

        if (gameObjectAddon.invisibilityType >= TOTAL_INVISIBILITY_TYPES)
        {
            TC_LOG_ERROR("sql.sql", "GameObject (GUID: {}) has invalid InvisibilityType in `gameobject_addon`, disabled invisibility", guid);
            gameObjectAddon.invisibilityType = INVISIBILITY_GENERAL;
            gameObjectAddon.InvisibilityValue = 0;
        }

        if (gameObjectAddon.invisibilityType && !gameObjectAddon.InvisibilityValue)
        {
            TC_LOG_ERROR("sql.sql", "GameObject (GUID: {}) has InvisibilityType set but has no InvisibilityValue in `gameobject_addon`, set to 1", guid);
            gameObjectAddon.InvisibilityValue = 1;
        }

        if (!gameObjectAddon.ParentRotation.isUnit())
        {
            TC_LOG_ERROR("sql.sql", "GameObject (GUID: {}) has invalid parent rotation in `gameobject_addon`, set to default", guid);
            gameObjectAddon.ParentRotation = QuaternionData();
        }

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gameobject addons in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

GameObjectAddon const* ObjectMgr::GetGameObjectAddon(ObjectGuid::LowType lowguid) const
{
    GameObjectAddonContainer::const_iterator itr = _gameObjectAddonStore.find(lowguid);
    if (itr != _gameObjectAddonStore.end())
        return &(itr->second);

    return nullptr;
}

CreatureAddon const* ObjectMgr::GetCreatureAddon(ObjectGuid::LowType lowguid) const
{
    CreatureAddonContainer::const_iterator itr = _creatureAddonStore.find(lowguid);
    if (itr != _creatureAddonStore.end())
        return &(itr->second);

    return nullptr;
}

CreatureAddon const* ObjectMgr::GetCreatureTemplateAddon(uint32 entry) const
{
    CreatureAddonContainer::const_iterator itr = _creatureTemplateAddonStore.find(entry);
    if (itr != _creatureTemplateAddonStore.end())
        return &(itr->second);

    return nullptr;
}

CreatureMovementData const* ObjectMgr::GetCreatureMovementOverride(ObjectGuid::LowType spawnId) const
{
    return Trinity::Containers::MapGetValuePtr(_creatureMovementOverrides, spawnId);
}

EquipmentInfo const* ObjectMgr::GetEquipmentInfo(uint32 entry, int8& id) const
{
    EquipmentInfoContainer::const_iterator itr = _equipmentInfoStore.find(entry);
    if (itr == _equipmentInfoStore.end())
        return nullptr;

    if (itr->second.empty())
        return nullptr;

    if (id == -1) // select a random element
    {
        EquipmentInfoContainerInternal::const_iterator ritr = itr->second.begin();
        std::advance(ritr, urand(0u, itr->second.size() - 1));
        id = std::distance(itr->second.begin(), ritr) + 1;
        return &ritr->second;
    }
    else
    {
        EquipmentInfoContainerInternal::const_iterator itr2 = itr->second.find(id);
        if (itr2 != itr->second.end())
            return &itr2->second;
    }

    return nullptr;
}

void ObjectMgr::LoadEquipmentTemplates()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0         1       2       3       4
    QueryResult result = WorldDatabase.Query("SELECT CreatureID, ID, ItemID1, ItemID2, ItemID3 FROM creature_equip_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature equipment templates. DB table `creature_equip_template` is empty!");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(entry))
        {
            TC_LOG_ERROR("sql.sql", "Creature template (Entry: {}) does not exist but has a record in `creature_equip_template`", entry);
            continue;
        }

        uint8 id = fields[1].GetUInt8();
        if (!id)
        {
            TC_LOG_ERROR("sql.sql", "Creature equipment template with id 0 found for creature {}, skipped.", entry);
            continue;
        }

        EquipmentInfo& equipmentInfo = _equipmentInfoStore[entry][id];

        equipmentInfo.ItemEntry[0] = fields[2].GetUInt32();
        equipmentInfo.ItemEntry[1] = fields[3].GetUInt32();
        equipmentInfo.ItemEntry[2] = fields[4].GetUInt32();

        for (uint8 i = 0; i < MAX_EQUIPMENT_ITEMS; ++i)
        {
            if (!equipmentInfo.ItemEntry[i])
                continue;

            ItemEntry const* dbcItem = sItemStore.LookupEntry(equipmentInfo.ItemEntry[i]);

            if (!dbcItem)
            {
                TC_LOG_ERROR("sql.sql", "Unknown item (entry={}) in creature_equip_template.itemEntry{} for entry = {} and id={}, forced to 0.",
                    equipmentInfo.ItemEntry[i], i+1, entry, id);
                equipmentInfo.ItemEntry[i] = 0;
                continue;
            }

            if (dbcItem->InventoryType != INVTYPE_WEAPON &&
                dbcItem->InventoryType != INVTYPE_SHIELD &&
                dbcItem->InventoryType != INVTYPE_RANGED &&
                dbcItem->InventoryType != INVTYPE_2HWEAPON &&
                dbcItem->InventoryType != INVTYPE_WEAPONMAINHAND &&
                dbcItem->InventoryType != INVTYPE_WEAPONOFFHAND &&
                dbcItem->InventoryType != INVTYPE_HOLDABLE &&
                dbcItem->InventoryType != INVTYPE_THROWN &&
                dbcItem->InventoryType != INVTYPE_RANGEDRIGHT)
            {
                TC_LOG_ERROR("sql.sql", "Item (entry={}) in creature_equip_template.itemEntry{} for entry = {} and id = {} is not equipable in a hand, forced to 0.",
                    equipmentInfo.ItemEntry[i], i+1, entry, id);
                equipmentInfo.ItemEntry[i] = 0;
            }
        }

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} equipment templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureMovementOverrides()
{
    uint32 oldMSTime = getMSTime();

    _creatureMovementOverrides.clear();

    // Load the data from creature_movement_override and if NULL fallback to creature_template_movement
    QueryResult result = WorldDatabase.Query(
        "SELECT cmo.SpawnId,"
        "COALESCE(cmo.Ground, ctm.Ground),"
        "COALESCE(cmo.Swim, ctm.Swim),"
        "COALESCE(cmo.Flight, ctm.Flight),"
        "COALESCE(cmo.Rooted, ctm.Rooted),"
        "COALESCE(cmo.Chase, ctm.Chase),"
        "COALESCE(cmo.Random, ctm.Random),"
        "COALESCE(cmo.InteractionPauseTimer, ctm.InteractionPauseTimer) "
        "FROM creature_movement_override AS cmo "
        "LEFT JOIN creature AS c ON c.guid = cmo.SpawnId "
        "LEFT JOIN creature_template_movement AS ctm ON ctm.CreatureId = c.id");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature movement overrides. DB table `creature_movement_override` is empty!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        ObjectGuid::LowType spawnId = fields[0].GetUInt32();
        if (!GetCreatureData(spawnId))
        {
            TC_LOG_ERROR("sql.sql", "Creature (GUID: {}) does not exist but has a record in `creature_movement_override`", spawnId);
            continue;
        }

        CreatureMovementData& movement = _creatureMovementOverrides[spawnId];
        if (!fields[1].IsNull())
            movement.Ground = static_cast<CreatureGroundMovementType>(fields[1].GetUInt8());
        if (!fields[2].IsNull())
            movement.Swim = fields[2].GetBool();
        if (!fields[3].IsNull())
            movement.Flight = static_cast<CreatureFlightMovementType>(fields[3].GetUInt8());
        if (!fields[4].IsNull())
            movement.Rooted = fields[4].GetBool();
        if (!fields[5].IsNull())
            movement.Chase = static_cast<CreatureChaseMovementType>(fields[5].GetUInt8());
        if (!fields[6].IsNull())
            movement.Random = static_cast<CreatureRandomMovementType>(fields[6].GetUInt8());
        if (!fields[7].IsNull())
            movement.InteractionPauseTimer = fields[7].GetUInt32();

        CheckCreatureMovement("creature_movement_override", spawnId, movement);
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} movement overrides in {} ms", _creatureMovementOverrides.size(), GetMSTimeDiffToNow(oldMSTime));
}

CreatureModelInfo const* ObjectMgr::GetCreatureModelInfo(uint32 modelId) const
{
    CreatureModelContainer::const_iterator itr = _creatureModelStore.find(modelId);
    if (itr != _creatureModelStore.end())
        return &(itr->second);

    return nullptr;
}

uint32 ObjectMgr::ChooseDisplayId(CreatureTemplate const* cinfo, CreatureData const* data /*= nullptr*/)
{
    // Load creature model (display id)
    if (data && data->displayid)
        return data->displayid;

    if (!(cinfo->flags_extra & CREATURE_FLAG_EXTRA_TRIGGER))
        return cinfo->GetRandomValidModelId();

    // Triggers by default receive the invisible model
    return cinfo->GetFirstInvisibleModel();
}

void ObjectMgr::ChooseCreatureFlags(CreatureTemplate const* cinfo, uint32* npcflag, uint32* unit_flags, uint32* dynamicflags, CreatureData const* data /*= nullptr*/)
{
#define ChooseCreatureFlagSource(field) ((data && data->field) ? data->field : cinfo->field)

    if (npcflag)
        *npcflag = ChooseCreatureFlagSource(npcflag);

    if (unit_flags)
        *unit_flags = ChooseCreatureFlagSource(unit_flags);

    if (dynamicflags)
        *dynamicflags = ChooseCreatureFlagSource(dynamicflags);

#undef ChooseCreatureFlagSource
}

CreatureModelInfo const* ObjectMgr::GetCreatureModelRandomGender(uint32* displayID) const
{
    CreatureModelInfo const* modelInfo = GetCreatureModelInfo(*displayID);
    if (!modelInfo)
        return nullptr;

    // If a model for another gender exists, 50% chance to use it
    if (modelInfo->modelid_other_gender != 0 && urand(0, 1) == 0)
    {
        CreatureModelInfo const* minfo_tmp = GetCreatureModelInfo(modelInfo->modelid_other_gender);
        if (!minfo_tmp)
            TC_LOG_ERROR("sql.sql", "Model (Entry: {}) has modelid_other_gender {} not found in table `creature_model_info`. ", *displayID, modelInfo->modelid_other_gender);
        else
        {
            // Model ID changed
            *displayID = modelInfo->modelid_other_gender;
            return minfo_tmp;
        }
    }

    return modelInfo;
}

void ObjectMgr::LoadCreatureModelInfo()
{
    uint32 oldMSTime = getMSTime();
    //                                                   0             1             2          3               4
    QueryResult result = WorldDatabase.Query("SELECT DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender FROM creature_model_info");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature model definitions. DB table `creature_model_info` is empty.");
        return;
    }

    _creatureModelStore.rehash(result->GetRowCount());
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 modelId = fields[0].GetUInt32();
        CreatureDisplayInfoEntry const* creatureDisplay = sCreatureDisplayInfoStore.LookupEntry(modelId);
        if (!creatureDisplay)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_model_info` has model for nonexistent display id ({}).", modelId);
            continue;
        }

        CreatureModelInfo& modelInfo = _creatureModelStore[modelId];

        modelInfo.bounding_radius      = fields[1].GetFloat();
        modelInfo.combat_reach         = fields[2].GetFloat();
        modelInfo.gender               = fields[3].GetUInt8();
        modelInfo.modelid_other_gender = fields[4].GetUInt32();
        modelInfo.is_trigger           = false;

        // Checks

        if (modelInfo.gender > GENDER_NONE)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_model_info` has wrong gender ({}) for display id ({}).", uint32(modelInfo.gender), modelId);
            modelInfo.gender = GENDER_MALE;
        }

        if (modelInfo.modelid_other_gender && !sCreatureDisplayInfoStore.LookupEntry(modelInfo.modelid_other_gender))
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_model_info` has nonexistent alt.gender model ({}) for existed display id ({}).", modelInfo.modelid_other_gender, modelId);
            modelInfo.modelid_other_gender = 0;
        }

        if (modelInfo.combat_reach < 0.1f)
            modelInfo.combat_reach = DEFAULT_PLAYER_COMBAT_REACH;

        if (CreatureModelDataEntry const* modelData = sCreatureModelDataStore.LookupEntry(creatureDisplay->ModelID))
            modelInfo.is_trigger = strstr(modelData->ModelName, "InvisibleStalker") != nullptr;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature model based info in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadPlayerTotemModels()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT TotemSlot, RaceId, DisplayId from player_totem_model");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 player totem model records. DB table `player_totem_model` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        SummonSlot totemSlot = SummonSlot(fields[0].GetUInt8());
        uint8 race = fields[1].GetUInt8();
        uint32 displayId = fields[2].GetUInt32();

        if (totemSlot < SUMMON_SLOT_TOTEM_FIRE || totemSlot >= MAX_TOTEM_SLOT)
        {
            TC_LOG_ERROR("sql.sql", "Wrong TotemSlot {} in `player_totem_model` table, skipped.", totemSlot);
            continue;
        }

        ChrRacesEntry const* raceEntry = sChrRacesStore.LookupEntry(race);
        if (!raceEntry)
        {
            TC_LOG_ERROR("sql.sql", "Race {} defined in `player_totem_model` does not exists, skipped.", uint32(race));
            continue;
        }

        CreatureDisplayInfoEntry const* displayEntry = sCreatureDisplayInfoStore.LookupEntry(displayId);
        if (!displayEntry)
        {
            TC_LOG_ERROR("sql.sql", "TotemSlot: {} defined in `player_totem_model` has non-existing model ({}), skipped.", totemSlot, displayId);
            continue;
        }

        _playerTotemModel[std::make_pair(totemSlot, Races(race))] = displayId;
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} player totem model records in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

uint32 ObjectMgr::GetModelForTotem(SummonSlot totemSlot, Races race) const
{
    auto itr = _playerTotemModel.find(std::make_pair(totemSlot, race));
    if (itr != _playerTotemModel.end())
        return itr->second;
    return 0;
}

void ObjectMgr::LoadLinkedRespawn()
{
    uint32 oldMSTime = getMSTime();

    _linkedRespawnStore.clear();
    //                                                 0        1          2
    QueryResult result = WorldDatabase.Query("SELECT guid, linkedGuid, linkType FROM linked_respawn ORDER BY guid ASC");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 linked respawns. DB table `linked_respawn` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        ObjectGuid::LowType guidLow = fields[0].GetUInt32();
        ObjectGuid::LowType linkedGuidLow = fields[1].GetUInt32();
        uint8 linkType = fields[2].GetUInt8();

        ObjectGuid guid, linkedGuid;
        bool error = false;
        switch (linkType)
        {
            case LINKED_RESPAWN_CREATURE_TO_CREATURE:
            {
                CreatureData const* slave = GetCreatureData(guidLow);
                if (!slave)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature (guid) '{}' not found in creature table", guidLow);
                    error = true;
                    break;
                }

                CreatureData const* master = GetCreatureData(linkedGuidLow);
                if (!master)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature (linkedGuid) '{}' not found in creature table", linkedGuidLow);
                    error = true;
                    break;
                }

                MapEntry const* const map = sMapStore.LookupEntry(master->mapId);
                if (!map || !map->Instanceable() || (master->mapId != slave->mapId))
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature '{}' linking to Creature '{}' on an unpermitted map.", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                if (!(master->spawnMask & slave->spawnMask))  // they must have a possibility to meet (normal/heroic difficulty)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature '{}' linking to Creature '{}' with not corresponding spawnMask", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                guid = ObjectGuid(HighGuid::Unit, slave->id, guidLow);
                linkedGuid = ObjectGuid(HighGuid::Unit, master->id, linkedGuidLow);
                break;
            }
            case LINKED_RESPAWN_CREATURE_TO_GO:
            {
                CreatureData const* slave = GetCreatureData(guidLow);
                if (!slave)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature (guid) '{}' not found in creature table", guidLow);
                    error = true;
                    break;
                }

                GameObjectData const* master = GetGameObjectData(linkedGuidLow);
                if (!master)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject (linkedGuid) '{}' not found in gameobject table", linkedGuidLow);
                    error = true;
                    break;
                }

                MapEntry const* const map = sMapStore.LookupEntry(master->mapId);
                if (!map || !map->Instanceable() || (master->mapId != slave->mapId))
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature '{}' linking to Gameobject '{}' on an unpermitted map.", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                if (!(master->spawnMask & slave->spawnMask))  // they must have a possibility to meet (normal/heroic difficulty)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature '{}' linking to Gameobject '{}' with not corresponding spawnMask", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                guid = ObjectGuid(HighGuid::Unit, slave->id, guidLow);
                linkedGuid = ObjectGuid(HighGuid::GameObject, master->id, linkedGuidLow);
                break;
            }
            case LINKED_RESPAWN_GO_TO_GO:
            {
                GameObjectData const* slave = GetGameObjectData(guidLow);
                if (!slave)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject (guid) '{}' not found in gameobject table", guidLow);
                    error = true;
                    break;
                }

                GameObjectData const* master = GetGameObjectData(linkedGuidLow);
                if (!master)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject (linkedGuid) '{}' not found in gameobject table", linkedGuidLow);
                    error = true;
                    break;
                }

                MapEntry const* const map = sMapStore.LookupEntry(master->mapId);
                if (!map || !map->Instanceable() || (master->mapId != slave->mapId))
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject '{}' linking to Gameobject '{}' on an unpermitted map.", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                if (!(master->spawnMask & slave->spawnMask))  // they must have a possibility to meet (normal/heroic difficulty)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject '{}' linking to Gameobject '{}' with not corresponding spawnMask", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                guid = ObjectGuid(HighGuid::GameObject, slave->id, guidLow);
                linkedGuid = ObjectGuid(HighGuid::GameObject, master->id, linkedGuidLow);
                break;
            }
            case LINKED_RESPAWN_GO_TO_CREATURE:
            {
                GameObjectData const* slave = GetGameObjectData(guidLow);
                if (!slave)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject (guid) '{}' not found in gameobject table", guidLow);
                    error = true;
                    break;
                }

                CreatureData const* master = GetCreatureData(linkedGuidLow);
                if (!master)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature (linkedGuid) '{}' not found in creature table", linkedGuidLow);
                    error = true;
                    break;
                }

                MapEntry const* const map = sMapStore.LookupEntry(master->mapId);
                if (!map || !map->Instanceable() || (master->mapId != slave->mapId))
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject '{}' linking to Creature '{}' on an unpermitted map.", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                if (!(master->spawnMask & slave->spawnMask))  // they must have a possibility to meet (normal/heroic difficulty)
                {
                    TC_LOG_ERROR("sql.sql", "LinkedRespawn: Gameobject '{}' linking to Creature '{}' with not corresponding spawnMask", guidLow, linkedGuidLow);
                    error = true;
                    break;
                }

                guid = ObjectGuid(HighGuid::GameObject, slave->id, guidLow);
                linkedGuid = ObjectGuid(HighGuid::Unit, master->id, linkedGuidLow);
                break;
            }
        }

        if (!error)
            _linkedRespawnStore[guid] = linkedGuid;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} linked respawns in {} ms", uint64(_linkedRespawnStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

bool ObjectMgr::SetCreatureLinkedRespawn(ObjectGuid::LowType guidLow, ObjectGuid::LowType linkedGuidLow)
{
    if (!guidLow)
        return false;

    CreatureData const* master = GetCreatureData(guidLow);
    ASSERT(master);
    ObjectGuid guid(HighGuid::Unit, master->id, guidLow);

    if (!linkedGuidLow) // we're removing the linking
    {
        _linkedRespawnStore.erase(guid);
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_LINKED_RESPAWN);
        stmt->setUInt32(0, guidLow);
        stmt->setUInt32(1, LINKED_RESPAWN_CREATURE_TO_CREATURE);
        WorldDatabase.Execute(stmt);
        return true;
    }

    CreatureData const* slave = GetCreatureData(linkedGuidLow);
    if (!slave)
    {
        TC_LOG_ERROR("sql.sql", "Creature '{}' linking to non-existent creature '{}'.", guidLow, linkedGuidLow);
        return false;
    }

    MapEntry const* map = sMapStore.LookupEntry(master->mapId);
    if (!map || !map->Instanceable() || (master->mapId != slave->mapId))
    {
        TC_LOG_ERROR("sql.sql", "Creature '{}' linking to '{}' on an unpermitted map.", guidLow, linkedGuidLow);
        return false;
    }

    if (!(master->spawnMask & slave->spawnMask))  // they must have a possibility to meet (normal/heroic difficulty)
    {
        TC_LOG_ERROR("sql.sql", "LinkedRespawn: Creature '{}' linking to '{}' with not corresponding spawnMask", guidLow, linkedGuidLow);
        return false;
    }

    ObjectGuid linkedGuid(HighGuid::Unit, slave->id, linkedGuidLow);

    _linkedRespawnStore[guid] = linkedGuid;
    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_REP_LINKED_RESPAWN);
    stmt->setUInt32(0, guidLow);
    stmt->setUInt32(1, linkedGuidLow);
    stmt->setUInt32(2, LINKED_RESPAWN_CREATURE_TO_CREATURE);
    WorldDatabase.Execute(stmt);
    return true;
}

void ObjectMgr::LoadTempSummons()
{
    uint32 oldMSTime = getMSTime();

    _tempSummonDataStore.clear();   // needed for reload case

    //                                               0           1             2        3      4           5           6           7            8           9
    QueryResult result = WorldDatabase.Query("SELECT summonerId, summonerType, groupId, entry, position_x, position_y, position_z, orientation, summonType, summonTime FROM creature_summon_groups");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 temp summons. DB table `creature_summon_groups` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 summonerId               = fields[0].GetUInt32();
        SummonerType summonerType       = SummonerType(fields[1].GetUInt8());
        uint8 group                     = fields[2].GetUInt8();

        switch (summonerType)
        {
            case SUMMONER_TYPE_CREATURE:
                if (!GetCreatureTemplate(summonerId))
                {
                    TC_LOG_ERROR("sql.sql", "Table `creature_summon_groups` has summoner with non existing entry {} for creature summoner type, skipped.", summonerId);
                    continue;
                }
                break;
            case SUMMONER_TYPE_GAMEOBJECT:
                if (!GetGameObjectTemplate(summonerId))
                {
                    TC_LOG_ERROR("sql.sql", "Table `creature_summon_groups` has summoner with non existing entry {} for gameobject summoner type, skipped.", summonerId);
                    continue;
                }
                break;
            case SUMMONER_TYPE_MAP:
                if (!sMapStore.LookupEntry(summonerId))
                {
                    TC_LOG_ERROR("sql.sql", "Table `creature_summon_groups` has summoner with non existing entry {} for map summoner type, skipped.", summonerId);
                    continue;
                }
                break;
            default:
                TC_LOG_ERROR("sql.sql", "Table `creature_summon_groups` has unhandled summoner type {} for summoner {}, skipped.", summonerType, summonerId);
                continue;
        }

        TempSummonData data;
        data.entry                      = fields[3].GetUInt32();

        if (!GetCreatureTemplate(data.entry))
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_summon_groups` has creature in group [Summoner ID: {}, Summoner Type: {}, Group ID: {}] with non existing creature entry {}, skipped.", summonerId, summonerType, group, data.entry);
            continue;
        }

        float posX                      = fields[4].GetFloat();
        float posY                      = fields[5].GetFloat();
        float posZ                      = fields[6].GetFloat();
        float orientation               = fields[7].GetFloat();

        data.pos.Relocate(posX, posY, posZ, orientation);

        data.type                       = TempSummonType(fields[8].GetUInt8());

        if (data.type > TEMPSUMMON_MANUAL_DESPAWN)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_summon_groups` has unhandled temp summon type {} in group [Summoner ID: {}, Summoner Type: {}, Group ID: {}] for creature entry {}, skipped.", data.type, summonerId, summonerType, group, data.entry);
            continue;
        }

        data.time                       = fields[9].GetUInt32();

        TempSummonGroupKey key(summonerId, summonerType, group);
        _tempSummonDataStore[key].push_back(data);

        ++count;

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} temp summons in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatures()
{
    uint32 oldMSTime = getMSTime();

    //                                               0              1   2    3           4           5           6            7        8             9              10
    QueryResult result = WorldDatabase.Query("SELECT creature.guid, id, map, position_x, position_y, position_z, orientation, modelid, equipment_id, spawntimesecs, wander_distance, "
    //   11               12         13       14            15         16          17          18                19                   20                    21
        "currentwaypoint, curhealth, curmana, MovementType, spawnMask, phaseMask, eventEntry, poolSpawnId, creature.npcflag, creature.unit_flags, creature.dynamicflags, "
    //   22                   23
        "creature.ScriptName, creature.StringId "
        "FROM creature "
        "LEFT OUTER JOIN game_event_creature ON creature.guid = game_event_creature.guid "
        "LEFT OUTER JOIN pool_members ON pool_members.type = 0 AND creature.guid = pool_members.spawnId");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creatures. DB table `creature` is empty.");
        return;
    }

    // Build single time for check spawnmask
    std::map<uint32, uint32> spawnMasks;
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
        if (sMapStore.LookupEntry(i))
            for (uint8 k = 0; k < MAX_DIFFICULTY; ++k)
                if (GetMapDifficultyData(i, Difficulty(k)))
                    spawnMasks[i] |= (1 << k);

    _creatureDataStore.rehash(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();

        ObjectGuid::LowType guid = fields[0].GetUInt32();
        uint32 entry        = fields[1].GetUInt32();

        CreatureTemplate const* cInfo = GetCreatureTemplate(entry);
        if (!cInfo)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {}) with non existing creature entry {}, skipped.", guid, entry);
            continue;
        }

        CreatureData& data = _creatureDataStore[guid];
        data.spawnId        = guid;
        data.id             = entry;
        data.mapId          = fields[2].GetUInt16();
        data.spawnPoint.Relocate(fields[3].GetFloat(), fields[4].GetFloat(), fields[5].GetFloat(), fields[6].GetFloat());
        data.displayid      = fields[7].GetUInt32();
        data.equipmentId    = fields[8].GetInt8();
        data.spawntimesecs  = fields[9].GetUInt32();
        data.wander_distance      = fields[10].GetFloat();
        data.currentwaypoint= fields[11].GetUInt32();
        data.curhealth      = fields[12].GetUInt32();
        data.curmana        = fields[13].GetUInt32();
        data.movementType   = fields[14].GetUInt8();
        data.spawnMask      = fields[15].GetUInt8();
        data.phaseMask      = fields[16].GetUInt32();
        int16 gameEvent     = fields[17].GetInt8();
        uint32 PoolId       = fields[18].GetUInt32();
        data.npcflag        = fields[19].GetUInt32();
        data.unit_flags     = fields[20].GetUInt32();
        data.dynamicflags   = fields[21].GetUInt32();
        data.scriptId       = GetScriptId(fields[22].GetString());
        data.StringId       = fields[23].GetString();
        data.spawnGroupData = GetDefaultSpawnGroup();

        MapEntry const* mapEntry = sMapStore.LookupEntry(data.mapId);
        if (!mapEntry)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {}) that spawned at nonexistent map (Id: {}), skipped.", guid, data.mapId);
            continue;
        }

        // Skip spawnMask check for transport maps
        if (!IsTransportMap(data.mapId))
        {
            if (data.spawnMask & ~spawnMasks[data.mapId])
                TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {}) that have wrong spawn mask {} including unsupported difficulty modes for map (Id: {}).", guid, data.spawnMask, data.mapId);
        }
        else
            data.spawnGroupData = GetLegacySpawnGroup(); // force compatibility group for transport spawns

        bool ok = true;
        for (uint32 diff = 0; diff < MAX_DIFFICULTY - 1 && ok; ++diff)
        {
            if (_difficultyEntries[diff].find(data.id) != _difficultyEntries[diff].end())
            {
                TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {}) that is listed as difficulty {} template (entry: {}) in `creature_template`, skipped.",
                    guid, diff + 1, data.id);
                ok = false;
            }
        }
        if (!ok)
            continue;

        // -1 random, 0 no equipment
        if (data.equipmentId != 0)
        {
            if (!GetEquipmentInfo(data.id, data.equipmentId))
            {
                TC_LOG_ERROR("sql.sql", "Table `creature` has creature (Entry: {}) with equipment_id {} not found in table `creature_equip_template`, set to no equipment.", data.id, data.equipmentId);
                data.equipmentId = 0;
            }
        }

        if (cInfo->flags_extra & CREATURE_FLAG_EXTRA_INSTANCE_BIND)
        {
            if (!mapEntry->IsDungeon())
                TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with `creature_template`.`flags_extra` including CREATURE_FLAG_EXTRA_INSTANCE_BIND but creature is not in instance.", guid, data.id);
        }

        if (data.movementType >= MAX_DB_MOTION_TYPE)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with wrong movement generator type ({}), ignored and set to IDLE.", guid, data.id, data.movementType);
            data.movementType = IDLE_MOTION_TYPE;
        }

        if (data.wander_distance < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with `wander_distance`< 0, set to 0.", guid, data.id);
            data.wander_distance = 0.0f;
        }
        else if (data.movementType == RANDOM_MOTION_TYPE)
        {
            if (data.wander_distance == 0.0f)
            {
                TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with `MovementType`=1 (random movement) but with `wander_distance`=0, replace by idle movement type (0).", guid, data.id);
                data.movementType = IDLE_MOTION_TYPE;
            }
        }
        else if (data.movementType == IDLE_MOTION_TYPE)
        {
            if (data.wander_distance != 0.0f)
            {
                TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with `MovementType`=0 (idle) have `wander_distance`<>0, set to 0.", guid, data.id);
                data.wander_distance = 0.0f;
            }
        }

        if (data.phaseMask == 0)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with `phaseMask`=0 (not visible for anyone), set to 1.", guid, data.id);
            data.phaseMask = 1;
        }

        if (uint32 disallowedUnitFlags = (data.unit_flags & ~UNIT_FLAG_ALLOWED))
        {
            TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with disallowed `unit_flags` {}, removing incorrect flag.", guid, data.id, disallowedUnitFlags);
            data.unit_flags &= UNIT_FLAG_ALLOWED;
        }

        if (data.dynamicflags)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature` has creature (GUID: {} Entry: {}) with `dynamicflags` > 0. Ignored and set to 0.", guid, data.id);
            data.dynamicflags = 0;
        }

        if (sWorld->getBoolConfig(CONFIG_CALCULATE_CREATURE_ZONE_AREA_DATA))
        {
            uint32 zoneId = 0;
            uint32 areaId = 0;
            sMapMgr->GetZoneAndAreaId(data.phaseMask, zoneId, areaId, data.mapId, data.spawnPoint);

            WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_CREATURE_ZONE_AREA_DATA);

            stmt->setUInt32(0, zoneId);
            stmt->setUInt32(1, areaId);
            stmt->setUInt64(2, guid);

            WorldDatabase.Execute(stmt);
        }

        // Add to grid if not managed by the game event or pool system
        if (gameEvent == 0 && PoolId == 0)
            AddCreatureToGrid(guid, &data);
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creatures in {} ms", _creatureDataStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

CellObjectGuids const* ObjectMgr::GetCellObjectGuids(uint16 mapid, uint8 spawnMode, uint32 cell_id)
{
    if (CellObjectGuidsMap const* mapGuids = Trinity::Containers::MapGetValuePtr(_mapObjectGuidsStore, MAKE_PAIR32(mapid, spawnMode)))
        return Trinity::Containers::MapGetValuePtr(*mapGuids, cell_id);

    return nullptr;
}

CellObjectGuidsMap const* ObjectMgr::GetMapObjectGuids(uint16 mapid, uint8 spawnMode)
{
    return Trinity::Containers::MapGetValuePtr(_mapObjectGuidsStore, MAKE_PAIR32(mapid, spawnMode));
}

void ObjectMgr::AddCreatureToGrid(ObjectGuid::LowType guid, CreatureData const* data)
{
    uint8 mask = data->spawnMask;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY());
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapId, i)][cellCoord.GetId()];
            cell_guids.creatures.insert(guid);
        }
    }
}

void ObjectMgr::RemoveCreatureFromGrid(ObjectGuid::LowType guid, CreatureData const* data)
{
    uint8 mask = data->spawnMask;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY());
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapId, i)][cellCoord.GetId()];
            cell_guids.creatures.erase(guid);
        }
    }
}

ObjectGuid::LowType ObjectMgr::AddGameObjectData(uint32 entry, uint32 mapId, Position const& pos, QuaternionData const& rot, uint32 spawntimedelay /*= 0*/)
{
    GameObjectTemplate const* goinfo = GetGameObjectTemplate(entry);
    if (!goinfo)
        return 0;

    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map)
        return 0;

    ObjectGuid::LowType spawnId = GenerateGameObjectSpawnId();

    GameObjectData& data = NewOrExistGameObjectData(spawnId);
    data.spawnId        = spawnId;
    data.id             = entry;
    data.mapId          = mapId;
    data.spawnPoint.Relocate(pos);
    data.rotation       = rot;
    data.spawntimesecs  = spawntimedelay;
    data.animprogress   = 100;
    data.spawnMask      = 1;
    data.goState        = GO_STATE_READY;
    data.phaseMask      = PHASEMASK_NORMAL;
    data.artKit         = goinfo->type == GAMEOBJECT_TYPE_CAPTURE_POINT ? 21 : 0;
    data.dbData         = false;
    data.spawnGroupData = GetLegacySpawnGroup();

    AddGameobjectToGrid(spawnId, &data);

    // Spawn if necessary (loaded grids only)
    // We use spawn coords to spawn
    if (!map->Instanceable() && map->IsGridLoaded(data.spawnPoint))
    {
        GameObject* go = new GameObject;
        if (!go->LoadFromDB(spawnId, map, true))
        {
            TC_LOG_ERROR("misc", "AddGameObjectData: cannot add gameobject entry {} to map", entry);
            delete go;
            return 0;
        }
    }

    TC_LOG_DEBUG("maps", "AddGameObjectData: dbguid {} entry {} map {} pos {}", spawnId, entry, mapId, data.spawnPoint.ToString());

    return spawnId;
}

ObjectGuid::LowType ObjectMgr::AddCreatureData(uint32 entry, uint32 mapId, Position const& pos, uint32 spawntimedelay /*= 0*/)
{
    CreatureTemplate const* cInfo = GetCreatureTemplate(entry);
    if (!cInfo)
        return 0;

    uint32 level = cInfo->minlevel == cInfo->maxlevel ? cInfo->minlevel : urand(cInfo->minlevel, cInfo->maxlevel); // Only used for extracting creature base stats
    CreatureBaseStats const* stats = GetCreatureBaseStats(level, cInfo->unit_class);
    Map* map = sMapMgr->CreateBaseMap(mapId);
    if (!map)
        return 0;

    ObjectGuid::LowType spawnId = GenerateCreatureSpawnId();
    CreatureData& data = NewOrExistCreatureData(spawnId);
    data.spawnId = spawnId;
    data.id = entry;
    data.mapId = mapId;
    data.spawnPoint.Relocate(pos);
    data.displayid = 0;
    data.equipmentId = 0;
    data.spawntimesecs = spawntimedelay;
    data.wander_distance = 0;
    data.currentwaypoint = 0;
    data.curhealth = stats->GenerateHealth(cInfo);
    data.curmana = stats->GenerateMana(cInfo);
    data.movementType = cInfo->MovementType;
    data.spawnMask = 1;
    data.phaseMask = PHASEMASK_NORMAL;
    data.dbData = false;
    data.npcflag = cInfo->npcflag;
    data.unit_flags = cInfo->unit_flags;
    data.dynamicflags = cInfo->dynamicflags;
    data.spawnGroupData = GetLegacySpawnGroup();

    AddCreatureToGrid(spawnId, &data);

    // We use spawn coords to spawn
    if (!map->Instanceable() && !map->IsRemovalGrid(data.spawnPoint))
    {
        Creature* creature = new Creature();
        if (!creature->LoadFromDB(spawnId, map, true, true))
        {
            TC_LOG_ERROR("misc", "AddCreature: Cannot add creature entry {} to map", entry);
            delete creature;
            return 0;
        }
    }

    return spawnId;
}

void ObjectMgr::LoadGameObjects()
{
    uint32 oldMSTime = getMSTime();

    //                                                0                1   2    3           4           5           6
    QueryResult result = WorldDatabase.Query("SELECT gameobject.guid, id, map, position_x, position_y, position_z, orientation, "
    //   7          8          9          10         11             12            13     14         15         16          17
        "rotation0, rotation1, rotation2, rotation3, spawntimesecs, animprogress, state, spawnMask, phaseMask, eventEntry, poolSpawnId, "
    //   18          19
        "ScriptName, StringId "
        "FROM gameobject LEFT OUTER JOIN game_event_gameobject ON gameobject.guid = game_event_gameobject.guid "
        "LEFT OUTER JOIN pool_members ON pool_members.type = 1 AND gameobject.guid = pool_members.spawnId");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gameobjects. DB table `gameobject` is empty.");
        return;
    }

    // build single time for check spawnmask
    std::map<uint32, uint32> spawnMasks;
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
        if (sMapStore.LookupEntry(i))
            for (uint8 k = 0; k < MAX_DIFFICULTY; ++k)
                if (GetMapDifficultyData(i, Difficulty(k)))
                    spawnMasks[i] |= (1 << k);

    _gameObjectDataStore.rehash(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();

        ObjectGuid::LowType guid = fields[0].GetUInt32();
        uint32 entry        = fields[1].GetUInt32();

        GameObjectTemplate const* gInfo = GetGameObjectTemplate(entry);
        if (!gInfo)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {}) with non existing gameobject entry {}, skipped.", guid, entry);
            continue;
        }

        if (!gInfo->displayId)
        {
            switch (gInfo->type)
            {
                case GAMEOBJECT_TYPE_TRAP:
                case GAMEOBJECT_TYPE_SPELL_FOCUS:
                    break;
                default:
                    TC_LOG_ERROR("sql.sql", "Gameobject (GUID: {} Entry {} GoType: {}) doesn't have a displayId ({}), not loaded.", guid, entry, gInfo->type, gInfo->displayId);
                    break;
            }
        }

        if (gInfo->displayId && !sGameObjectDisplayInfoStore.LookupEntry(gInfo->displayId))
        {
            TC_LOG_ERROR("sql.sql", "Gameobject (GUID: {} Entry {} GoType: {}) has an invalid displayId ({}), not loaded.", guid, entry, gInfo->type, gInfo->displayId);
            continue;
        }

        GameObjectData& data = _gameObjectDataStore[guid];

        data.spawnId        = guid;
        data.id             = entry;
        data.mapId          = fields[2].GetUInt16();
        data.spawnPoint.Relocate(fields[3].GetFloat(), fields[4].GetFloat(), fields[5].GetFloat(), fields[6].GetFloat());
        data.rotation.x     = fields[7].GetFloat();
        data.rotation.y     = fields[8].GetFloat();
        data.rotation.z     = fields[9].GetFloat();
        data.rotation.w     = fields[10].GetFloat();
        data.spawntimesecs  = fields[11].GetInt32();
        data.spawnGroupData = GetDefaultSpawnGroup();

        MapEntry const* mapEntry = sMapStore.LookupEntry(data.mapId);
        if (!mapEntry)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) spawned on a non-existed map (Id: {}), skip", guid, data.id, data.mapId);
            continue;
        }

        if (data.spawntimesecs == 0 && gInfo->IsDespawnAtAction())
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with `spawntimesecs` (0) value, but the gameobejct is marked as despawnable at action.", guid, data.id);
        }

        data.animprogress   = fields[12].GetUInt8();
        data.artKit         = 0;

        uint32 go_state     = fields[13].GetUInt8();
        if (go_state >= MAX_GO_STATE)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with invalid `state` ({}) value, skip", guid, data.id, go_state);
            continue;
        }
        data.goState       = GOState(go_state);

        data.spawnMask      = fields[14].GetUInt8();

        if (!IsTransportMap(data.mapId))
        {
            if (data.spawnMask & ~spawnMasks[data.mapId])
                TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) that has wrong spawn mask {} including unsupported difficulty modes for map (Id: {}), skip", guid, data.id, data.spawnMask, data.mapId);
        }
        else
            data.spawnGroupData = GetLegacySpawnGroup(); // force compatibility group for transport spawns

        data.phaseMask      = fields[15].GetUInt32();
        int16 gameEvent     = fields[16].GetInt8();
        uint32 PoolId        = fields[17].GetUInt32();

        data.scriptId = GetScriptId(fields[18].GetString());
        data.StringId = fields[19].GetString();

        if (data.rotation.x < -1.0f || data.rotation.x > 1.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with invalid rotationX ({}) value, skip", guid, data.id, data.rotation.x);
            continue;
        }

        if (data.rotation.y < -1.0f || data.rotation.y > 1.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with invalid rotationY ({}) value, skip", guid, data.id, data.rotation.y);
            continue;
        }

        if (data.rotation.z < -1.0f || data.rotation.z > 1.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with invalid rotationZ ({}) value, skip", guid, data.id, data.rotation.z);
            continue;
        }

        if (data.rotation.w < -1.0f || data.rotation.w > 1.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with invalid rotationW ({}) value, skip", guid, data.id, data.rotation.w);
            continue;
        }

        if (!MapManager::IsValidMapCoord(data.mapId, data.spawnPoint))
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with invalid coordinates, skip", guid, data.id);
            continue;
        }

        if (!data.rotation.isUnit())
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with invalid rotation quaternion (non-unit), defaulting to orientation on Z axis only", guid, data.id);
            data.rotation = QuaternionData::fromEulerAnglesZYX(data.spawnPoint.GetOrientation(), 0.0f, 0.0f);
        }

        if (data.phaseMask == 0)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject` has gameobject (GUID: {} Entry: {}) with `phaseMask`=0 (not visible for anyone), set to 1.", guid, data.id);
            data.phaseMask = 1;
        }

        if (sWorld->getBoolConfig(CONFIG_CALCULATE_GAMEOBJECT_ZONE_AREA_DATA))
        {
            uint32 zoneId = 0;
            uint32 areaId = 0;
            sMapMgr->GetZoneAndAreaId(data.phaseMask, zoneId, areaId, data.mapId, data.spawnPoint);

            WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_UPD_GAMEOBJECT_ZONE_AREA_DATA);

            stmt->setUInt32(0, zoneId);
            stmt->setUInt32(1, areaId);
            stmt->setUInt64(2, guid);

            WorldDatabase.Execute(stmt);
        }

        if (gameEvent == 0 && PoolId == 0)                      // if not this is to be managed by GameEvent System or Pool system
            AddGameobjectToGrid(guid, &data);
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gameobjects in {} ms", _gameObjectDataStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadSpawnGroupTemplates()
{
    uint32 oldMSTime = getMSTime();

    //                                               0        1          2
    QueryResult result = WorldDatabase.Query("SELECT groupId, groupName, groupFlags FROM spawn_group_template");

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 groupId = fields[0].GetUInt32();
            SpawnGroupTemplateData& group = _spawnGroupDataStore[groupId];
            group.groupId = groupId;
            group.name = fields[1].GetString();
            group.mapId = SPAWNGROUP_MAP_UNSET;
            uint32 flags = fields[2].GetUInt32();
            if (flags & ~SPAWNGROUP_FLAGS_ALL)
            {
                flags &= SPAWNGROUP_FLAGS_ALL;
                TC_LOG_ERROR("sql.sql", "Invalid spawn group flag {} on group ID {} ({}), reduced to valid flag {}.", flags, groupId, group.name, uint32(group.flags));
            }
            if (flags & SPAWNGROUP_FLAG_SYSTEM && flags & SPAWNGROUP_FLAG_MANUAL_SPAWN)
            {
                flags &= ~SPAWNGROUP_FLAG_MANUAL_SPAWN;
                TC_LOG_ERROR("sql.sql", "System spawn group {} ({}) has invalid manual spawn flag. Ignored.", groupId, group.name);
            }
            group.flags = SpawnGroupFlags(flags);
        } while (result->NextRow());
    }

    if (_spawnGroupDataStore.find(0) == _spawnGroupDataStore.end())
    {
        TC_LOG_ERROR("sql.sql", "Default spawn group (index 0) is missing from DB! Manually inserted.");
        SpawnGroupTemplateData& data = _spawnGroupDataStore[0];
        data.groupId = 0;
        data.name = "Default Group";
        data.mapId = 0;
        data.flags = SPAWNGROUP_FLAG_SYSTEM;
    }
    if (_spawnGroupDataStore.find(1) == _spawnGroupDataStore.end())
    {
        TC_LOG_ERROR("sql.sql", "Default legacy spawn group (index 1) is missing from DB! Manually inserted.");
        SpawnGroupTemplateData&data = _spawnGroupDataStore[1];
        data.groupId = 1;
        data.name = "Legacy Group";
        data.mapId = 0;
        data.flags = SpawnGroupFlags(SPAWNGROUP_FLAG_SYSTEM | SPAWNGROUP_FLAG_COMPATIBILITY_MODE);
    }

    if (result)
        TC_LOG_INFO("server.loading", ">> Loaded {} spawn group templates in {} ms", _spawnGroupDataStore.size(), GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 spawn group templates. DB table `spawn_group_template` is empty.");

    return;
}

void ObjectMgr::LoadSpawnGroups()
{
    uint32 oldMSTime = getMSTime();

    //                                               0        1          2
    QueryResult result = WorldDatabase.Query("SELECT groupId, spawnType, spawnId FROM spawn_group");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spawn group members. DB table `spawn_group` is empty.");
        return;
    }

    uint32 numMembers = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 groupId = fields[0].GetUInt32();
        SpawnObjectType spawnType = SpawnObjectType(fields[1].GetUInt8());
        if (!SpawnData::TypeIsValid(spawnType))
        {
            TC_LOG_ERROR("sql.sql", "Spawn data with invalid type {} listed for spawn group {}. Skipped.", uint32(spawnType), groupId);
            continue;
        }
        ObjectGuid::LowType spawnId = fields[2].GetUInt32();

        SpawnMetadata const* data = GetSpawnMetadata(spawnType, spawnId);
        if (!data)
        {
            TC_LOG_ERROR("sql.sql", "Spawn data with ID ({},{}) not found, but is listed as a member of spawn group {}!", uint32(spawnType), spawnId, groupId);
            continue;
        }
        else if (data->spawnGroupData->groupId)
        {
            TC_LOG_ERROR("sql.sql", "Spawn with ID ({},{}) is listed as a member of spawn group {}, but is already a member of spawn group {}. Skipping.", uint32(spawnType), spawnId, groupId, data->spawnGroupData->groupId);
            continue;
        }
        auto it = _spawnGroupDataStore.find(groupId);
        if (it == _spawnGroupDataStore.end())
        {
            TC_LOG_ERROR("sql.sql", "Spawn group {} assigned to spawn ID ({},{}), but group does not exist!", groupId, uint32(spawnType), spawnId);
            continue;
        }
        else
        {
            SpawnGroupTemplateData& groupTemplate = it->second;
            if (groupTemplate.mapId == SPAWNGROUP_MAP_UNSET)
                groupTemplate.mapId = data->mapId;
            else if (groupTemplate.mapId != data->mapId && !(groupTemplate.flags & SPAWNGROUP_FLAG_SYSTEM))
            {
                TC_LOG_ERROR("sql.sql", "Spawn group {} has map ID {}, but spawn ({},{}) has map id {} - spawn NOT added to group!", groupId, groupTemplate.mapId, uint32(spawnType), spawnId, data->mapId);
                continue;
            }
            const_cast<SpawnMetadata*>(data)->spawnGroupData = &groupTemplate;
            if (!(groupTemplate.flags & SPAWNGROUP_FLAG_SYSTEM))
                _spawnGroupMapStore.emplace(groupId, data);
            ++numMembers;
        }
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spawn group members in {} ms", numMembers, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadInstanceSpawnGroups()
{
    uint32 oldMSTime = getMSTime();

    //                                               0              1            2           3             4
    QueryResult result = WorldDatabase.Query("SELECT instanceMapId, bossStateId, bossStates, spawnGroupId, flags FROM instance_spawn_groups");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 instance spawn groups. DB table `instance_spawn_groups` is empty.");
        return;
    }

    uint32 n = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 const spawnGroupId = fields[3].GetUInt32();
        auto it = _spawnGroupDataStore.find(spawnGroupId);
        if (it == _spawnGroupDataStore.end() || (it->second.flags & SPAWNGROUP_FLAG_SYSTEM))
        {
            TC_LOG_ERROR("sql.sql", "Invalid spawn group {} specified for instance {}. Skipped.", spawnGroupId, fields[0].GetUInt16());
            continue;
        }

        uint16 const instanceMapId = fields[0].GetUInt16();
        if (it->second.mapId != instanceMapId)
        {
            TC_LOG_ERROR("sql.sql", "Instance spawn group {} specified for instance {} has spawns on a different map {}. Skipped.",
                spawnGroupId, instanceMapId, it->second.mapId);
            continue;
        }

        InstanceSpawnGroupInfo& info = _instanceSpawnGroupStore[instanceMapId].emplace_back();
        info.SpawnGroupId = spawnGroupId;
        info.BossStateId = fields[1].GetUInt8();

        uint8 const ALL_STATES = (1 << TO_BE_DECIDED) - 1;
        uint8 const states = fields[2].GetUInt8();
        if (states & ~ALL_STATES)
        {
            info.BossStates = states & ALL_STATES;
            TC_LOG_ERROR("sql.sql", "Instance spawn group ({},{}) had invalid boss state mask {} - truncated to {}.", instanceMapId, spawnGroupId, states, info.BossStates);
        }
        else
            info.BossStates = states;

        uint8 const flags = fields[4].GetUInt8();
        if (flags & ~InstanceSpawnGroupInfo::FLAG_ALL)
        {
            info.Flags = flags & InstanceSpawnGroupInfo::FLAG_ALL;
            TC_LOG_ERROR("sql.sql", "Instance spawn group ({},{}) had invalid flags {} - truncated to {}.", instanceMapId, spawnGroupId, flags, info.Flags);
        }
        else
            info.Flags = flags;

        if ((flags & InstanceSpawnGroupInfo::FLAG_ALLIANCE_ONLY) && (flags & InstanceSpawnGroupInfo::FLAG_HORDE_ONLY))
        {
            info.Flags = flags & ~(InstanceSpawnGroupInfo::FLAG_ALLIANCE_ONLY | InstanceSpawnGroupInfo::FLAG_HORDE_ONLY);
            TC_LOG_ERROR("sql.sql", "Instance spawn group ({},{}) FLAG_ALLIANCE_ONLY and FLAG_HORDE_ONLY may not be used together in a single entry - truncated to {}.", instanceMapId, spawnGroupId, info.Flags);
        }

        ++n;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} instance spawn groups in {} ms", n, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::OnDeleteSpawnData(SpawnData const* data)
{
    auto templateIt = _spawnGroupDataStore.find(data->spawnGroupData->groupId);
    ASSERT(templateIt != _spawnGroupDataStore.end(), "Creature data for (%u,%u) is being deleted and has invalid spawn group index %u!", uint32(data->type), data->spawnId, data->spawnGroupData->groupId);
    if (templateIt->second.flags & SPAWNGROUP_FLAG_SYSTEM) // system groups don't store their members in the map
        return;

    auto pair = _spawnGroupMapStore.equal_range(data->spawnGroupData->groupId);
    for (auto it = pair.first; it != pair.second; ++it)
    {
        if (it->second != data)
            continue;
        _spawnGroupMapStore.erase(it);
        return;
    }
    ABORT_MSG("Spawn data (%u,%u) being removed is member of spawn group %u, but not actually listed in the lookup table for that group!", uint32(data->type), data->spawnId, data->spawnGroupData->groupId);
}

void ObjectMgr::AddGameobjectToGrid(ObjectGuid::LowType guid, GameObjectData const* data)
{
    uint8 mask = data->spawnMask;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY());
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapId, i)][cellCoord.GetId()];
            cell_guids.gameobjects.insert(guid);
        }
    }
}

void ObjectMgr::RemoveGameobjectFromGrid(ObjectGuid::LowType guid, GameObjectData const* data)
{
    uint8 mask = data->spawnMask;
    for (uint8 i = 0; mask != 0; i++, mask >>= 1)
    {
        if (mask & 1)
        {
            CellCoord cellCoord = Trinity::ComputeCellCoord(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY());
            CellObjectGuids& cell_guids = _mapObjectGuidsStore[MAKE_PAIR32(data->mapId, i)][cellCoord.GetId()];
            cell_guids.gameobjects.erase(guid);
        }
    }
}

void ObjectMgr::LoadItemLocales()
{
    uint32 oldMSTime = getMSTime();

    _itemLocaleStore.clear();                                 // need for reload case

    //                                               0   1       2     3
    QueryResult result = WorldDatabase.Query("SELECT ID, locale, Name, Description FROM item_template_locale");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id               = fields[0].GetUInt32();
        std::string localeName  = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        ItemLocale& data = _itemLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.Name);
        AddLocaleString(fields[3].GetString(), locale, data.Description);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Item locale strings in {} ms", uint32(_itemLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadItemTemplates()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0      1       2               3              4        5        6       7          8         9        10        11           12
    QueryResult result = WorldDatabase.Query("SELECT entry, class, subclass, SoundOverrideSubclass, name, displayid, Quality, Flags, FlagsExtra, BuyCount, BuyPrice, SellPrice, InventoryType, "
    //                                              13              14           15          16             17               18                19              20
                                             "AllowableClass, AllowableRace, ItemLevel, RequiredLevel, RequiredSkill, RequiredSkillRank, requiredspell, requiredhonorrank, "
    //                                              21                      22                       23               24        25          26             27           28
                                             "RequiredCityRank, RequiredReputationFaction, RequiredReputationRank, maxcount, stackable, ContainerSlots, StatsCount, stat_type1, "
    //                                            29           30          31           32          33           34          35           36          37           38
                                             "stat_value1, stat_type2, stat_value2, stat_type3, stat_value3, stat_type4, stat_value4, stat_type5, stat_value5, stat_type6, "
    //                                            39           40          41           42           43          44           45           46           47
                                             "stat_value6, stat_type7, stat_value7, stat_type8, stat_value8, stat_type9, stat_value9, stat_type10, stat_value10, "
    //                                                   48                    49           50        51        52         53        54         55      56      57        58
                                             "ScalingStatDistribution, ScalingStatValue, dmg_min1, dmg_max1, dmg_type1, dmg_min2, dmg_max2, dmg_type2, armor, holy_res, fire_res, "
    //                                            59          60         61          62       63       64            65            66          67               68
                                             "nature_res, frost_res, shadow_res, arcane_res, delay, ammo_type, RangedModRange, spellid_1, spelltrigger_1, spellcharges_1, "
    //                                              69              70                71                 72                 73           74               75
                                             "spellppmRate_1, spellcooldown_1, spellcategory_1, spellcategorycooldown_1, spellid_2, spelltrigger_2, spellcharges_2, "
    //                                              76               77              78                  79                 80           81               82
                                             "spellppmRate_2, spellcooldown_2, spellcategory_2, spellcategorycooldown_2, spellid_3, spelltrigger_3, spellcharges_3, "
    //                                              83               84              85                  86                 87           88               89
                                             "spellppmRate_3, spellcooldown_3, spellcategory_3, spellcategorycooldown_3, spellid_4, spelltrigger_4, spellcharges_4, "
    //                                              90               91              92                  93                  94          95               96
                                             "spellppmRate_4, spellcooldown_4, spellcategory_4, spellcategorycooldown_4, spellid_5, spelltrigger_5, spellcharges_5, "
    //                                              97               98              99                  100                 101        102         103       104          105
                                             "spellppmRate_5, spellcooldown_5, spellcategory_5, spellcategorycooldown_5, bonding, description, PageText, LanguageID, PageMaterial, "
    //                                            106       107     108      109          110            111       112     113         114       115   116     117
                                             "startquest, lockid, Material, sheath, RandomProperty, RandomSuffix, block, itemset, MaxDurability, area, Map, BagFamily, "
    //                                            118             119             120             121             122            123              124            125
                                             "TotemCategory, socketColor_1, socketContent_1, socketColor_2, socketContent_2, socketColor_3, socketContent_3, socketBonus, "
    //                                            126                 127                     128            129            130            131         132         133
                                             "GemProperties, RequiredDisenchantSkill, ArmorDamageModifier, duration, ItemLimitCategory, HolidayId, ScriptName, DisenchantID, "
    //                                           134        135            136
                                             "FoodType, minMoneyLoot, maxMoneyLoot, flagsCustom FROM item_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 item templates. DB table `item_template` is empty.");
        return;
    }

    _itemTemplateStore.reserve(result->GetRowCount());
    bool enforceDBCAttributes = sWorld->getBoolConfig(CONFIG_DBC_ENFORCE_ITEM_ATTRIBUTES);

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        ItemTemplate& itemTemplate = _itemTemplateStore[entry];

        itemTemplate.ItemId                    = entry;
        itemTemplate.Class                     = uint32(fields[1].GetUInt8());
        itemTemplate.SubClass                  = uint32(fields[2].GetUInt8());
        itemTemplate.SoundOverrideSubclass     = int32(fields[3].GetInt8());
        itemTemplate.Name1                     = fields[4].GetString();
        itemTemplate.DisplayInfoID             = fields[5].GetUInt32();
        itemTemplate.Quality                   = uint32(fields[6].GetUInt8());
        itemTemplate.Flags                     = fields[7].GetUInt32();
        itemTemplate.Flags2                    = fields[8].GetUInt32();
        itemTemplate.BuyCount                  = uint32(fields[9].GetUInt8());
        itemTemplate.BuyPrice                  = int32(fields[10].GetInt64());
        itemTemplate.SellPrice                 = fields[11].GetUInt32();
        itemTemplate.InventoryType             = uint32(fields[12].GetUInt8());
        itemTemplate.AllowableClass            = fields[13].GetInt32();
        itemTemplate.AllowableRace             = fields[14].GetInt32();
        itemTemplate.ItemLevel                 = uint32(fields[15].GetUInt16());
        itemTemplate.RequiredLevel             = uint32(fields[16].GetUInt8());
        itemTemplate.RequiredSkill             = uint32(fields[17].GetUInt16());
        itemTemplate.RequiredSkillRank         = uint32(fields[18].GetUInt16());
        itemTemplate.RequiredSpell             = fields[19].GetUInt32();
        itemTemplate.RequiredHonorRank         = fields[20].GetUInt32();
        itemTemplate.RequiredCityRank          = fields[21].GetUInt32();
        itemTemplate.RequiredReputationFaction = uint32(fields[22].GetUInt16());
        itemTemplate.RequiredReputationRank    = uint32(fields[23].GetUInt16());
        itemTemplate.MaxCount                  = fields[24].GetInt32();
        itemTemplate.Stackable                 = fields[25].GetInt32();
        itemTemplate.ContainerSlots            = uint32(fields[26].GetUInt8());
        itemTemplate.StatsCount                = uint32(fields[27].GetUInt8());

        if (itemTemplate.StatsCount > MAX_ITEM_PROTO_STATS)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has too large value in statscount ({}), replace by hardcoded limit ({}).", entry, itemTemplate.StatsCount, MAX_ITEM_PROTO_STATS);
            itemTemplate.StatsCount = MAX_ITEM_PROTO_STATS;
        }

        for (uint8 i = 0; i < itemTemplate.StatsCount; ++i)
        {
            itemTemplate.ItemStat[i].ItemStatType  = uint32(fields[28 + i*2].GetUInt8());
            itemTemplate.ItemStat[i].ItemStatValue = int32(fields[29 + i*2].GetInt16());
        }

        itemTemplate.ScalingStatDistribution = uint32(fields[48].GetUInt16());
        itemTemplate.ScalingStatValue        = fields[49].GetInt32();

        for (uint8 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
        {
            itemTemplate.Damage[i].DamageMin  = fields[50 + i*3].GetFloat();
            itemTemplate.Damage[i].DamageMax  = fields[51 + i*3].GetFloat();
            itemTemplate.Damage[i].DamageType = uint32(fields[52 + i*3].GetUInt8());
        }

        itemTemplate.Armor          = uint32(fields[56].GetUInt16());
        itemTemplate.HolyRes        = uint32(fields[57].GetUInt8());
        itemTemplate.FireRes        = uint32(fields[58].GetUInt8());
        itemTemplate.NatureRes      = uint32(fields[59].GetUInt8());
        itemTemplate.FrostRes       = uint32(fields[60].GetUInt8());
        itemTemplate.ShadowRes      = uint32(fields[61].GetUInt8());
        itemTemplate.ArcaneRes      = uint32(fields[62].GetUInt8());
        itemTemplate.Delay          = uint32(fields[63].GetUInt16());
        itemTemplate.AmmoType       = uint32(fields[64].GetUInt8());
        itemTemplate.RangedModRange = fields[65].GetFloat();

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            itemTemplate.Spells[i].SpellId               = fields[66 + i*7  ].GetInt32();
            itemTemplate.Spells[i].SpellTrigger          = uint32(fields[67 + i*7].GetUInt8());
            itemTemplate.Spells[i].SpellCharges          = int32(fields[68 + i*7].GetInt16());
            itemTemplate.Spells[i].SpellPPMRate          = fields[69 + i*7].GetFloat();
            itemTemplate.Spells[i].SpellCooldown         = fields[70 + i*7].GetInt32();
            itemTemplate.Spells[i].SpellCategory         = uint32(fields[71 + i*7].GetUInt16());
            itemTemplate.Spells[i].SpellCategoryCooldown = fields[72 + i*7].GetInt32();
        }

        itemTemplate.Bonding        = uint32(fields[101].GetUInt8());
        itemTemplate.Description    = fields[102].GetString();
        itemTemplate.PageText       = fields[103].GetUInt32();
        itemTemplate.LanguageID     = uint32(fields[104].GetUInt8());
        itemTemplate.PageMaterial   = uint32(fields[105].GetUInt8());
        itemTemplate.StartQuest     = fields[106].GetUInt32();
        itemTemplate.LockID         = fields[107].GetUInt32();
        itemTemplate.Material       = int32(fields[108].GetInt8());
        itemTemplate.Sheath         = uint32(fields[109].GetUInt8());
        itemTemplate.RandomProperty = fields[110].GetUInt32();
        itemTemplate.RandomSuffix   = fields[111].GetInt32();
        itemTemplate.Block          = fields[112].GetUInt32();
        itemTemplate.ItemSet        = fields[113].GetUInt32();
        itemTemplate.MaxDurability  = uint32(fields[114].GetUInt16());
        itemTemplate.Area           = fields[115].GetUInt32();
        itemTemplate.Map            = uint32(fields[116].GetUInt16());
        itemTemplate.BagFamily      = fields[117].GetUInt32();
        itemTemplate.TotemCategory  = fields[118].GetUInt32();

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i)
        {
            itemTemplate.Socket[i].Color   = uint32(fields[119 + i*2].GetUInt8());
            itemTemplate.Socket[i].Content = fields[120 + i*2].GetUInt32();
        }

        itemTemplate.socketBonus             = fields[125].GetUInt32();
        itemTemplate.GemProperties           = fields[126].GetUInt32();
        itemTemplate.RequiredDisenchantSkill = uint32(fields[127].GetInt16());
        itemTemplate.ArmorDamageModifier     = fields[128].GetFloat();
        itemTemplate.Duration                = fields[129].GetUInt32();
        itemTemplate.ItemLimitCategory       = uint32(fields[130].GetInt16());
        itemTemplate.HolidayId               = fields[131].GetUInt32();
        itemTemplate.ScriptId                = sObjectMgr->GetScriptId(fields[132].GetString());
        itemTemplate.DisenchantID            = fields[133].GetUInt32();
        itemTemplate.FoodType                = uint32(fields[134].GetUInt8());
        itemTemplate.MinMoneyLoot            = fields[135].GetUInt32();
        itemTemplate.MaxMoneyLoot            = fields[136].GetUInt32();
        itemTemplate.FlagsCu                 = fields[137].GetUInt32();

        // Checks

        ItemEntry const* dbcitem = sItemStore.LookupEntry(entry);

        if (dbcitem)
        {
            if (itemTemplate.Class != dbcitem->ClassID)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have a correct class {}, must be {} .", entry, itemTemplate.Class, dbcitem->ClassID);
                if (enforceDBCAttributes)
                    itemTemplate.Class = dbcitem->ClassID;
            }

            if (itemTemplate.SoundOverrideSubclass != dbcitem->SoundOverrideSubclassID)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have a correct SoundOverrideSubclass ({}), must be {} .", entry, itemTemplate.SoundOverrideSubclass, dbcitem->SoundOverrideSubclassID);
                if (enforceDBCAttributes)
                    itemTemplate.SoundOverrideSubclass = dbcitem->SoundOverrideSubclassID;
            }
            if (itemTemplate.Material != dbcitem->Material)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have a correct material ({}), must be {} .", entry, itemTemplate.Material, dbcitem->Material);
                if (enforceDBCAttributes)
                    itemTemplate.Material = dbcitem->Material;
            }
            if (itemTemplate.InventoryType != dbcitem->InventoryType)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have a correct inventory type ({}), must be {} .", entry, itemTemplate.InventoryType, dbcitem->InventoryType);
                if (enforceDBCAttributes)
                    itemTemplate.InventoryType = dbcitem->InventoryType;
            }
            if (itemTemplate.DisplayInfoID != dbcitem->DisplayInfoID)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have a correct display id ({}), must be {} .", entry, itemTemplate.DisplayInfoID, dbcitem->DisplayInfoID);
                if (enforceDBCAttributes)
                    itemTemplate.DisplayInfoID = dbcitem->DisplayInfoID;
            }
            if (itemTemplate.Sheath != dbcitem->SheatheType)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have a correct sheathid ({}), must be {} .", entry, itemTemplate.Sheath, dbcitem->SheatheType);
                if (enforceDBCAttributes)
                    itemTemplate.Sheath = dbcitem->SheatheType;
            }

        }
        else
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not exist in item.dbc! (not correct id?).", entry);

        if (itemTemplate.Class >= MAX_ITEM_CLASS)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong Class value ({})", entry, itemTemplate.Class);
            itemTemplate.Class = ITEM_CLASS_MISCELLANEOUS;
        }

        if (itemTemplate.SubClass >= MaxItemSubclassValues[itemTemplate.Class])
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong Subclass value ({}) for class {}", entry, itemTemplate.SubClass, itemTemplate.Class);
            itemTemplate.SubClass = 0;// exist for all item classes
        }

        if (itemTemplate.Quality >= MAX_ITEM_QUALITY)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong Quality value ({})", entry, itemTemplate.Quality);
            itemTemplate.Quality = ITEM_QUALITY_NORMAL;
        }

        if (itemTemplate.Flags2 & ITEM_FLAG2_FACTION_HORDE)
        {
            if (FactionEntry const* faction = sFactionStore.LookupEntry(HORDE))
                if ((itemTemplate.AllowableRace & faction->ReputationRaceMask[0]) == 0)
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has value ({}) in `AllowableRace` races, not compatible with ITEM_FLAG2_FACTION_HORDE ({}) in Flags field, item cannot be equipped or used by these races.",
                        entry, itemTemplate.AllowableRace, ITEM_FLAG2_FACTION_HORDE);

            if (itemTemplate.Flags2 & ITEM_FLAG2_FACTION_ALLIANCE)
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has value ({}) in `Flags2` flags (ITEM_FLAG2_FACTION_ALLIANCE) and ITEM_FLAG2_FACTION_HORDE ({}) in Flags field, this is a wrong combination.",
                    entry, ITEM_FLAG2_FACTION_ALLIANCE, ITEM_FLAG2_FACTION_HORDE);
        }
        else if (itemTemplate.Flags2 & ITEM_FLAG2_FACTION_ALLIANCE)
        {
            if (FactionEntry const* faction = sFactionStore.LookupEntry(ALLIANCE))
                if ((itemTemplate.AllowableRace & faction->ReputationRaceMask[0]) == 0)
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has value ({}) in `AllowableRace` races, not compatible with ITEM_FLAG2_FACTION_ALLIANCE ({}) in Flags field, item cannot be equipped or used by these races.",
                        entry, itemTemplate.AllowableRace, ITEM_FLAG2_FACTION_ALLIANCE);
        }

        if (itemTemplate.BuyCount <= 0)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong BuyCount value ({}), set to default(1).", entry, itemTemplate.BuyCount);
            itemTemplate.BuyCount = 1;
        }

        if (itemTemplate.InventoryType >= MAX_INVTYPE)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong InventoryType value ({})", entry, itemTemplate.InventoryType);
            itemTemplate.InventoryType = INVTYPE_NON_EQUIP;
        }

        if (itemTemplate.RequiredSkill >= MAX_SKILL_TYPE)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong RequiredSkill value ({})", entry, itemTemplate.RequiredSkill);
            itemTemplate.RequiredSkill = 0;
        }

        {
            // can be used in equip slot, as page read use in inventory, or spell casting at use
            bool req = itemTemplate.InventoryType != INVTYPE_NON_EQUIP || itemTemplate.PageText;
            if (!req)
                for (uint8 j = 0; j < MAX_ITEM_PROTO_SPELLS; ++j)
                {
                    if (itemTemplate.Spells[j].SpellId > 0)
                    {
                        req = true;
                        break;
                    }
                }

            if (req)
            {
                if (!(itemTemplate.AllowableClass & CLASSMASK_ALL_PLAYABLE))
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have any playable classes ({}) in `AllowableClass` and can't be equipped or used.", entry, itemTemplate.AllowableClass);

                if (!(itemTemplate.AllowableRace & RACEMASK_ALL_PLAYABLE))
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have any playable races ({}) in `AllowableRace` and can't be equipped or used.", entry, itemTemplate.AllowableRace);
            }
        }

        if (itemTemplate.RequiredSpell && !sSpellMgr->GetSpellInfo(itemTemplate.RequiredSpell))
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has a wrong (non-existing) spell in RequiredSpell ({})", entry, itemTemplate.RequiredSpell);
            itemTemplate.RequiredSpell = 0;
        }

        if (itemTemplate.RequiredReputationRank >= MAX_REPUTATION_RANK)
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong reputation rank in RequiredReputationRank ({}), item can't be used.", entry, itemTemplate.RequiredReputationRank);

        if (itemTemplate.RequiredReputationFaction)
        {
            if (!sFactionStore.LookupEntry(itemTemplate.RequiredReputationFaction))
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong (not existing) faction in RequiredReputationFaction ({})", entry, itemTemplate.RequiredReputationFaction);
                itemTemplate.RequiredReputationFaction = 0;
            }

            if (itemTemplate.RequiredReputationRank == MIN_REPUTATION_RANK)
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has min. reputation rank in RequiredReputationRank (0) but RequiredReputationFaction > 0, faction setting is useless.", entry);
        }

        if (itemTemplate.MaxCount < -1)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has too large negative in maxcount ({}), replace by value (-1) no storing limits.", entry, itemTemplate.MaxCount);
            itemTemplate.MaxCount = -1;
        }

        if (itemTemplate.Stackable == 0)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong value in stackable ({}), replace by default 1.", entry, itemTemplate.Stackable);
            itemTemplate.Stackable = 1;
        }
        else if (itemTemplate.Stackable < -1)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has too large negative in stackable ({}), replace by value (-1) no stacking limits.", entry, itemTemplate.Stackable);
            itemTemplate.Stackable = -1;
        }

        if (itemTemplate.ContainerSlots > MAX_BAG_SIZE)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has too large value in ContainerSlots ({}), replace by hardcoded limit ({}).", entry, itemTemplate.ContainerSlots, MAX_BAG_SIZE);
            itemTemplate.ContainerSlots = MAX_BAG_SIZE;
        }

        for (uint8 j = 0; j < itemTemplate.StatsCount; ++j)
        {
            // for ItemStatValue != 0
            if (itemTemplate.ItemStat[j].ItemStatValue && itemTemplate.ItemStat[j].ItemStatType >= MAX_ITEM_MOD)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong (non-existing?) stat_type{} ({})", entry, j+1, itemTemplate.ItemStat[j].ItemStatType);
                itemTemplate.ItemStat[j].ItemStatType = 0;
            }

            switch (itemTemplate.ItemStat[j].ItemStatType)
            {
                case ITEM_MOD_SPELL_HEALING_DONE:
                case ITEM_MOD_SPELL_DAMAGE_DONE:
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has deprecated stat_type{} ({})", entry, j+1, itemTemplate.ItemStat[j].ItemStatType);
                    break;
                default:
                    break;
            }
        }

        for (uint8 j = 0; j < MAX_ITEM_PROTO_DAMAGES; ++j)
        {
            if (itemTemplate.Damage[j].DamageType >= MAX_SPELL_SCHOOL)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong dmg_type{} ({})", entry, j+1, itemTemplate.Damage[j].DamageType);
                itemTemplate.Damage[j].DamageType = 0;
            }
        }

        // special format
        if ((itemTemplate.Spells[0].SpellId == 483) || (itemTemplate.Spells[0].SpellId == 55884))
        {
            // spell_1
            if (itemTemplate.Spells[0].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong item spell trigger value in spelltrigger_{} ({}) for special learning format", entry, 0+1, itemTemplate.Spells[0].SpellTrigger);
                itemTemplate.Spells[0].SpellId = 0;
                itemTemplate.Spells[0].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                itemTemplate.Spells[1].SpellId = 0;
                itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
            }

            // spell_2 have learning spell
            if (itemTemplate.Spells[1].SpellTrigger != ITEM_SPELLTRIGGER_LEARN_SPELL_ID)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong item spell trigger value in spelltrigger_{} ({}) for special learning format.", entry, 1+1, itemTemplate.Spells[1].SpellTrigger);
                itemTemplate.Spells[0].SpellId = 0;
                itemTemplate.Spells[1].SpellId = 0;
                itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
            }
            else if (!itemTemplate.Spells[1].SpellId)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not have an expected spell in spellid_{} in special learning format.", entry, 1+1);
                itemTemplate.Spells[0].SpellId = 0;
                itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
            }
            else if (itemTemplate.Spells[1].SpellId != -1)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemTemplate.Spells[1].SpellId);
                if (!spellInfo && !DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, itemTemplate.Spells[1].SpellId, nullptr))
                {
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong (not existing) spell in spellid_{} ({})", entry, 1+1, itemTemplate.Spells[1].SpellId);
                    itemTemplate.Spells[0].SpellId = 0;
                    itemTemplate.Spells[1].SpellId = 0;
                    itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }
                // allowed only in special format
                else if ((itemTemplate.Spells[1].SpellId == 483) || (itemTemplate.Spells[1].SpellId == 55884))
                {
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has broken spell in spellid_{} ({})", entry, 1+1, itemTemplate.Spells[1].SpellId);
                    itemTemplate.Spells[0].SpellId = 0;
                    itemTemplate.Spells[1].SpellId = 0;
                    itemTemplate.Spells[1].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }
            }

            // spell_3*, spell_4*, spell_5* is empty
            for (uint8 j = 2; j < MAX_ITEM_PROTO_SPELLS; ++j)
            {
                if (itemTemplate.Spells[j].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                {
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong item spell trigger value in spelltrigger_{} ({})", entry, j+1, itemTemplate.Spells[j].SpellTrigger);
                    itemTemplate.Spells[j].SpellId = 0;
                    itemTemplate.Spells[j].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }
                else if (itemTemplate.Spells[j].SpellId != 0)
                {
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong spell in spellid_{} ({}) for learning special format", entry, j+1, itemTemplate.Spells[j].SpellId);
                    itemTemplate.Spells[j].SpellId = 0;
                }
            }
        }
        // normal spell list
        else
        {
            for (uint8 j = 0; j < MAX_ITEM_PROTO_SPELLS; ++j)
            {
                if (itemTemplate.Spells[j].SpellTrigger >= MAX_ITEM_SPELLTRIGGER || itemTemplate.Spells[j].SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID)
                {
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong item spell trigger value in spelltrigger_{} ({})", entry, j+1, itemTemplate.Spells[j].SpellTrigger);
                    itemTemplate.Spells[j].SpellId = 0;
                    itemTemplate.Spells[j].SpellTrigger = ITEM_SPELLTRIGGER_ON_USE;
                }

                if (itemTemplate.Spells[j].SpellId && itemTemplate.Spells[j].SpellId != -1)
                {
                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemTemplate.Spells[j].SpellId);
                    if (!spellInfo && !DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, itemTemplate.Spells[j].SpellId, nullptr))
                    {
                        TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong (not existing) spell in spellid_{} ({})", entry, j+1, itemTemplate.Spells[j].SpellId);
                        itemTemplate.Spells[j].SpellId = 0;
                    }
                    // allowed only in special format
                    else if ((itemTemplate.Spells[j].SpellId == 483) || (itemTemplate.Spells[j].SpellId == 55884))
                    {
                        TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has broken spell in spellid_{} ({})", entry, j+1, itemTemplate.Spells[j].SpellId);
                        itemTemplate.Spells[j].SpellId = 0;
                    }
                }
            }
        }

        if (itemTemplate.Bonding >= MAX_BIND_TYPE)
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong Bonding value ({})", entry, itemTemplate.Bonding);

        if (itemTemplate.PageText && !GetPageText(itemTemplate.PageText))
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has non existing first page (Id:{})", entry, itemTemplate.PageText);

        if (itemTemplate.LockID && !sLockStore.LookupEntry(itemTemplate.LockID))
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong LockID ({})", entry, itemTemplate.LockID);

        if (itemTemplate.Sheath >= MAX_SHEATHETYPE)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong Sheath ({})", entry, itemTemplate.Sheath);
            itemTemplate.Sheath = SHEATHETYPE_NONE;
        }

        if (itemTemplate.RandomProperty)
        {
            // To be implemented later
            if (itemTemplate.RandomProperty == -1)
                itemTemplate.RandomProperty = 0;

            else if (!sItemRandomPropertiesStore.LookupEntry(GetItemEnchantMod(itemTemplate.RandomProperty)))
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has unknown (wrong or not listed in `item_enchantment_template`) RandomProperty ({})", entry, itemTemplate.RandomProperty);
                itemTemplate.RandomProperty = 0;
            }
        }

        if (itemTemplate.RandomSuffix && !sItemRandomSuffixStore.LookupEntry(GetItemEnchantMod(itemTemplate.RandomSuffix)))
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong RandomSuffix ({})", entry, itemTemplate.RandomSuffix);
            itemTemplate.RandomSuffix = 0;
        }

        if (itemTemplate.ItemSet && !sItemSetStore.LookupEntry(itemTemplate.ItemSet))
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) have wrong ItemSet ({})", entry, itemTemplate.ItemSet);
            itemTemplate.ItemSet = 0;
        }

        if (itemTemplate.Area && !sAreaTableStore.LookupEntry(itemTemplate.Area))
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong Area ({})", entry, itemTemplate.Area);

        if (itemTemplate.Map && !sMapStore.LookupEntry(itemTemplate.Map))
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong Map ({})", entry, itemTemplate.Map);

        if (itemTemplate.BagFamily)
        {
            // check bits
            for (uint32 j = 0; j < sizeof(itemTemplate.BagFamily)*8; ++j)
            {
                uint32 mask = 1 << j;
                if ((itemTemplate.BagFamily & mask) == 0)
                    continue;

                ItemBagFamilyEntry const* bf = sItemBagFamilyStore.LookupEntry(j+1);
                if (!bf)
                {
                    TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has bag family bit set not listed in ItemBagFamily.dbc, remove bit", entry);
                    itemTemplate.BagFamily &= ~mask;
                    continue;
                }

                if (BAG_FAMILY_MASK_CURRENCY_TOKENS & mask)
                {
                    CurrencyTypesEntry const* ctEntry = sCurrencyTypesStore.LookupEntry(itemTemplate.ItemId);
                    if (!ctEntry)
                    {
                        TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has currency bag family bit set in BagFamily but not listed in CurrencyTypes.dbc, remove bit", entry);
                        itemTemplate.BagFamily &= ~mask;
                    }
                }
            }
        }

        if (itemTemplate.TotemCategory && !sTotemCategoryStore.LookupEntry(itemTemplate.TotemCategory))
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong TotemCategory ({})", entry, itemTemplate.TotemCategory);

        for (uint8 j = 0; j < MAX_ITEM_PROTO_SOCKETS; ++j)
        {
            if (itemTemplate.Socket[j].Color && (itemTemplate.Socket[j].Color & SOCKET_COLOR_ALL) != itemTemplate.Socket[j].Color)
            {
                TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong socketColor_{} ({})", entry, j+1, itemTemplate.Socket[j].Color);
                itemTemplate.Socket[j].Color = 0;
            }
        }

        if (itemTemplate.GemProperties && !sGemPropertiesStore.LookupEntry(itemTemplate.GemProperties))
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong GemProperties ({})", entry, itemTemplate.GemProperties);

        if (itemTemplate.FoodType >= MAX_PET_DIET)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong FoodType value ({})", entry, itemTemplate.FoodType);
            itemTemplate.FoodType = 0;
        }

        if (itemTemplate.ItemLimitCategory && !sItemLimitCategoryStore.LookupEntry(itemTemplate.ItemLimitCategory))
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong LimitCategory value ({})", entry, itemTemplate.ItemLimitCategory);
            itemTemplate.ItemLimitCategory = 0;
        }

        if (itemTemplate.HolidayId && !sHolidaysStore.LookupEntry(itemTemplate.HolidayId))
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry: {}) has wrong HolidayId value ({})", entry, itemTemplate.HolidayId);
            itemTemplate.HolidayId = 0;
        }

        if (itemTemplate.FlagsCu & ITEM_FLAGS_CU_DURATION_REAL_TIME && !itemTemplate.Duration)
        {
            TC_LOG_ERROR("sql.sql", "Item (Entry {}) has flag ITEM_FLAGS_CU_DURATION_REAL_TIME but it does not have duration limit", entry);
            itemTemplate.FlagsCu &= ~ITEM_FLAGS_CU_DURATION_REAL_TIME;
        }

        // Load cached data
        itemTemplate._LoadTotalAP();
    } while (result->NextRow());

    // Check if item templates for DBC referenced character start outfit are present
    std::set<uint32> notFoundOutfit;
    for (uint32 i = 1; i < sCharStartOutfitStore.GetNumRows(); ++i)
    {
        CharStartOutfitEntry const* entry = sCharStartOutfitStore.LookupEntry(i);
        if (!entry)
            continue;

        for (uint8 j = 0; j < MAX_OUTFIT_ITEMS; ++j)
        {
            if (entry->ItemID[j] <= 0)
                continue;

            uint32 item_id = entry->ItemID[j];

            if (!GetItemTemplate(item_id))
                notFoundOutfit.insert(item_id);
        }
    }

    for (std::set<uint32>::const_iterator itr = notFoundOutfit.begin(); itr != notFoundOutfit.end(); ++itr)
        TC_LOG_ERROR("sql.sql", "Item (Entry: {}) does not exist in `item_template` but is referenced in `CharStartOutfit.dbc`", *itr);

    TC_LOG_INFO("server.loading", ">> Loaded {} item templates in {} ms", _itemTemplateStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

ItemTemplate const* ObjectMgr::GetItemTemplate(uint32 entry) const
{
    return Trinity::Containers::MapGetValuePtr(_itemTemplateStore, entry);
}

void ObjectMgr::LoadItemSetNameLocales()
{
    uint32 oldMSTime = getMSTime();

    _itemSetNameLocaleStore.clear();                                 // need for reload case

    //                                               0   1       2
    QueryResult result = WorldDatabase.Query("SELECT ID, locale, Name FROM item_set_names_locale");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id               = fields[0].GetUInt32();
        std::string localeName  = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        ItemSetNameLocale& data = _itemSetNameLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.Name);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Item set name locale strings in {} ms", uint64(_itemSetNameLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadItemSetNames()
{
    uint32 oldMSTime = getMSTime();

    _itemSetNameStore.clear();                               // needed for reload case

    std::set<uint32> itemSetItems;

    // fill item set member ids
    for (uint32 entryId = 0; entryId < sItemSetStore.GetNumRows(); ++entryId)
    {
        ItemSetEntry const* setEntry = sItemSetStore.LookupEntry(entryId);
        if (!setEntry)
            continue;

        for (uint32 i = 0; i < MAX_ITEM_SET_ITEMS; ++i)
            if (setEntry->ItemID[i])
                itemSetItems.insert(setEntry->ItemID[i]);
    }

    //                                                  0        1            2
    QueryResult result = WorldDatabase.Query("SELECT `entry`, `name`, `InventoryType` FROM `item_set_names`");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 item set names. DB table `item_set_names` is empty.");
        return;
    }

    _itemSetNameStore.rehash(result->GetRowCount());
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        if (itemSetItems.find(entry) == itemSetItems.end())
        {
            TC_LOG_ERROR("sql.sql", "Item set name (Entry: {}) not found in ItemSet.dbc, data useless.", entry);
            continue;
        }

        ItemSetNameEntry &data = _itemSetNameStore[entry];
        data.name = fields[1].GetString();

        uint32 invType = fields[2].GetUInt8();
        if (invType >= MAX_INVTYPE)
        {
            TC_LOG_ERROR("sql.sql", "Item set name (Entry: {}) has wrong InventoryType value ({})", entry, invType);
            invType = INVTYPE_NON_EQUIP;
        }

        data.InventoryType = invType;
        itemSetItems.erase(entry);
        ++count;
    } while (result->NextRow());

    if (!itemSetItems.empty())
    {
        ItemTemplate const* pProto;
        for (std::set<uint32>::iterator itr = itemSetItems.begin(); itr != itemSetItems.end(); ++itr)
        {
            uint32 entry = *itr;
            // add data from item_template if available
            pProto = sObjectMgr->GetItemTemplate(entry);
            if (pProto)
            {
                TC_LOG_ERROR("sql.sql", "Item set part (Entry: {}) does not have entry in `item_set_names`, adding data from `item_template`.", entry);
                ItemSetNameEntry &data = _itemSetNameStore[entry];
                data.name = pProto->Name1;
                data.InventoryType = pProto->InventoryType;
                ++count;
            }
            else
                TC_LOG_ERROR("sql.sql", "Item set part (Entry: {}) does not have entry in `item_set_names`, set will not display properly.", entry);
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} item set names in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadVehicleTemplateAccessories()
{
    uint32 oldMSTime = getMSTime();

    _vehicleTemplateAccessoryStore.clear();                           // needed for reload case

    uint32 count = 0;

    //                                                  0             1              2          3           4             5
    QueryResult result = WorldDatabase.Query("SELECT `entry`, `accessory_entry`, `seat_id`, `minion`, `summontype`, `summontimer` FROM `vehicle_template_accessory`");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 vehicle template accessories. DB table `vehicle_template_accessory` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 entry        = fields[0].GetUInt32();
        uint32 accessory    = fields[1].GetUInt32();
        int8   seatId       = fields[2].GetInt8();
        bool   isMinion     = fields[3].GetBool();
        uint8  summonType   = fields[4].GetUInt8();
        uint32 summonTimer  = fields[5].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(entry))
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_template_accessory`: creature template entry {} does not exist.", entry);
            continue;
        }

        if (!sObjectMgr->GetCreatureTemplate(accessory))
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_template_accessory`: Accessory {} does not exist.", accessory);
            continue;
        }

        if (_spellClickInfoStore.find(entry) == _spellClickInfoStore.end())
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_template_accessory`: creature template entry {} has no data in npc_spellclick_spells", entry);
            continue;
        }

        _vehicleTemplateAccessoryStore[entry].push_back(VehicleAccessory(accessory, seatId, isMinion, summonType, summonTimer));

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Vehicle Template Accessories in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadVehicleTemplate()
{
    uint32 oldMSTime = getMSTime();

    _vehicleTemplateStore.clear();

    //                                               0           1
    QueryResult result = WorldDatabase.Query("SELECT creatureId, despawnDelayMs FROM vehicle_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 vehicle template. DB table `vehicle_template` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 creatureId = fields[0].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(creatureId))
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_template`: Vehicle {} does not exist.", creatureId);
            continue;
        }

        VehicleTemplate& vehicleTemplate = _vehicleTemplateStore[creatureId];
        vehicleTemplate.DespawnDelay = Milliseconds(fields[1].GetInt32());

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Vehicle Template entries in {} ms", _vehicleTemplateStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadVehicleAccessories()
{
    uint32 oldMSTime = getMSTime();

    _vehicleAccessoryStore.clear();                           // needed for reload case

    uint32 count = 0;

    //                                                  0             1             2          3           4             5
    QueryResult result = WorldDatabase.Query("SELECT `guid`, `accessory_entry`, `seat_id`, `minion`, `summontype`, `summontimer` FROM `vehicle_accessory`");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 Vehicle Accessories in {} ms", GetMSTimeDiffToNow(oldMSTime));
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 uiGUID       = fields[0].GetUInt32();
        uint32 uiAccessory  = fields[1].GetUInt32();
        int8   uiSeat       = int8(fields[2].GetInt16());
        bool   bMinion      = fields[3].GetBool();
        uint8  uiSummonType = fields[4].GetUInt8();
        uint32 uiSummonTimer= fields[5].GetUInt32();

        if (!sObjectMgr->GetCreatureTemplate(uiAccessory))
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_accessory`: Accessory {} does not exist.", uiAccessory);
            continue;
        }

        _vehicleAccessoryStore[uiGUID].push_back(VehicleAccessory(uiAccessory, uiSeat, bMinion, uiSummonType, uiSummonTimer));

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Vehicle Accessories in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadVehicleSeatAddon()
{
    uint32 oldMSTime = getMSTime();

    _vehicleSeatAddonStore.clear();                           // needed for reload case

    uint32 count = 0;

    //                                                0            1                  2             3             4             5             6
    QueryResult result = WorldDatabase.Query("SELECT `SeatEntry`, `SeatOrientation`, `ExitParamX`, `ExitParamY`, `ExitParamZ`, `ExitParamO`, `ExitParamValue` FROM `vehicle_seat_addon`");

    if (!result)
    {
        TC_LOG_ERROR("server.loading", ">> Loaded 0 vehicle seat addons. DB table `vehicle_seat_addon` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 seatID = fields[0].GetUInt32();
        float orientation = fields[1].GetFloat();
        float exitX = fields[2].GetFloat();
        float exitY = fields[3].GetFloat();
        float exitZ = fields[4].GetFloat();
        float exitO = fields[5].GetFloat();
        uint8 exitParam = fields[6].GetUInt8();

        if (!sVehicleSeatStore.LookupEntry(seatID))
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_seat_addon`: SeatID: {} does not exist in VehicleSeat.dbc. Skipping entry.", seatID);
            continue;
        }

        // Sanitizing values
        if (orientation > float(M_PI * 2))
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_seat_addon`: SeatID: {} is using invalid angle offset value ({}). Set Value to 0.", seatID, orientation);
            orientation = 0.0f;
        }

        if (exitParam >= AsUnderlyingType(VehicleExitParameters::VehicleExitParamMax))
        {
            TC_LOG_ERROR("sql.sql", "Table `vehicle_seat_addon`: SeatID: {} is using invalid exit parameter value ({}). Setting to 0 (none).", seatID, exitParam);
            continue;
        }

        _vehicleSeatAddonStore[seatID] = VehicleSeatAddon(orientation, exitX, exitY, exitZ, exitO, exitParam);

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Vehicle Seat Addon entries in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadPetLevelInfo()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0               1      2   3     4    5    6    7     8    9      10       11
    QueryResult result = WorldDatabase.Query("SELECT creature_entry, level, hp, mana, str, agi, sta, inte, spi, armor, min_dmg, max_dmg FROM pet_levelstats");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 level pet stats definitions. DB table `pet_levelstats` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 creature_id = fields[0].GetUInt32();
        if (!sObjectMgr->GetCreatureTemplate(creature_id))
        {
            TC_LOG_ERROR("sql.sql", "Wrong creature id {} in `pet_levelstats` table, ignoring.", creature_id);
            continue;
        }

        uint32 current_level = fields[1].GetUInt8();
        if (current_level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        {
            if (current_level > STRONG_MAX_LEVEL)        // hardcoded level maximum
                TC_LOG_ERROR("sql.sql", "Wrong (> {}) level {} in `pet_levelstats` table, ignoring.", STRONG_MAX_LEVEL, current_level);
            else
            {
                TC_LOG_INFO("misc", "Unused (> MaxPlayerLevel in worldserver.conf) level {} in `pet_levelstats` table, ignoring.", current_level);
                ++count;                                // make result loading percent "expected" correct in case disabled detail mode for example.
            }
            continue;
        }
        else if (current_level < 1)
        {
            TC_LOG_ERROR("sql.sql", "Wrong (<1) level {} in `pet_levelstats` table, ignoring.", current_level);
            continue;
        }

        auto& pInfoMapEntry = _petInfoStore[creature_id];
        if (!pInfoMapEntry)
            pInfoMapEntry = std::make_unique<PetLevelInfo[]>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));

        // data for level 1 stored in [0] array element, ...
        PetLevelInfo* pLevelInfo = &pInfoMapEntry[current_level - 1];

        pLevelInfo->health = fields[2].GetUInt16();
        pLevelInfo->mana   = fields[3].GetUInt16();
        pLevelInfo->armor  = fields[9].GetUInt32();
        pLevelInfo->minDamage = fields[10].GetUInt16();
        pLevelInfo->maxDamage = fields[11].GetUInt16();

        for (uint8 i = 0; i < MAX_STATS; i++)
            pLevelInfo->stats[i] = fields[i + 4].GetUInt16();

        ++count;
    }
    while (result->NextRow());

    // Fill gaps and check integrity
    for (PetLevelInfoContainer::iterator itr = _petInfoStore.begin(); itr != _petInfoStore.end(); ++itr)
    {
        auto& pInfo = itr->second;

        // fatal error if no level 1 data
        if (!pInfo || pInfo[0].health == 0)
        {
            TC_LOG_ERROR("sql.sql", "Creature {} does not have pet stats data for Level 1!", itr->first);
            ABORT();
        }

        // fill level gaps
        for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
        {
            if (pInfo[level].health == 0)
            {
                TC_LOG_ERROR("sql.sql", "Creature {} has no data for Level {} pet stats data, using data of Level {}.", itr->first, level + 1, level);
                pInfo[level] = pInfo[level - 1];
            }
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} level pet stats definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

PetLevelInfo const* ObjectMgr::GetPetLevelInfo(uint32 creature_id, uint8 level) const
{
    if (level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        level = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    auto itr = _petInfoStore.find(creature_id);
    if (itr == _petInfoStore.end())
        return nullptr;

    return &itr->second[level - 1];                         // data for level 1 stored in [0] array element, ...
}

void ObjectMgr::PlayerCreateInfoAddItemHelper(uint32 race_, uint32 class_, uint32 itemId, int32 count)
{
    if (!_playerInfo[race_][class_])
        return;

    if (count > 0)
        _playerInfo[race_][class_]->item.push_back(PlayerCreateInfoItem(itemId, count));
    else
    {
        if (count < -1)
            TC_LOG_ERROR("sql.sql", "Invalid count {} specified on item {} be removed from original player create info (use -1)!", count, itemId);

        for (uint32 gender = 0; gender < GENDER_NONE; ++gender)
        {
            if (CharStartOutfitEntry const* entry = GetCharStartOutfitEntry(race_, class_, gender))
            {
                bool found = false;
                for (uint8 x = 0; x < MAX_OUTFIT_ITEMS; ++x)
                {
                    if (entry->ItemID[x] > 0 && uint32(entry->ItemID[x]) == itemId)
                    {
                        found = true;
                        const_cast<CharStartOutfitEntry*>(entry)->ItemID[x] = 0;
                        break;
                    }
                }

                if (!found)
                    TC_LOG_ERROR("sql.sql", "Item {} specified to be removed from original create info not found in dbc!", itemId);
            }
        }
    }
}

void ObjectMgr::LoadPlayerInfo()
{
    // Load playercreate
    {
        uint32 oldMSTime = getMSTime();
        //                                                0     1      2    3        4          5           6
        QueryResult result = WorldDatabase.Query("SELECT race, class, map, zone, position_x, position_y, position_z, orientation FROM playercreateinfo");

        if (!result)
        {
            TC_LOG_ERROR("server.loading", ">> Loaded 0 player create definitions. DB table `playercreateinfo` is empty.");
            ABORT();
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();

                uint32 current_race  = fields[0].GetUInt8();
                uint32 current_class = fields[1].GetUInt8();
                uint32 mapId         = fields[2].GetUInt16();
                uint32 areaId        = fields[3].GetUInt32(); // zone
                float  positionX     = fields[4].GetFloat();
                float  positionY     = fields[5].GetFloat();
                float  positionZ     = fields[6].GetFloat();
                float  orientation   = fields[7].GetFloat();

                if (current_race >= MAX_RACES)
                {
                    TC_LOG_ERROR("sql.sql", "Wrong race {} in `playercreateinfo` table, ignoring.", current_race);
                    continue;
                }

                ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(current_race);
                if (!rEntry)
                {
                    TC_LOG_ERROR("sql.sql", "Wrong race {} in `playercreateinfo` table, ignoring.", current_race);
                    continue;
                }

                if (current_class >= MAX_CLASSES)
                {
                    TC_LOG_ERROR("sql.sql", "Wrong class {} in `playercreateinfo` table, ignoring.", current_class);
                    continue;
                }

                if (!sChrClassesStore.LookupEntry(current_class))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong class {} in `playercreateinfo` table, ignoring.", current_class);
                    continue;
                }

                // accept DB data only for valid position (and non instanceable)
                if (!MapManager::IsValidMapCoord(mapId, positionX, positionY, positionZ, orientation))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong home position for class {} race {} pair in `playercreateinfo` table, ignoring.", current_class, current_race);
                    continue;
                }

                if (sMapStore.LookupEntry(mapId)->Instanceable())
                {
                    TC_LOG_ERROR("sql.sql", "Home position in instanceable map for class {} race {} pair in `playercreateinfo` table, ignoring.", current_class, current_race);
                    continue;
                }

                std::unique_ptr<PlayerInfo> info = std::make_unique<PlayerInfo>();
                info->mapId = mapId;
                info->areaId = areaId;
                info->positionX = positionX;
                info->positionY = positionY;
                info->positionZ = positionZ;
                info->orientation = orientation;
                info->displayId_m = rEntry->MaleDisplayID;
                info->displayId_f = rEntry->FemaleDisplayID;
                _playerInfo[current_race][current_class] = std::move(info);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} player create definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Load playercreate items
    TC_LOG_INFO("server.loading", "Loading Player Create Items Data...");
    {
        uint32 oldMSTime = getMSTime();
        //                                                0     1      2       3
        QueryResult result = WorldDatabase.Query("SELECT race, class, itemid, amount FROM playercreateinfo_item");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 custom player create items. DB table `playercreateinfo_item` is empty.");
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();

                uint32 current_race = fields[0].GetUInt8();
                if (current_race >= MAX_RACES)
                {
                    TC_LOG_ERROR("sql.sql", "Wrong race {} in `playercreateinfo_item` table, ignoring.", current_race);
                    continue;
                }

                uint32 current_class = fields[1].GetUInt8();
                if (current_class >= MAX_CLASSES)
                {
                    TC_LOG_ERROR("sql.sql", "Wrong class {} in `playercreateinfo_item` table, ignoring.", current_class);
                    continue;
                }

                uint32 item_id = fields[2].GetUInt32();

                if (!GetItemTemplate(item_id))
                {
                    TC_LOG_ERROR("sql.sql", "Item id {} (race {} class {}) in `playercreateinfo_item` table but not listed in `item_template`, ignoring.", item_id, current_race, current_class);
                    continue;
                }

                int32 amount   = fields[3].GetInt8();

                if (!amount)
                {
                    TC_LOG_ERROR("sql.sql", "Item id {} (class {} race {}) have amount == 0 in `playercreateinfo_item` table, ignoring.", item_id, current_race, current_class);
                    continue;
                }

                if (!current_race || !current_class)
                {
                    uint32 min_race = current_race ? current_race : 1;
                    uint32 max_race = current_race ? current_race + 1 : MAX_RACES;
                    uint32 min_class = current_class ? current_class : 1;
                    uint32 max_class = current_class ? current_class + 1 : MAX_CLASSES;
                    for (uint32 r = min_race; r < max_race; ++r)
                        for (uint32 c = min_class; c < max_class; ++c)
                            PlayerCreateInfoAddItemHelper(r, c, item_id, amount);
                }
                else
                    PlayerCreateInfoAddItemHelper(current_race, current_class, item_id, amount);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} custom player create items in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Load playercreate skills
    TC_LOG_INFO("server.loading", "Loading Player Create Skill Data...");
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.PQuery("SELECT raceMask, classMask, skill, `rank` FROM playercreateinfo_skills");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 player create skills. DB table `playercreateinfo_skills` is empty.");
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();
                uint32 raceMask = fields[0].GetUInt32();
                uint32 classMask = fields[1].GetUInt32();
                PlayerCreateInfoSkill skill;
                skill.SkillId = fields[2].GetUInt16();
                skill.Rank = fields[3].GetUInt16();

                if (skill.Rank >= MAX_SKILL_STEP)
                {
                    TC_LOG_ERROR("sql.sql", "Skill rank value {} set for skill {} raceMask {} classMask {} is too high, max allowed value is {}", skill.Rank, skill.SkillId, raceMask, classMask, MAX_SKILL_STEP);
                    continue;
                }

                if (raceMask != 0 && !(raceMask & RACEMASK_ALL_PLAYABLE))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong race mask {} in `playercreateinfo_skills` table, ignoring.", raceMask);
                    continue;
                }

                if (classMask != 0 && !(classMask & CLASSMASK_ALL_PLAYABLE))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong class mask {} in `playercreateinfo_skills` table, ignoring.", classMask);
                    continue;
                }

                if (!sSkillLineStore.LookupEntry(skill.SkillId))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong skill id {} in `playercreateinfo_skills` table, ignoring.", skill.SkillId);
                    continue;
                }

                for (uint32 raceIndex = RACE_HUMAN; raceIndex < MAX_RACES; ++raceIndex)
                {
                    if (raceMask == 0 || ((1 << (raceIndex - 1)) & raceMask))
                    {
                        for (uint32 classIndex = CLASS_WARRIOR; classIndex < MAX_CLASSES; ++classIndex)
                        {
                            if (classMask == 0 || ((1 << (classIndex - 1)) & classMask))
                            {
                                if (!GetSkillRaceClassInfo(skill.SkillId, raceIndex, classIndex))
                                    continue;

                                if (auto& info = _playerInfo[raceIndex][classIndex])
                                {
                                    info->skills.push_back(skill);
                                    ++count;
                                }
                            }
                        }
                    }
                }
            } while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} player create skills in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Load playercreate spells
    TC_LOG_INFO("server.loading", "Loading Player Create Spell Data...");
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.PQuery("SELECT racemask, classmask, Spell FROM playercreateinfo_spell_custom");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 player create spells. DB table `playercreateinfo_spell_custom` is empty.");
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();
                uint32 raceMask = fields[0].GetUInt32();
                uint32 classMask = fields[1].GetUInt32();
                uint32 spellId = fields[2].GetUInt32();

                if (raceMask != 0 && !(raceMask & RACEMASK_ALL_PLAYABLE))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong race mask {} in `playercreateinfo_spell_custom` table, ignoring.", raceMask);
                    continue;
                }

                if (classMask != 0 && !(classMask & CLASSMASK_ALL_PLAYABLE))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong class mask {} in `playercreateinfo_spell_custom` table, ignoring.", classMask);
                    continue;
                }

                for (uint32 raceIndex = RACE_HUMAN; raceIndex < MAX_RACES; ++raceIndex)
                {
                    if (raceMask == 0 || ((1 << (raceIndex - 1)) & raceMask))
                    {
                        for (uint32 classIndex = CLASS_WARRIOR; classIndex < MAX_CLASSES; ++classIndex)
                        {
                            if (classMask == 0 || ((1 << (classIndex - 1)) & classMask))
                            {
                                if (auto& info = _playerInfo[raceIndex][classIndex])
                                {
                                    info->customSpells.push_back(spellId);
                                    ++count;
                                }
                                // We need something better here, the check is not accounting for spells used by multiple races/classes but not all of them.
                                // Either split the masks per class, or per race, which kind of kills the point yet.
                                // else if (raceMask != 0 && classMask != 0)
                                //     TC_LOG_ERROR("sql.sql", "Racemask/classmask ({}/{}) combination was found containing an invalid race/class combination ({}/{}) in `{}` (Spell {}), ignoring.", raceMask, classMask, raceIndex, classIndex, tableName, spellId);
                            }
                        }
                    }
                }
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} custom player create spells in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Load playercreate cast spell
    TC_LOG_INFO("server.loading", "Loading Player Create Cast Spell Data...");
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.PQuery("SELECT raceMask, classMask, spell FROM playercreateinfo_cast_spell");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 player create cast spells. DB table `playercreateinfo_cast_spell` is empty.");
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields       = result->Fetch();
                uint32 raceMask     = fields[0].GetUInt32();
                uint32 classMask    = fields[1].GetUInt32();
                uint32 spellId      = fields[2].GetUInt32();

                if (raceMask != 0 && !(raceMask & RACEMASK_ALL_PLAYABLE))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong race mask {} in `playercreateinfo_cast_spell` table, ignoring.", raceMask);
                    continue;
                }

                if (classMask != 0 && !(classMask & CLASSMASK_ALL_PLAYABLE))
                {
                    TC_LOG_ERROR("sql.sql", "Wrong class mask {} in `playercreateinfo_cast_spell` table, ignoring.", classMask);
                    continue;
                }

                for (uint32 raceIndex = RACE_HUMAN; raceIndex < MAX_RACES; ++raceIndex)
                {
                    if (raceMask == 0 || ((1 << (raceIndex - 1)) & raceMask))
                    {
                        for (uint32 classIndex = CLASS_WARRIOR; classIndex < MAX_CLASSES; ++classIndex)
                        {
                            if (classMask == 0 || ((1 << (classIndex - 1)) & classMask))
                            {
                                if (auto& info = _playerInfo[raceIndex][classIndex])
                                {
                                    info->castSpells.push_back(spellId);
                                    ++count;
                                }
                            }
                        }
                    }
                }
            } while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} player create cast spells in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Load playercreate actions
    TC_LOG_INFO("server.loading", "Loading Player Create Action Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                0     1      2       3       4
        QueryResult result = WorldDatabase.Query("SELECT race, class, button, action, type FROM playercreateinfo_action");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 player create actions. DB table `playercreateinfo_action` is empty.");
        }
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();

                uint32 current_race = fields[0].GetUInt8();
                if (current_race >= MAX_RACES)
                {
                    TC_LOG_ERROR("sql.sql", "Wrong race {} in `playercreateinfo_action` table, ignoring.", current_race);
                    continue;
                }

                uint32 current_class = fields[1].GetUInt8();
                if (current_class >= MAX_CLASSES)
                {
                    TC_LOG_ERROR("sql.sql", "Wrong class {} in `playercreateinfo_action` table, ignoring.", current_class);
                    continue;
                }

                if (auto& info = _playerInfo[current_race][current_class])
                    info->action.push_back(PlayerCreateInfoAction(fields[2].GetUInt16(), fields[3].GetUInt32(), fields[4].GetUInt16()));

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} player create actions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // Loading levels data (class only dependent)
    TC_LOG_INFO("server.loading", "Loading Player Create Level HP/Mana Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                0      1      2       3
        QueryResult result  = WorldDatabase.Query("SELECT class, level, basehp, basemana FROM player_classlevelstats");

        if (!result)
        {
            TC_LOG_ERROR("server.loading", ">> Loaded 0 level health/mana definitions. DB table `player_classlevelstats` is empty.");
            ABORT();
        }

        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();

            uint32 current_class = fields[0].GetUInt8();
            if (current_class >= MAX_CLASSES)
            {
                TC_LOG_ERROR("sql.sql", "Wrong class {} in `player_classlevelstats` table, ignoring.", current_class);
                continue;
            }

            uint8 current_level = fields[1].GetUInt8();      // Can't be > than STRONG_MAX_LEVEL (hardcoded level maximum) due to var type
            if (current_level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                TC_LOG_INFO("misc", "Unused (> MaxPlayerLevel in worldserver.conf) level {} in `player_classlevelstats` table, ignoring.", current_level);
                ++count;                                    // make result loading percent "expected" correct in case disabled detail mode for example.
                continue;
            }

            auto& info = _playerClassInfo[current_class];
            if (!info)
            {
                info = std::make_unique<PlayerClassInfo>();
                info->levelInfo = std::make_unique<PlayerClassLevelInfo[]>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
            }

            PlayerClassLevelInfo& levelInfo = info->levelInfo[current_level - 1];
            levelInfo.basehealth = fields[2].GetUInt16();
            levelInfo.basemana   = fields[3].GetUInt16();

            ++count;
        }
        while (result->NextRow());

        // Fill gaps and check integrity
        for (uint8 class_ = 0; class_ < MAX_CLASSES; ++class_)
        {
            // skip non existed classes
            if (!sChrClassesStore.LookupEntry(class_))
                continue;

            auto& pClassInfo = _playerClassInfo[class_];

            // fatal error if no level 1 data
            if (!pClassInfo || !pClassInfo->levelInfo || pClassInfo->levelInfo[0].basehealth == 0)
            {
                TC_LOG_ERROR("sql.sql", "Class {} Level 1 does not have health/mana data!", class_);
                ABORT();
            }

            // fill level gaps
            for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
            {
                if (pClassInfo->levelInfo[level].basehealth == 0)
                {
                    TC_LOG_ERROR("sql.sql", "Class {} Level {} does not have health/mana data. Using stats data of level {}.", class_, level + 1, level);
                    pClassInfo->levelInfo[level] = pClassInfo->levelInfo[level - 1];
                }
            }
        }

        TC_LOG_INFO("server.loading", ">> Loaded {} level health/mana definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }

    // Loading levels data (class/race dependent)
    TC_LOG_INFO("server.loading", "Loading Player Create Level Stats Data...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                 0     1      2      3    4    5    6    7
        QueryResult result  = WorldDatabase.Query("SELECT race, class, level, str, agi, sta, inte, spi FROM player_levelstats");

        if (!result)
        {
            TC_LOG_ERROR("server.loading", ">> Loaded 0 level stats definitions. DB table `player_levelstats` is empty.");
            ABORT();
        }

        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();

            uint32 current_race = fields[0].GetUInt8();
            if (current_race >= MAX_RACES)
            {
                TC_LOG_ERROR("sql.sql", "Wrong race {} in `player_levelstats` table, ignoring.", current_race);
                continue;
            }

            uint32 current_class = fields[1].GetUInt8();
            if (current_class >= MAX_CLASSES)
            {
                TC_LOG_ERROR("sql.sql", "Wrong class {} in `player_levelstats` table, ignoring.", current_class);
                continue;
            }

            uint32 current_level = fields[2].GetUInt8();
            if (current_level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                if (current_level > STRONG_MAX_LEVEL)        // hardcoded level maximum
                    TC_LOG_ERROR("sql.sql", "Wrong (> {}) level {} in `player_levelstats` table, ignoring.", STRONG_MAX_LEVEL, current_level);
                else
                {
                    TC_LOG_INFO("misc", "Unused (> MaxPlayerLevel in worldserver.conf) level {} in `player_levelstats` table, ignoring.", current_level);
                    ++count;                                // make result loading percent "expected" correct in case disabled detail mode for example.
                }
                continue;
            }

            if (auto& info = _playerInfo[current_race][current_class])
            {
                if (!info->levelInfo)
                    info->levelInfo = std::make_unique<PlayerLevelInfo[]>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));

                PlayerLevelInfo& levelInfo = info->levelInfo[current_level - 1];
                for (uint8 i = 0; i < MAX_STATS; ++i)
                    levelInfo.stats[i] = fields[i + 3].GetUInt8();
            }

            ++count;
        }
        while (result->NextRow());

        // Fill gaps and check integrity
        for (uint8 race = 0; race < MAX_RACES; ++race)
        {
            // skip non existed races
            if (!sChrRacesStore.LookupEntry(race))
                continue;

            for (uint8 class_ = 0; class_ < MAX_CLASSES; ++class_)
            {
                // skip non existed classes
                if (!sChrClassesStore.LookupEntry(class_))
                    continue;

                auto& info = _playerInfo[race][class_];
                if (!info)
                    continue;

                // skip expansion races if not playing with expansion
                if (sWorld->getIntConfig(CONFIG_EXPANSION) < EXPANSION_THE_BURNING_CRUSADE && (race == RACE_BLOODELF || race == RACE_DRAENEI))
                    continue;

                // skip expansion classes if not playing with expansion
                if (sWorld->getIntConfig(CONFIG_EXPANSION) < EXPANSION_WRATH_OF_THE_LICH_KING && class_ == CLASS_DEATH_KNIGHT)
                    continue;

                // fatal error if no level 1 data
                if (!info->levelInfo || info->levelInfo[0].stats[0] == 0)
                {
                    TC_LOG_ERROR("sql.sql", "Race {} Class {} Level 1 does not have stats data!", race, class_);
                    ABORT();
                }

                // fill level gaps
                for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
                {
                    if (info->levelInfo[level].stats[0] == 0)
                    {
                        TC_LOG_ERROR("sql.sql", "Race {} Class {} Level {} does not have stats data. Using stats data of level {}.", race, class_, level + 1, level);
                        info->levelInfo[level] = info->levelInfo[level - 1];
                    }
                }
            }
        }

        TC_LOG_INFO("server.loading", ">> Loaded {} level stats definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }

    // Loading xp per level data
    TC_LOG_INFO("server.loading", "Loading Player Create XP Data...");
    {
        uint32 oldMSTime = getMSTime();

        _playerXPperLevel.resize(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
        for (uint8 level = 0; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
            _playerXPperLevel[level] = 0;

        //                                                  0         1
        QueryResult result  = WorldDatabase.Query("SELECT Level, Experience FROM player_xp_for_level");

        if (!result)
        {
            TC_LOG_ERROR("server.loading", ">> Loaded 0 xp for level definitions. DB table `player_xp_for_level` is empty.");
            ABORT();
        }

        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();

            uint32 current_level = fields[0].GetUInt8();
            uint32 current_xp    = fields[1].GetUInt32();

            if (current_level >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                if (current_level > STRONG_MAX_LEVEL)        // hardcoded level maximum
                    TC_LOG_ERROR("sql.sql", "Wrong (> {}) level {} in `player_xp_for_level` table, ignoring.", STRONG_MAX_LEVEL, current_level);
                else
                {
                    TC_LOG_INFO("misc", "Unused (> MaxPlayerLevel in worldserver.conf) level {} in `player_xp_for_levels` table, ignoring.", current_level);
                    ++count;                                // make result loading percent "expected" correct in case disabled detail mode for example.
                }
                continue;
            }
            //PlayerXPperLevel
            _playerXPperLevel[current_level] = current_xp;
            ++count;
        }
        while (result->NextRow());

        // fill level gaps
        for (uint8 level = 1; level < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL); ++level)
        {
            if (_playerXPperLevel[level] == 0)
            {
                TC_LOG_ERROR("sql.sql", "Level {} does not have XP for level data. Using data of level [{}] + 100.", level + 1, level);
                _playerXPperLevel[level] = _playerXPperLevel[level - 1] + 100;
            }
        }

        TC_LOG_INFO("server.loading", ">> Loaded {} xp for level definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }
}

void ObjectMgr::GetPlayerClassLevelInfo(uint32 class_, uint8 level, PlayerClassLevelInfo* info) const
{
    if (level < 1 || class_ >= MAX_CLASSES)
        return;

    auto const& pInfo = _playerClassInfo[class_];

    if (level > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        level = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    *info = pInfo->levelInfo[level - 1];
}

void ObjectMgr::GetPlayerLevelInfo(uint32 race, uint32 class_, uint8 level, PlayerLevelInfo* info) const
{
    if (level < 1 || race >= MAX_RACES || class_ >= MAX_CLASSES)
        return;

    auto const& pInfo = _playerInfo[race][class_];
    if (!pInfo)
        return;

    if (level <= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        *info = pInfo->levelInfo[level - 1];
    else
        BuildPlayerLevelInfo(race, class_, level, info);
}

void ObjectMgr::BuildPlayerLevelInfo(uint8 race, uint8 _class, uint8 level, PlayerLevelInfo* info) const
{
    // base data (last known level)
    *info = _playerInfo[race][_class]->levelInfo[sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL) - 1];

    // if conversion from uint32 to uint8 causes unexpected behaviour, change lvl to uint32
    for (uint8 lvl = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL) - 1; lvl < level; ++lvl)
    {
        switch (_class)
        {
            case CLASS_WARRIOR:
                info->stats[STAT_STRENGTH]  += (lvl > 23 ? 2: (lvl > 1  ? 1: 0));
                info->stats[STAT_STAMINA]   += (lvl > 23 ? 2: (lvl > 1  ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 36 ? 1: (lvl > 6 && (lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 9 && !(lvl%2) ? 1: 0);
                break;
            case CLASS_PALADIN:
                info->stats[STAT_STRENGTH]  += (lvl > 3  ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 33 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 38 ? 1: (lvl > 7 && !(lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 6 && (lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 7 ? 1: 0);
                break;
            case CLASS_HUNTER:
                info->stats[STAT_STRENGTH]  += (lvl > 4  ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 4  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 33 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 8 && (lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 1: (lvl > 9 && !(lvl%2) ? 1: 0));
                break;
            case CLASS_ROGUE:
                info->stats[STAT_STRENGTH]  += (lvl > 5  ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 4  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 16 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 8 && !(lvl%2) ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 1: (lvl > 9 && !(lvl%2) ? 1: 0));
                break;
            case CLASS_PRIEST:
                info->stats[STAT_STRENGTH]  += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 5  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 38 ? 1: (lvl > 8 && (lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 22 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 3  ? 1: 0);
                break;
            case CLASS_SHAMAN:
                info->stats[STAT_STRENGTH]  += (lvl > 34 ? 1: (lvl > 6 && (lvl%2) ? 1: 0));
                info->stats[STAT_STAMINA]   += (lvl > 4 ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 7 && !(lvl%2) ? 1: 0);
                info->stats[STAT_INTELLECT] += (lvl > 5 ? 1: 0);
                info->stats[STAT_SPIRIT]    += (lvl > 4 ? 1: 0);
                break;
            case CLASS_MAGE:
                info->stats[STAT_STRENGTH]  += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 5  ? 1: 0);
                info->stats[STAT_AGILITY]   += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_INTELLECT] += (lvl > 24 ? 2: (lvl > 1 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 33 ? 2: (lvl > 2 ? 1: 0));
                break;
            case CLASS_WARLOCK:
                info->stats[STAT_STRENGTH]  += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_STAMINA]   += (lvl > 38 ? 2: (lvl > 3 ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 9 && !(lvl%2) ? 1: 0);
                info->stats[STAT_INTELLECT] += (lvl > 33 ? 2: (lvl > 2 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 2: (lvl > 3 ? 1: 0));
                break;
            case CLASS_DRUID:
                info->stats[STAT_STRENGTH]  += (lvl > 38 ? 2: (lvl > 6 && (lvl%2) ? 1: 0));
                info->stats[STAT_STAMINA]   += (lvl > 32 ? 2: (lvl > 4 ? 1: 0));
                info->stats[STAT_AGILITY]   += (lvl > 38 ? 2: (lvl > 8 && (lvl%2) ? 1: 0));
                info->stats[STAT_INTELLECT] += (lvl > 38 ? 3: (lvl > 4 ? 1: 0));
                info->stats[STAT_SPIRIT]    += (lvl > 38 ? 3: (lvl > 5 ? 1: 0));
        }
    }
}

void ObjectMgr::LoadQuests()
{
    uint32 oldMSTime = getMSTime();

    _questTemplates.clear();

    _exclusiveQuestGroups.clear();

    QueryResult result = WorldDatabase.Query("SELECT "
        //0      1           2         3           4            5                6              7             8
        "ID, QuestType, QuestLevel, MinLevel, QuestSortID, QuestInfoID, SuggestedGroupNum, TimeAllowed, AllowableRaces,"
        //      9                     10                   11                    12
        "RequiredFactionId1, RequiredFactionId2, RequiredFactionValue1, RequiredFactionValue2, "
        //     13                 14               15             16                17               18            19            20
        "RewardNextQuest, RewardXPDifficulty, RewardMoney, RewardBonusMoney, RewardDisplaySpell, RewardSpell, RewardHonor, RewardKillHonor, "
        //   21       22        23              24                25               26
        "StartItem, Flags, RewardTitle, RequiredPlayerKills, RewardTalents, RewardArenaPoints, "
        //    27            28            29           30             31            32            33            34
        "RewardItem1, RewardAmount1, RewardItem2, RewardAmount2, RewardItem3, RewardAmount3, RewardItem4, RewardAmount4, "
        //        35                      36                      37                      38                      39                      40                      41                      42                      43                      44                     45                      46
        "RewardChoiceItemID1, RewardChoiceItemQuantity1, RewardChoiceItemID2, RewardChoiceItemQuantity2, RewardChoiceItemID3, RewardChoiceItemQuantity3, RewardChoiceItemID4, RewardChoiceItemQuantity4, RewardChoiceItemID5, RewardChoiceItemQuantity5, RewardChoiceItemID6, RewardChoiceItemQuantity6, "
        //       47                 48                     49                  50                  51                     52                 53                  54                     55                  56                  57                    58                   59                 60                      61
        "RewardFactionID1, RewardFactionValue1, RewardFactionOverride1, RewardFactionID2, RewardFactionValue2, RewardFactionOverride2, RewardFactionID3, RewardFactionValue3, RewardFactionOverride3, RewardFactionID4, RewardFactionValue4, RewardFactionOverride4, RewardFactionID5, RewardFactionValue5,  RewardFactionOverride5,"
        //    62        63    64       65
        "POIContinent, POIx, POIy, POIPriority, "
        //   66          67               68                69                70
        "LogTitle, LogDescription, QuestDescription, AreaDescription, QuestCompletionLog, "
        //      71                72                73                74                   75                     76                    77                      78
        "RequiredNpcOrGo1, RequiredNpcOrGo2, RequiredNpcOrGo3, RequiredNpcOrGo4, RequiredNpcOrGoCount1, RequiredNpcOrGoCount2, RequiredNpcOrGoCount3, RequiredNpcOrGoCount4, "
        //   79         80         81         82            83                 84                  85                86
        "ItemDrop1, ItemDrop2, ItemDrop3, ItemDrop4, ItemDropQuantity1, ItemDropQuantity2, ItemDropQuantity3, ItemDropQuantity4, "
        //      87               88               89               90               91               92                93                  94                  95                  96                  97                  98
        "RequiredItemId1, RequiredItemId2, RequiredItemId3, RequiredItemId4, RequiredItemId5, RequiredItemId6, RequiredItemCount1, RequiredItemCount2, RequiredItemCount3, RequiredItemCount4, RequiredItemCount5, RequiredItemCount6, "
        //  99          100             101             102             103
        "Unknown0, ObjectiveText1, ObjectiveText2, ObjectiveText3, ObjectiveText4"
        " FROM quest_template");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 quests definitions. DB table `quest_template` is empty.");
        return;
    }

    _questTemplates.reserve(result->GetRowCount());

    // create multimap previous quest for each existed quest
    // some quests can have many previous maps set by NextQuestId in previous quest
    // for example set of race quests can lead to single not race specific quest
    do
    {
        Field* fields = result->Fetch();

        uint32 questId = fields[0].GetUInt32();
        auto itr = _questTemplates.emplace(std::piecewise_construct, std::forward_as_tuple(questId), std::forward_as_tuple(new Quest(fields))).first;
        itr->second->_weakRef = itr->second;
    } while (result->NextRow());

    std::unordered_map<uint32, uint32> usedMailTemplates;

    struct QuestLoaderHelper
    {
        typedef void(Quest::*QuestLoaderFunction)(Field* fields);

        char const* QueryFields;
        char const* TableName;
        char const* TableDesc;
        QuestLoaderFunction LoaderFunction;
    };

    static std::vector<QuestLoaderHelper> const QuestLoaderHelpers =
    {
        // 0   1       2       3       4       5            6            7            8
        { "ID, Emote1, Emote2, Emote3, Emote4, EmoteDelay1, EmoteDelay2, EmoteDelay3, EmoteDelay4",                                                                       "quest_details",        "details",             &Quest::LoadQuestDetails       },

        // 0   1                2                  3
        { "ID, EmoteOnComplete, EmoteOnIncomplete, CompletionText",                                                                                                       "quest_request_items",  "request items",       &Quest::LoadQuestRequestItems  },

        // 0   1       2       3       4       5            6            7            8            9
        { "ID, Emote1, Emote2, Emote3, Emote4, EmoteDelay1, EmoteDelay2, EmoteDelay3, EmoteDelay4, RewardText",                                                           "quest_offer_reward",   "reward emotes",       &Quest::LoadQuestOfferReward   },

        // 0   1         2                 3              4            5            6               7                     8                     9
        { "ID, MaxLevel, AllowableClasses, SourceSpellID, PrevQuestID, NextQuestID, ExclusiveGroup, BreadcrumbForQuestId, RewardMailTemplateID, RewardMailDelay,"
        // 10              11                   12                     13                     14                   15                   16                 17
        " RequiredSkillID, RequiredSkillPoints, RequiredMinRepFaction, RequiredMaxRepFaction, RequiredMinRepValue, RequiredMaxRepValue, ProvidedItemCount, SpecialFlags", "quest_template_addon", "template addons",     &Quest::LoadQuestTemplateAddon },

        // 0        1
        { "QuestId, RewardMailSenderEntry",                                                                                                                               "quest_mail_sender",    "mail sender entries", &Quest::LoadQuestMailSender    }
    };

    for (QuestLoaderHelper const& loader : QuestLoaderHelpers)
    {
        result = WorldDatabase.PQuery("SELECT {} FROM {}", loader.QueryFields, loader.TableName);
        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 quest {}. DB table `{}` is empty.", loader.TableDesc, loader.TableName);
        else
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 questId = fields[0].GetUInt32();

                auto itr = _questTemplates.find(questId);
                if (itr != _questTemplates.end())
                    (itr->second.get()->*loader.LoaderFunction)(fields);
                else
                    TC_LOG_ERROR("server.loading", "Table `{}` has data for quest {} but such quest does not exist", loader.TableName, questId);
            } while (result->NextRow());
        }
    }

    // Post processing
    for (auto& questPair : _questTemplates)
    {
        // skip post-loading checks for disabled quests
        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_QUEST, questPair.first, nullptr))
            continue;

        Quest* qinfo = questPair.second.get();

        // additional quest integrity checks (GO, creature_template and item_template must be loaded already)

        if (qinfo->GetQuestMethod() >= 3)
            TC_LOG_ERROR("sql.sql", "Quest {} has `Method` = {}, expected values are 0, 1 or 2.", qinfo->GetQuestId(), qinfo->GetQuestMethod());

        if (qinfo->_specialFlags & ~QUEST_SPECIAL_FLAGS_DB_ALLOWED)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `SpecialFlags` = {} > max allowed value. Correct `SpecialFlags` to value <= {}",
                qinfo->GetQuestId(), qinfo->_specialFlags, QUEST_SPECIAL_FLAGS_DB_ALLOWED);
            qinfo->_specialFlags &= QUEST_SPECIAL_FLAGS_DB_ALLOWED;
        }

        if (qinfo->_flags & QUEST_FLAGS_DAILY && qinfo->_flags & QUEST_FLAGS_WEEKLY)
        {
            TC_LOG_ERROR("sql.sql", "Weekly Quest {} is marked as daily quest in `Flags`, removed daily flag.", qinfo->GetQuestId());
            qinfo->_flags &= ~QUEST_FLAGS_DAILY;
        }

        if (qinfo->_flags & QUEST_FLAGS_DAILY)
        {
            if (!(qinfo->_specialFlags & QUEST_SPECIAL_FLAGS_REPEATABLE))
            {
                TC_LOG_ERROR("sql.sql", "Daily Quest {} not marked as repeatable in `SpecialFlags`, added.", qinfo->GetQuestId());
                qinfo->_specialFlags |= QUEST_SPECIAL_FLAGS_REPEATABLE;
            }
        }

        if (qinfo->_flags & QUEST_FLAGS_WEEKLY)
        {
            if (!(qinfo->_specialFlags & QUEST_SPECIAL_FLAGS_REPEATABLE))
            {
                TC_LOG_ERROR("sql.sql", "Weekly Quest {} not marked as repeatable in `SpecialFlags`, added.", qinfo->GetQuestId());
                qinfo->_specialFlags |= QUEST_SPECIAL_FLAGS_REPEATABLE;
            }
        }

        if (qinfo->_specialFlags & QUEST_SPECIAL_FLAGS_MONTHLY)
        {
            if (!(qinfo->_specialFlags & QUEST_SPECIAL_FLAGS_REPEATABLE))
            {
                TC_LOG_ERROR("sql.sql", "Monthly quest {} not marked as repeatable in `SpecialFlags`, added.", qinfo->GetQuestId());
                qinfo->_specialFlags |= QUEST_SPECIAL_FLAGS_REPEATABLE;
            }
        }

        if (qinfo->_flags & QUEST_FLAGS_TRACKING)
        {
            // at auto-reward can be rewarded only RewardChoiceItemId[0]
            for (uint32 j = 1; j < QUEST_REWARD_CHOICES_COUNT; ++j)
            {
                if (uint32 id = qinfo->RewardChoiceItemId[j])
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RewardChoiceItemId{}` = {} but item from `RewardChoiceItemId{}` can't be rewarded with quest flag QUEST_FLAGS_TRACKING.",
                        qinfo->GetQuestId(), j + 1, id, j + 1);
                    // no changes, quest ignore this data
                }
            }
        }

        // client quest log visual (area case)
        if (qinfo->_zoneOrSort > 0)
        {
            if (!sAreaTableStore.LookupEntry(qinfo->_zoneOrSort))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `ZoneOrSort` = {} (zone case) but zone with this id does not exist.",
                    qinfo->GetQuestId(), qinfo->_zoneOrSort);
                // no changes, quest not dependent from this value but can have problems at client
            }
        }
        // client quest log visual (sort case)
        if (qinfo->_zoneOrSort < 0)
        {
            QuestSortEntry const* qSort = sQuestSortStore.LookupEntry(-int32(qinfo->_zoneOrSort));
            if (!qSort)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `ZoneOrSort` = {} (sort case) but quest sort with this id does not exist.",
                    qinfo->GetQuestId(), qinfo->_zoneOrSort);
                // no changes, quest not dependent from this value but can have problems at client (note some may be 0, we must allow this so no check)
            }
            //check for proper RequiredSkillId value (skill case)
            if (uint32 skill_id = SkillByQuestSort(-qinfo->_zoneOrSort))
            {
                if (qinfo->_requiredSkillId != skill_id)
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `ZoneOrSort` = {} but `RequiredSkillId` does not have a corresponding value ({}).",
                        qinfo->GetQuestId(), qinfo->_zoneOrSort, skill_id);
                    //override, and force proper value here?
                }
            }
        }

        // RequiredClasses, can be 0/CLASSMASK_ALL_PLAYABLE to allow any class
        if (qinfo->_requiredClasses)
        {
            if (!(qinfo->_requiredClasses & CLASSMASK_ALL_PLAYABLE))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} does not contain any playable classes in `RequiredClasses` ({}), value set to 0 (all classes).", qinfo->GetQuestId(), qinfo->_requiredClasses);
                    qinfo->_requiredClasses = 0;
            }
        }

        // AllowableRaces, can be 0/RACEMASK_ALL_PLAYABLE to allow any race
        if (qinfo->_allowableRaces)
        {
            if (!(qinfo->_allowableRaces & RACEMASK_ALL_PLAYABLE))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} does not contain any playable races in `AllowableRaces` ({}), value set to 0 (all races).", qinfo->GetQuestId(), qinfo->_allowableRaces);
                qinfo->_allowableRaces = 0;
            }
        }

        // RequiredSkillId, can be 0
        if (qinfo->_requiredSkillId)
        {
            if (!sSkillLineStore.LookupEntry(qinfo->_requiredSkillId))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredSkillId` = {} but this skill does not exist",
                    qinfo->GetQuestId(), qinfo->_requiredSkillId);
            }
        }

        if (qinfo->_requiredSkillPoints)
        {
            if (qinfo->_requiredSkillPoints > sWorld->GetConfigMaxSkillValue())
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredSkillPoints` = {} but max possible skill is {}, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->_requiredSkillPoints, sWorld->GetConfigMaxSkillValue());
                // no changes, quest can't be done for this requirement
            }
        }
        // else Skill quests can have 0 skill level, this is ok

        if (qinfo->_requiredFactionId2 && !sFactionStore.LookupEntry(qinfo->_requiredFactionId2))
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredFactionId2` = {} but faction template {} does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->_requiredFactionId2, qinfo->_requiredFactionId2);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->_requiredFactionId1 && !sFactionStore.LookupEntry(qinfo->_requiredFactionId1))
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredFactionId1` = {} but faction template {} does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->_requiredFactionId1, qinfo->_requiredFactionId1);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->_requiredMinRepFaction && !sFactionStore.LookupEntry(qinfo->_requiredMinRepFaction))
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredMinRepFaction` = {} but faction template {} does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->_requiredMinRepFaction, qinfo->_requiredMinRepFaction);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->_requiredMaxRepFaction && !sFactionStore.LookupEntry(qinfo->_requiredMaxRepFaction))
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredMaxRepFaction` = {} but faction template {} does not exist, quest can't be done.",
                qinfo->GetQuestId(), qinfo->_requiredMaxRepFaction, qinfo->_requiredMaxRepFaction);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->_requiredMinRepValue && qinfo->_requiredMinRepValue > ReputationMgr::Reputation_Cap)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredMinRepValue` = {} but max reputation is {}, quest can't be done.",
                qinfo->GetQuestId(), qinfo->_requiredMinRepValue, ReputationMgr::Reputation_Cap);
            // no changes, quest can't be done for this requirement
        }

        if (qinfo->_requiredMinRepValue && qinfo->_requiredMaxRepValue && qinfo->_requiredMaxRepValue <= qinfo->_requiredMinRepValue)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredMaxRepValue` = {} and `RequiredMinRepValue` = {}, quest can't be done.",
                qinfo->GetQuestId(), qinfo->_requiredMaxRepValue, qinfo->_requiredMinRepValue);
            // no changes, quest can't be done for this requirement
        }

        if (!qinfo->_requiredFactionId1 && qinfo->_requiredFactionValue1 != 0)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredFactionValue1` = {} but `RequiredFactionId1` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->_requiredFactionValue1);
            // warning
        }

        if (!qinfo->_requiredFactionId2 && qinfo->_requiredFactionValue2 != 0)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredFactionValue2` = {} but `RequiredFactionId2` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->_requiredFactionValue2);
            // warning
        }

        if (!qinfo->_requiredMinRepFaction && qinfo->_requiredMinRepValue != 0)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredMinRepValue` = {} but `RequiredMinRepFaction` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->_requiredMinRepValue);
            // warning
        }

        if (!qinfo->_requiredMaxRepFaction && qinfo->_requiredMaxRepValue != 0)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredMaxRepValue` = {} but `RequiredMaxRepFaction` is 0, value has no effect",
                qinfo->GetQuestId(), qinfo->_requiredMaxRepValue);
            // warning
        }

        if (qinfo->_rewardTitleId && !sCharTitlesStore.LookupEntry(qinfo->_rewardTitleId))
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `RewardTitleId` = {} but CharTitle Id {} does not exist, quest can't be rewarded with title.",
                qinfo->GetQuestId(), qinfo->GetCharTitleId(), qinfo->GetCharTitleId());
            qinfo->_rewardTitleId = 0;
            // quest can't reward this title
        }

        if (qinfo->_startItem)
        {
            if (!sObjectMgr->GetItemTemplate(qinfo->_startItem))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `StartItem` = {} but item with entry {} does not exist, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->_startItem, qinfo->_startItem);
                qinfo->_startItem = 0;                       // quest can't be done for this requirement
            }
            else if (qinfo->_startItemCount == 0)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `StartItem` = {} but `StartItemCount` = 0, set to 1 but need fix in DB.",
                    qinfo->GetQuestId(), qinfo->_startItem);
                qinfo->_startItemCount = 1;                    // update to 1 for allow quest work for backward compatibility with DB
            }
        }
        else if (qinfo->_startItemCount > 0)
        {
            TC_LOG_ERROR("sql.sql", "Quest {} has `StartItem` = 0 but `StartItemCount` = {}, useless value.",
                qinfo->GetQuestId(), qinfo->_startItemCount);
            qinfo->_startItemCount = 0;                          // no quest work changes in fact
        }

        if (qinfo->_sourceSpellid)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(qinfo->_sourceSpellid);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `SourceSpellid` = {} but spell {} doesn't exist, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->_sourceSpellid, qinfo->_sourceSpellid);
                qinfo->_sourceSpellid = 0;                        // quest can't be done for this requirement
            }
            else if (!SpellMgr::IsSpellValid(spellInfo))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `SourceSpellid` = {} but spell {} is broken, quest can't be done.",
                    qinfo->GetQuestId(), qinfo->_sourceSpellid, qinfo->_sourceSpellid);
                qinfo->_sourceSpellid = 0;                        // quest can't be done for this requirement
            }
        }

        for (uint8 j = 0; j < QUEST_ITEM_OBJECTIVES_COUNT; ++j)
        {
            uint32 id = qinfo->RequiredItemId[j];
            if (id)
            {
                if (qinfo->RequiredItemCount[j] == 0)
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredItemId{}` = {} but `RequiredItemCount{}` = 0, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes, quest can't be done for this requirement
                }

                qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_DELIVER);

                if (!sObjectMgr->GetItemTemplate(id))
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredItemId{}` = {} but item with entry {} does not exist, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, id);
                    qinfo->RequiredItemCount[j] = 0;             // prevent incorrect work of quest
                }
            }
            else if (qinfo->RequiredItemCount[j]>0)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredItemId{}` = 0 but `RequiredItemCount{}` = {}, quest can't be done.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RequiredItemCount[j]);
                qinfo->RequiredItemCount[j] = 0;                 // prevent incorrect work of quest
            }
        }

        for (uint8 j = 0; j < QUEST_SOURCE_ITEM_IDS_COUNT; ++j)
        {
            uint32 id = qinfo->ItemDrop[j];
            if (id)
            {
                if (!sObjectMgr->GetItemTemplate(id))
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `ItemDrop{}` = {} but item with entry {} does not exist, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, id);
                    // no changes, quest can't be done for this requirement
                }
            }
            else
            {
                if (qinfo->ItemDropQuantity[j]>0)
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `ItemDrop{}` = 0 but `ItemDropQuantity{}` = {}.",
                        qinfo->GetQuestId(), j+1, j+1, qinfo->ItemDropQuantity[j]);
                    // no changes, quest ignore this data
                }
            }
        }

        for (uint8 j = 0; j < QUEST_OBJECTIVES_COUNT; ++j)
        {
            int32 id = qinfo->RequiredNpcOrGo[j];
            if (id < 0 && !sObjectMgr->GetGameObjectTemplate(-id))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredNpcOrGo{}` = {} but gameobject {} does not exist, quest can't be done.",
                    qinfo->GetQuestId(), j+1, id, uint32(-id));
                qinfo->RequiredNpcOrGo[j] = 0;            // quest can't be done for this requirement
            }

            if (id > 0 && !sObjectMgr->GetCreatureTemplate(id))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredNpcOrGo{}` = {} but creature with entry {} does not exist, quest can't be done.",
                    qinfo->GetQuestId(), j+1, id, uint32(id));
                qinfo->RequiredNpcOrGo[j] = 0;            // quest can't be done for this requirement
            }

            if (id)
            {
                // In fact SpeakTo and Kill are quite same: either you can speak to mob:SpeakTo or you can't:Kill/Cast

                qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_KILL | QUEST_SPECIAL_FLAGS_CAST | QUEST_SPECIAL_FLAGS_SPEAKTO);

                if (!qinfo->RequiredNpcOrGoCount[j])
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredNpcOrGo{}` = {} but `RequiredNpcOrGoCount{}` = 0, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes, quest can be incorrectly done, but we already report this
                }
            }
            else if (qinfo->RequiredNpcOrGoCount[j]>0)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RequiredNpcOrGo{}` = 0 but `RequiredNpcOrGoCount{}` = {}.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RequiredNpcOrGoCount[j]);
                // no changes, quest ignore this data
            }
        }

        for (uint8 j = 0; j < QUEST_REWARD_CHOICES_COUNT; ++j)
        {
            uint32 id = qinfo->RewardChoiceItemId[j];
            if (id)
            {
                if (!sObjectMgr->GetItemTemplate(id))
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RewardChoiceItemId{}` = {} but item with entry {} does not exist, quest will not reward this item.",
                        qinfo->GetQuestId(), j+1, id, id);
                    qinfo->RewardChoiceItemId[j] = 0;          // no changes, quest will not reward this
                }

                if (!qinfo->RewardChoiceItemCount[j])
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RewardChoiceItemId{}` = {} but `RewardChoiceItemCount{}` = 0, quest can't be done.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes, quest can't be done
                }
            }
            else if (qinfo->RewardChoiceItemCount[j]>0)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardChoiceItemId{}` = 0 but `RewardChoiceItemCount{}` = {}.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RewardChoiceItemCount[j]);
                // no changes, quest ignore this data
            }
        }

        for (uint8 j = 0; j < QUEST_REWARDS_COUNT; ++j)
        {
            uint32 id = qinfo->RewardItemId[j];
            if (id)
            {
                if (!sObjectMgr->GetItemTemplate(id))
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RewardItemId{}` = {} but item with entry {} does not exist, quest will not reward this item.",
                        qinfo->GetQuestId(), j+1, id, id);
                    qinfo->RewardItemId[j] = 0;                // no changes, quest will not reward this item
                }

                if (!qinfo->RewardItemIdCount[j])
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RewardItemId{}` = {} but `RewardItemIdCount{}` = 0, quest will not reward this item.",
                        qinfo->GetQuestId(), j+1, id, j+1);
                    // no changes
                }
            }
            else if (qinfo->RewardItemIdCount[j]>0)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardItemId{}` = 0 but `RewardItemIdCount{}` = {}.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RewardItemIdCount[j]);
                // no changes, quest ignore this data
            }
        }

        for (uint8 j = 0; j < QUEST_REPUTATIONS_COUNT; ++j)
        {
            if (qinfo->RewardFactionId[j])
            {
                if (std::abs(qinfo->RewardFactionValueId[j]) > 9)
                {
               TC_LOG_ERROR("sql.sql", "Quest {} has RewardFactionValueId{} = {}. That is outside the range of valid values (-9 to 9).", qinfo->GetQuestId(), j+1, qinfo->RewardFactionValueId[j]);
                }
                if (!sFactionStore.LookupEntry(qinfo->RewardFactionId[j]))
                {
                    TC_LOG_ERROR("sql.sql", "Quest {} has `RewardFactionId{}` = {} but raw faction (faction.dbc) {} does not exist, quest will not reward reputation for this faction.", qinfo->GetQuestId(), j+1, qinfo->RewardFactionId[j], qinfo->RewardFactionId[j]);
                    qinfo->RewardFactionId[j] = 0;            // quest will not reward this
                }
            }

            else if (qinfo->RewardFactionValueIdOverride[j] != 0)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardFactionId{}` = 0 but `RewardFactionValueIdOverride{}` = {}.",
                    qinfo->GetQuestId(), j+1, j+1, qinfo->RewardFactionValueIdOverride[j]);
                // no changes, quest ignore this data
            }
        }

        if (qinfo->_rewardDisplaySpell)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(qinfo->_rewardDisplaySpell);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardDisplaySpell` = {} but spell {} does not exist, spell removed as display reward.",
                    qinfo->GetQuestId(), qinfo->_rewardDisplaySpell, qinfo->_rewardDisplaySpell);
                qinfo->_rewardDisplaySpell = 0;                        // no spell reward will display for this quest
            }

            else if (!SpellMgr::IsSpellValid(spellInfo))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardDisplaySpell` = {} but spell {} is broken, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->_rewardDisplaySpell, qinfo->_rewardDisplaySpell);
                qinfo->_rewardDisplaySpell = 0;                        // no spell reward will display for this quest
            }

            else if (GetTalentSpellCost(qinfo->_rewardDisplaySpell))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardDisplaySpell` = {} but spell {} is talent, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->_rewardDisplaySpell, qinfo->_rewardDisplaySpell);
                qinfo->_rewardDisplaySpell = 0;                        // no spell reward will display for this quest
            }
        }

        if (qinfo->_rewardSpell > 0)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(qinfo->_rewardSpell);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardSpell` = {} but spell {} does not exist, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->_rewardSpell, qinfo->_rewardSpell);
                qinfo->_rewardSpell = 0;                    // no spell will be cast on player
            }

            else if (!SpellMgr::IsSpellValid(spellInfo))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardSpell` = {} but spell {} is broken, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->_rewardSpell, qinfo->_rewardSpell);
                qinfo->_rewardSpell = 0;                    // no spell will be cast on player
            }

            else if (GetTalentSpellCost(qinfo->_rewardSpell))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardDisplaySpell` = {} but spell {} is talent, quest will not have a spell reward.",
                    qinfo->GetQuestId(), qinfo->_rewardSpell, qinfo->_rewardSpell);
                qinfo->_rewardSpell = 0;                    // no spell will be cast on player
            }
        }

        if (qinfo->_rewardMailTemplateId)
        {
            if (!sMailTemplateStore.LookupEntry(qinfo->_rewardMailTemplateId))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardMailTemplateId` = {} but mail template  {} does not exist, quest will not have a mail reward.",
                    qinfo->GetQuestId(), qinfo->_rewardMailTemplateId, qinfo->_rewardMailTemplateId);
                qinfo->_rewardMailTemplateId = 0;               // no mail will send to player
                qinfo->_rewardMailDelay = 0;                // no mail will send to player
                qinfo->_rewardMailSenderEntry = 0;
            }
            else if (usedMailTemplates.find(qinfo->_rewardMailTemplateId) != usedMailTemplates.end())
            {
                auto used_mt_itr = usedMailTemplates.find(qinfo->_rewardMailTemplateId);
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardMailTemplateId` = {} but mail template  {} already used for quest {}, quest will not have a mail reward.",
                    qinfo->GetQuestId(), qinfo->_rewardMailTemplateId, qinfo->_rewardMailTemplateId, used_mt_itr->second);
                qinfo->_rewardMailTemplateId = 0;               // no mail will send to player
                qinfo->_rewardMailDelay = 0;                // no mail will send to player
                qinfo->_rewardMailSenderEntry = 0;
            }
            else
                usedMailTemplates.emplace(qinfo->_rewardMailTemplateId, qinfo->GetQuestId());
        }

        if (uint32 rewardNextQuest = qinfo->_rewardNextQuest)
        {
            if (!_questTemplates.count(rewardNextQuest))
            {
                TC_LOG_ERROR("sql.sql", "Quest {} has `RewardNextQuest` = {} but quest {} does not exist, quest chain will not work.",
                    qinfo->GetQuestId(), qinfo->_rewardNextQuest, qinfo->_rewardNextQuest);
                qinfo->_rewardNextQuest = 0;
            }
        }

        // fill additional data stores
        if (uint32 prevQuestId = std::abs(qinfo->_prevQuestId))
        {
            auto prevQuestItr = _questTemplates.find(prevQuestId);
            if (prevQuestItr == _questTemplates.end())
                TC_LOG_ERROR("sql.sql", "Quest {} has PrevQuestId {}, but no such quest", qinfo->GetQuestId(), qinfo->_prevQuestId);
            else if (prevQuestItr->second->_breadcrumbForQuestId)
                TC_LOG_ERROR("sql.sql", "Quest {} should not be unlocked by breadcrumb quest {}", qinfo->_id, prevQuestId);
            else if (qinfo->_prevQuestId > 0)
                qinfo->DependentPreviousQuests.push_back(prevQuestId);
        }

        if (uint32 nextQuestId = qinfo->_nextQuestId)
        {
            auto nextQuestItr = _questTemplates.find(nextQuestId);
            if (nextQuestItr == _questTemplates.end())
                TC_LOG_ERROR("sql.sql", "Quest {} has NextQuestId {}, but no such quest", qinfo->GetQuestId(), qinfo->_nextQuestId);
            else
                nextQuestItr->second->DependentPreviousQuests.push_back(qinfo->GetQuestId());
        }

        if (uint32 breadcrumbForQuestId = std::abs(qinfo->_breadcrumbForQuestId))
        {
            if (_questTemplates.find(breadcrumbForQuestId) == _questTemplates.end())
            {
                TC_LOG_ERROR("sql.sql", "Quest {} is a breadcrumb for quest {}, but no such quest exists", qinfo->_id, breadcrumbForQuestId);
                qinfo->_breadcrumbForQuestId = 0;
            }
            if (qinfo->_nextQuestId)
                TC_LOG_ERROR("sql.sql", "Quest {} is a breadcrumb, should not unlock quest {}", qinfo->_id, qinfo->_nextQuestId);
        }

        if (qinfo->_exclusiveGroup)
            _exclusiveQuestGroups.emplace(qinfo->_exclusiveGroup, qinfo->GetQuestId());
        if (qinfo->_timeAllowed)
            qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_TIMED);
        if (qinfo->_requiredPlayerKills)
            qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_PLAYER_KILL);

        // Special flag to determine if quest is completed from the start, used to determine if we can fail timed quest if it is completed
        if (!qinfo->HasSpecialFlag(QUEST_SPECIAL_FLAGS_KILL | QUEST_SPECIAL_FLAGS_CAST | QUEST_SPECIAL_FLAGS_SPEAKTO | QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
        {
            bool addFlag = true;
            if (qinfo->HasSpecialFlag(QUEST_SPECIAL_FLAGS_DELIVER))
            {
                for (uint8 j = 0; j < QUEST_ITEM_OBJECTIVES_COUNT; ++j)
                {
                    if (qinfo->RequiredItemId[j] != 0 && (qinfo->RequiredItemId[j] != qinfo->GetSrcItemId() || qinfo->RequiredItemCount[j] > qinfo->GetSrcItemCount()))
                    {
                        addFlag = false;
                        break;
                    }
                }
            }

            if (addFlag)
                qinfo->SetSpecialFlag(QUEST_SPECIAL_FLAGS_COMPLETED_AT_START);
        }
    }

    // Disallow any breadcrumb loops and inform quests of their breadcrumbs
    for (auto& questPair : _questTemplates)
    {
        // skip post-loading checks for disabled quests
        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_QUEST, questPair.first, nullptr))
            continue;

        Quest* qinfo = questPair.second.get();
        uint32   qid = qinfo->GetQuestId();
        uint32 breadcrumbForQuestId = std::abs(qinfo->_breadcrumbForQuestId);
        std::set<uint32> questSet;

        while(breadcrumbForQuestId)
        {
            //a previously visited quest was found as a breadcrumb quest
            //breadcrumb loop found!
            if (!questSet.insert(qinfo->_id).second)
            {
                TC_LOG_ERROR("sql.sql", "Breadcrumb quests {} and {} are in a loop", qid, breadcrumbForQuestId);
                qinfo->_breadcrumbForQuestId = 0;
                break;
            }

            qinfo = const_cast<Quest*>(sObjectMgr->GetQuestTemplate(breadcrumbForQuestId));

            //every quest has a list of every breadcrumb towards it
            qinfo->DependentBreadcrumbQuests.push_back(qid);

            breadcrumbForQuestId = qinfo->GetBreadcrumbForQuestId();
        }
    }

    // check QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT for spell with SPELL_EFFECT_QUEST_COMPLETE
    for (uint32 i = 0; i < sSpellMgr->GetSpellInfoStoreSize(); ++i)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(i);
        if (!spellInfo)
            continue;

        for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
        {
            if (spellEffectInfo.Effect != SPELL_EFFECT_QUEST_COMPLETE)
                continue;

            uint32 quest_id = spellEffectInfo.MiscValue;

            Quest const* quest = GetQuestTemplate(quest_id);

            // some quest referenced in spells not exist (outdated spells)
            if (!quest)
                continue;

            if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
            {
                TC_LOG_ERROR("sql.sql", "Spell (id: {}) have SPELL_EFFECT_QUEST_COMPLETE for quest {}, but quest not have flag QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT. Quest flags must be fixed, quest modified to enable objective.", spellInfo->Id, quest_id);

                // this will prevent quest completing without objective
                const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT);
            }
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} quests definitions in {} ms", _questTemplates.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadQuestStartersAndEnders()
{
    TC_LOG_INFO("server.loading", "Loading GO Start Quest Data...");
    LoadGameobjectQuestStarters();
    TC_LOG_INFO("server.loading", "Loading GO End Quest Data...");
    LoadGameobjectQuestEnders();
    TC_LOG_INFO("server.loading", "Loading Creature Start Quest Data...");
    LoadCreatureQuestStarters();
    TC_LOG_INFO("server.loading", "Loading Creature End Quest Data...");
    LoadCreatureQuestEnders();
}

void ObjectMgr::LoadQuestLocales()
{
    uint32 oldMSTime = getMSTime();

    _questLocaleStore.clear();                                // need for reload case

    //                                               0   1       2      3        4           5        6              7               8               9               10
    QueryResult result = WorldDatabase.Query("SELECT ID, locale, Title, Details, Objectives, EndText, CompletedText, ObjectiveText1, ObjectiveText2, ObjectiveText3, ObjectiveText4 FROM quest_template_locale");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id              = fields[0].GetUInt32();
        std::string localeName = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        QuestLocale& data = _questLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.Title);
        AddLocaleString(fields[3].GetString(), locale, data.Details);
        AddLocaleString(fields[4].GetString(), locale, data.Objectives);
        AddLocaleString(fields[5].GetString(), locale, data.AreaDescription);
        AddLocaleString(fields[6].GetString(), locale, data.CompletedText);

        for (uint8 i = 0; i < 4; ++i)
            AddLocaleString(fields[i + 7].GetString(), locale, data.ObjectiveText[i]);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Quest locale strings in {} ms", uint32(_questLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadScripts(ScriptsType type)
{
    uint32 oldMSTime = getMSTime();

    ScriptMapMap* scripts = GetScriptsMapByType(type);
    if (!scripts)
        return;

    std::string tableName = GetScriptsTableNameByType(type);
    if (tableName.empty())
        return;

    if (sMapMgr->IsScriptScheduled())                    // function cannot be called when scripts are in use.
        return;

    TC_LOG_INFO("server.loading", "Loading {}...", tableName);

    scripts->clear();                                       // need for reload support

    bool isSpellScriptTable = (type == SCRIPTS_SPELL);
    //                                                 0    1       2         3         4          5    6  7  8  9
    QueryResult result = WorldDatabase.PQuery("SELECT id, delay, command, datalong, datalong2, dataint, x, y, z, o{} FROM {}", isSpellScriptTable ? ", effIndex" : "", tableName);

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 script definitions. DB table `{}` is empty!", tableName);
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        ScriptInfo tmp;
        tmp.type      = type;
        tmp.id           = fields[0].GetUInt32();
        if (isSpellScriptTable)
            tmp.id      |= fields[10].GetUInt8() << 24;
        tmp.delay        = fields[1].GetUInt32();
        tmp.command      = ScriptCommands(fields[2].GetUInt32());
        tmp.Raw.nData[0] = fields[3].GetUInt32();
        tmp.Raw.nData[1] = fields[4].GetUInt32();
        tmp.Raw.nData[2] = fields[5].GetInt32();
        tmp.Raw.fData[0] = fields[6].GetFloat();
        tmp.Raw.fData[1] = fields[7].GetFloat();
        tmp.Raw.fData[2] = fields[8].GetFloat();
        tmp.Raw.fData[3] = fields[9].GetFloat();

        // generic command args check
        switch (tmp.command)
        {
            case SCRIPT_COMMAND_TALK:
            {
                if (tmp.Talk.ChatType > CHAT_TYPE_WHISPER && tmp.Talk.ChatType != CHAT_MSG_RAID_BOSS_WHISPER)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid talk type (datalong = {}) in SCRIPT_COMMAND_TALK for script id {}",
                        tableName, tmp.Talk.ChatType, tmp.id);
                    continue;
                }
                if (!sObjectMgr->GetBroadcastText(uint32(tmp.Talk.TextID)))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid talk text id (dataint = {}) in SCRIPT_COMMAND_TALK for script id {}",
                        tableName, tmp.Talk.TextID, tmp.id);
                    continue;
                }

                break;
            }

            case SCRIPT_COMMAND_EMOTE:
            {
                if (!sEmotesStore.LookupEntry(tmp.Emote.EmoteID))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid emote id (datalong = {}) in SCRIPT_COMMAND_EMOTE for script id {}",
                        tableName, tmp.Emote.EmoteID, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_TELEPORT_TO:
            {
                if (!sMapStore.LookupEntry(tmp.TeleportTo.MapID))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid map (Id: {}) in SCRIPT_COMMAND_TELEPORT_TO for script id {}",
                        tableName, tmp.TeleportTo.MapID, tmp.id);
                    continue;
                }

                if (!Trinity::IsValidMapCoord(tmp.TeleportTo.DestX, tmp.TeleportTo.DestY, tmp.TeleportTo.DestZ, tmp.TeleportTo.Orientation))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid coordinates (X: {} Y: {} Z: {} O: {}) in SCRIPT_COMMAND_TELEPORT_TO for script id {}",
                        tableName, tmp.TeleportTo.DestX, tmp.TeleportTo.DestY, tmp.TeleportTo.DestZ, tmp.TeleportTo.Orientation, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_QUEST_EXPLORED:
            {
                Quest const* quest = GetQuestTemplate(tmp.QuestExplored.QuestID);
                if (!quest)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid quest (ID: {}) in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id {}",
                        tableName, tmp.QuestExplored.QuestID, tmp.id);
                    continue;
                }

                if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has quest (ID: {}) in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id {}, but quest not have flag QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT in quest flags. Script command or quest flags wrong. Quest modified to require objective.",
                        tableName, tmp.QuestExplored.QuestID, tmp.id);

                    // this will prevent quest completing without objective
                    const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT);

                    // continue; - quest objective requirement set and command can be allowed
                }

                if (float(tmp.QuestExplored.Distance) > DEFAULT_VISIBILITY_DISTANCE)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has too large distance ({}) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id {}",
                        tableName, tmp.QuestExplored.Distance, tmp.id);
                    continue;
                }

                if (tmp.QuestExplored.Distance && float(tmp.QuestExplored.Distance) > DEFAULT_VISIBILITY_DISTANCE)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has too large distance ({}) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id {}, max distance is {} or 0 for disable distance check",
                        tableName, tmp.QuestExplored.Distance, tmp.id, DEFAULT_VISIBILITY_DISTANCE);
                    continue;
                }

                if (tmp.QuestExplored.Distance && float(tmp.QuestExplored.Distance) < INTERACTION_DISTANCE)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has too small distance ({}) for exploring objective complete in `datalong2` in SCRIPT_COMMAND_QUEST_EXPLORED in `datalong` for script id {}, min distance is {} or 0 for disable distance check",
                        tableName, tmp.QuestExplored.Distance, tmp.id, INTERACTION_DISTANCE);
                    continue;
                }

                break;
            }

            case SCRIPT_COMMAND_KILL_CREDIT:
            {
                if (!GetCreatureTemplate(tmp.KillCredit.CreatureEntry))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid creature (Entry: {}) in SCRIPT_COMMAND_KILL_CREDIT for script id {}",
                        tableName, tmp.KillCredit.CreatureEntry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_RESPAWN_GAMEOBJECT:
            {
                GameObjectData const* data = GetGameObjectData(tmp.RespawnGameobject.GOGuid);
                if (!data)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid gameobject (GUID: {}) in SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id {}",
                        tableName, tmp.RespawnGameobject.GOGuid, tmp.id);
                    continue;
                }

                GameObjectTemplate const* info = GetGameObjectTemplate(data->id);
                if (!info)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has gameobject with invalid entry (GUID: {} Entry: {}) in SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id {}",
                        tableName, tmp.RespawnGameobject.GOGuid, data->id, tmp.id);
                    continue;
                }

                if (info->type == GAMEOBJECT_TYPE_FISHINGNODE ||
                    info->type == GAMEOBJECT_TYPE_FISHINGHOLE ||
                    info->type == GAMEOBJECT_TYPE_DOOR        ||
                    info->type == GAMEOBJECT_TYPE_BUTTON      ||
                    info->type == GAMEOBJECT_TYPE_TRAP)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has gameobject type ({}) unsupported by command SCRIPT_COMMAND_RESPAWN_GAMEOBJECT for script id {}",
                        tableName, info->entry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_TEMP_SUMMON_CREATURE:
            {
                if (!Trinity::IsValidMapCoord(tmp.TempSummonCreature.PosX, tmp.TempSummonCreature.PosY, tmp.TempSummonCreature.PosZ, tmp.TempSummonCreature.Orientation))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid coordinates (X: {} Y: {} Z: {} O: {}) in SCRIPT_COMMAND_TEMP_SUMMON_CREATURE for script id {}",
                        tableName, tmp.TempSummonCreature.PosX, tmp.TempSummonCreature.PosY, tmp.TempSummonCreature.PosZ, tmp.TempSummonCreature.Orientation, tmp.id);
                    continue;
                }

                if (!GetCreatureTemplate(tmp.TempSummonCreature.CreatureEntry))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid creature (Entry: {}) in SCRIPT_COMMAND_TEMP_SUMMON_CREATURE for script id {}",
                        tableName, tmp.TempSummonCreature.CreatureEntry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_OPEN_DOOR:
            case SCRIPT_COMMAND_CLOSE_DOOR:
            {
                GameObjectData const* data = GetGameObjectData(tmp.ToggleDoor.GOGuid);
                if (!data)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has invalid gameobject (GUID: {}) in {} for script id {}",
                        tableName, tmp.ToggleDoor.GOGuid, GetScriptCommandName(tmp.command), tmp.id);
                    continue;
                }

                GameObjectTemplate const* info = GetGameObjectTemplate(data->id);
                if (!info)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has gameobject with invalid entry (GUID: {} Entry: {}) in {} for script id {}",
                        tableName, tmp.ToggleDoor.GOGuid, data->id, GetScriptCommandName(tmp.command), tmp.id);
                    continue;
                }

                if (info->type != GAMEOBJECT_TYPE_DOOR)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has gameobject type ({}) unsupported by command {} for script id {}",
                        tableName, info->entry, GetScriptCommandName(tmp.command), tmp.id);
                    continue;
                }

                break;
            }

            case SCRIPT_COMMAND_REMOVE_AURA:
            {
                if (!sSpellMgr->GetSpellInfo(tmp.RemoveAura.SpellID))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` using non-existent spell (id: {}) in SCRIPT_COMMAND_REMOVE_AURA for script id {}",
                        tableName, tmp.RemoveAura.SpellID, tmp.id);
                    continue;
                }
                if (tmp.RemoveAura.Flags & ~0x1)                    // 1 bits (0, 1)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` using unknown flags in datalong2 ({}) in SCRIPT_COMMAND_REMOVE_AURA for script id {}",
                        tableName, tmp.RemoveAura.Flags, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_CAST_SPELL:
            {
                if (!sSpellMgr->GetSpellInfo(tmp.CastSpell.SpellID))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` using non-existent spell (id: {}) in SCRIPT_COMMAND_CAST_SPELL for script id {}",
                        tableName, tmp.CastSpell.SpellID, tmp.id);
                    continue;
                }
                if (tmp.CastSpell.Flags > 4)                      // targeting type
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` using unknown target in datalong2 ({}) in SCRIPT_COMMAND_CAST_SPELL for script id {}",
                        tableName, tmp.CastSpell.Flags, tmp.id);
                    continue;
                }
                if (tmp.CastSpell.Flags != 4 && tmp.CastSpell.CreatureEntry & ~0x1)                      // 1 bit (0, 1)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` using unknown flags in dataint ({}) in SCRIPT_COMMAND_CAST_SPELL for script id {}",
                        tableName, tmp.CastSpell.CreatureEntry, tmp.id);
                    continue;
                }
                else if (tmp.CastSpell.Flags == 4 && !GetCreatureTemplate(tmp.CastSpell.CreatureEntry))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` using invalid creature entry in dataint ({}) in SCRIPT_COMMAND_CAST_SPELL for script id {}",
                        tableName, tmp.CastSpell.CreatureEntry, tmp.id);
                    continue;
                }
                break;
            }

            case SCRIPT_COMMAND_CREATE_ITEM:
            {
                if (!GetItemTemplate(tmp.CreateItem.ItemEntry))
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` has nonexistent item (entry: {}) in SCRIPT_COMMAND_CREATE_ITEM for script id {}",
                        tableName, tmp.CreateItem.ItemEntry, tmp.id);
                    continue;
                }
                if (!tmp.CreateItem.Amount)
                {
                    TC_LOG_ERROR("sql.sql", "Table `{}` SCRIPT_COMMAND_CREATE_ITEM but amount is {} for script id {}",
                        tableName, tmp.CreateItem.Amount, tmp.id);
                    continue;
                }
                break;
            }
            default:
                break;
        }

        if (scripts->find(tmp.id) == scripts->end())
        {
            ScriptMap emptyMap;
            (*scripts)[tmp.id] = emptyMap;
        }
        (*scripts)[tmp.id].insert(std::pair<uint32, ScriptInfo>(tmp.delay, tmp));

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} script definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadSpellScripts()
{
    LoadScripts(SCRIPTS_SPELL);

    // check ids
    for (ScriptMapMap::const_iterator itr = sSpellScripts.begin(); itr != sSpellScripts.end(); ++itr)
    {
        uint32 spellId = uint32(itr->first) & 0x00FFFFFF;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);

        if (!spellInfo)
        {
            TC_LOG_ERROR("sql.sql", "Table `spell_scripts` has not existing spell (Id: {}) as script id", spellId);
            continue;
        }

        SpellEffIndex i = SpellEffIndex((uint32(itr->first) >> 24) & 0x000000FF);
        if (uint32(i) >= MAX_SPELL_EFFECTS)
        {
            TC_LOG_ERROR("sql.sql", "Table `spell_scripts` has too high effect index {} for spell (Id: {}) as script id", uint32(i), spellId);
            continue;
        }

        //check for correct spellEffect
        if (!spellInfo->GetEffect(i).Effect || (spellInfo->GetEffect(i).Effect != SPELL_EFFECT_SCRIPT_EFFECT && spellInfo->GetEffect(i).Effect != SPELL_EFFECT_DUMMY))
            TC_LOG_ERROR("sql.sql", "Table `spell_scripts` - spell {} effect {} is not SPELL_EFFECT_SCRIPT_EFFECT or SPELL_EFFECT_DUMMY", spellId, uint32(i));
    }
}

void ObjectMgr::LoadEventScripts()
{
    LoadScripts(SCRIPTS_EVENT);

    std::set<uint32> evt_scripts;
    // Load all possible script entries from gameobjects
    for (auto const& gameObjectTemplatePair : _gameObjectTemplateStore)
        if (uint32 eventId = gameObjectTemplatePair.second.GetEventScriptId())
            evt_scripts.insert(eventId);

    // Load all possible script entries from spells
    for (uint32 i = 1; i < sSpellMgr->GetSpellInfoStoreSize(); ++i)
        if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(i))
            for (SpellEffectInfo const& spellEffectInfo : spell->GetEffects())
                if (spellEffectInfo.IsEffect(SPELL_EFFECT_SEND_EVENT))
                    if (spellEffectInfo.MiscValue)
                        evt_scripts.insert(spellEffectInfo.MiscValue);

    for (size_t path_idx = 0; path_idx < sTaxiPathNodesByPath.size(); ++path_idx)
    {
        for (size_t node_idx = 0; node_idx < sTaxiPathNodesByPath[path_idx].size(); ++node_idx)
        {
            TaxiPathNodeEntry const* node = sTaxiPathNodesByPath[path_idx][node_idx];

            if (node->ArrivalEventID)
                evt_scripts.insert(node->ArrivalEventID);

            if (node->DepartureEventID)
                evt_scripts.insert(node->DepartureEventID);
        }
    }

    // Then check if all scripts are in above list of possible script entries
    for (ScriptMapMap::const_iterator itr = sEventScripts.begin(); itr != sEventScripts.end(); ++itr)
    {
        std::set<uint32>::const_iterator itr2 = evt_scripts.find(itr->first);
        if (itr2 == evt_scripts.end())
            TC_LOG_ERROR("sql.sql", "Table `event_scripts` has script (Id: {}) not referring to any gameobject_template type 10 data2 field, type 3 data6 field, type 13 data 2 field or any spell effect {}",
                itr->first, SPELL_EFFECT_SEND_EVENT);
    }
}

//Load WP Scripts
void ObjectMgr::LoadWaypointScripts()
{
    LoadScripts(SCRIPTS_WAYPOINT);

    std::set<uint32> actionSet;

    for (ScriptMapMap::const_iterator itr = sWaypointScripts.begin(); itr != sWaypointScripts.end(); ++itr)
        actionSet.insert(itr->first);

    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_WAYPOINT_DATA_ACTION);
    PreparedQueryResult result = WorldDatabase.Query(stmt);

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 action = fields[0].GetUInt32();

            actionSet.erase(action);
        }
        while (result->NextRow());
    }

    for (std::set<uint32>::iterator itr = actionSet.begin(); itr != actionSet.end(); ++itr)
        TC_LOG_ERROR("sql.sql", "There is no waypoint which links to the waypoint script {}", *itr);
}

void ObjectMgr::LoadSpellScriptNames()
{
    uint32 oldMSTime = getMSTime();

    _spellScriptsStore.clear();                            // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT spell_id, ScriptName FROM spell_script_names");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell script names. DB table `spell_script_names` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {

        Field* fields = result->Fetch();

        int32 spellId                = fields[0].GetInt32();
        std::string const scriptName = fields[1].GetString();

        bool allRanks = false;
        if (spellId < 0)
        {
            allRanks = true;
            spellId = -spellId;
        }

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
        {
            TC_LOG_ERROR("sql.sql", "Scriptname: `{}` spell (Id: {}) does not exist.", scriptName, spellId);
            continue;
        }

        if (allRanks)
        {
            if (!spellInfo->IsRanked())
                TC_LOG_ERROR("sql.sql", "Scriptname: `{}` spell (Id: {}) has no ranks of spell.", scriptName, fields[0].GetInt32());

            if (spellInfo->GetFirstRankSpell()->Id != uint32(spellId))
            {
                TC_LOG_ERROR("sql.sql", "Scriptname: `{}` spell (Id: {}) is not first rank of spell.", scriptName, fields[0].GetInt32());
                continue;
            }

            while (spellInfo)
            {
                _spellScriptsStore.insert(SpellScriptsContainer::value_type(spellInfo->Id, std::make_pair(GetScriptId(scriptName), true)));
                spellInfo = spellInfo->GetNextRankSpell();
            }
        }
        else
        {
            if (spellInfo->IsRanked())
                TC_LOG_ERROR("sql.sql", "Scriptname: `{}` spell (Id: {}) is ranked spell. Perhaps not all ranks are assigned to this script.", scriptName, spellId);

            _spellScriptsStore.insert(SpellScriptsContainer::value_type(spellInfo->Id, std::make_pair(GetScriptId(scriptName), true)));
        }

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spell script names in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::ValidateSpellScripts()
{
    uint32 oldMSTime = getMSTime();

    if (_spellScriptsStore.empty())
    {
        TC_LOG_INFO("server.loading", ">> Validated 0 scripts.");
        return;
    }

    uint32 count = 0;

    for (auto spell : _spellScriptsStore)
    {
        SpellInfo const* spellEntry = sSpellMgr->AssertSpellInfo(spell.first);

        auto const bounds = sObjectMgr->GetSpellScriptsBounds(spell.first);

        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (SpellScriptLoader* spellScriptLoader = sScriptMgr->GetSpellScriptLoader(itr->second.first))
            {
                ++count;

                std::unique_ptr<SpellScript> spellScript(spellScriptLoader->GetSpellScript());
                std::unique_ptr<AuraScript> auraScript(spellScriptLoader->GetAuraScript());

                if (!spellScript && !auraScript)
                {
                    TC_LOG_ERROR("scripts", "Functions GetSpellScript() and GetAuraScript() of script `{}` do not return objects - script skipped", GetScriptName(itr->second.first));

                    itr->second.second = false;
                    continue;
                }

                if (spellScript)
                {
                    spellScript->_Init(&spellScriptLoader->GetName(), spellEntry->Id);
                    spellScript->_Register();

                    if (!spellScript->_Validate(spellEntry))
                    {
                        itr->second.second = false;
                        continue;
                    }
                }

                if (auraScript)
                {
                    auraScript->_Init(&spellScriptLoader->GetName(), spellEntry->Id);
                    auraScript->_Register();

                    if (!auraScript->_Validate(spellEntry))
                    {
                        itr->second.second = false;
                        continue;
                    }
                }

                // Enable the script when all checks passed
                itr->second.second = true;
            }
            else
                itr->second.second = false;
        }
    }

    TC_LOG_INFO("server.loading", ">> Validated {} scripts in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadPageTexts()
{
    uint32 oldMSTime = getMSTime();

    //                                               0    1      2
    QueryResult result = WorldDatabase.Query("SELECT ID, `Text`, NextPageID FROM page_text");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 page texts. DB table `page_text` is empty!");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        PageText& pageText  = _pageTextStore[fields[0].GetUInt32()];

        pageText.Text       = fields[1].GetString();
        pageText.NextPageID = fields[2].GetUInt32();

        ++count;
    }
    while (result->NextRow());

    for (PageTextContainer::const_iterator itr = _pageTextStore.begin(); itr != _pageTextStore.end(); ++itr)
    {
        if (itr->second.NextPageID)
        {
            PageTextContainer::const_iterator itr2 = _pageTextStore.find(itr->second.NextPageID);
            if (itr2 == _pageTextStore.end())
                TC_LOG_ERROR("sql.sql", "Page text (ID: {}) has non-existing `NextPageID` ({})", itr->first, itr->second.NextPageID);

        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} page texts in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

PageText const* ObjectMgr::GetPageText(uint32 pageEntry)
{
    PageTextContainer::const_iterator itr = _pageTextStore.find(pageEntry);
    if (itr != _pageTextStore.end())
        return &(itr->second);

    return nullptr;
}

void ObjectMgr::LoadPageTextLocales()
{
    uint32 oldMSTime = getMSTime();

    _pageTextLocaleStore.clear();                             // need for reload case

    //                                               0   1        2
    QueryResult result = WorldDatabase.Query("SELECT ID, locale, `Text` FROM page_text_locale");

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id                   = fields[0].GetUInt32();
        std::string localeName      = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        PageTextLocale& data = _pageTextLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.Text);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} PageText locale strings in {} ms", uint32(_pageTextLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadInstanceTemplate()
{
    uint32 oldMSTime = getMSTime();

    //                                                0     1       2        4
    QueryResult result = WorldDatabase.Query("SELECT map, parent, script, allowMount FROM instance_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 instance templates. DB table `page_text` is empty!");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint16 mapID = fields[0].GetUInt16();

        if (!MapManager::IsValidMAP(mapID, true))
        {
            TC_LOG_ERROR("sql.sql", "ObjectMgr::LoadInstanceTemplate: bad mapid {} for template!", mapID);
            continue;
        }

        InstanceTemplate instanceTemplate;

        instanceTemplate.AllowMount = fields[3].GetBool();
        instanceTemplate.Parent     = uint32(fields[1].GetUInt16());
        instanceTemplate.ScriptId   = sObjectMgr->GetScriptId(fields[2].GetString());

        _instanceTemplateStore[mapID] = instanceTemplate;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} instance templates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

InstanceTemplate const* ObjectMgr::GetInstanceTemplate(uint32 mapID) const
{
    InstanceTemplateContainer::const_iterator itr = _instanceTemplateStore.find(uint16(mapID));
    if (itr != _instanceTemplateStore.end())
        return &(itr->second);

    return nullptr;
}

void ObjectMgr::LoadInstanceEncounters()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0         1            2                3
    QueryResult result = WorldDatabase.Query("SELECT entry, creditType, creditEntry, lastEncounterDungeon FROM instance_encounters");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 instance encounters, table is empty!");
        return;
    }

    uint32 count = 0;
    std::map<uint32, DungeonEncounterEntry const*> dungeonLastBosses;
    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();
        uint8 creditType = fields[1].GetUInt8();
        uint32 creditEntry = fields[2].GetUInt32();
        uint32 lastEncounterDungeon = fields[3].GetUInt16();
        DungeonEncounterEntry const* dungeonEncounter = sDungeonEncounterStore.LookupEntry(entry);
        if (!dungeonEncounter)
        {
            TC_LOG_ERROR("sql.sql", "Table `instance_encounters` has an invalid encounter id {}, skipped!", entry);
            continue;
        }

        if (lastEncounterDungeon && !sLFGMgr->GetLFGDungeonEntry(lastEncounterDungeon))
        {
            TC_LOG_ERROR("sql.sql", "Table `instance_encounters` has an encounter {} ({}) marked as final for invalid dungeon id {}, skipped!", entry, dungeonEncounter->Name[0], lastEncounterDungeon);
            continue;
        }

        auto itr = dungeonLastBosses.find(lastEncounterDungeon);
        if (lastEncounterDungeon)
        {
            if (itr != dungeonLastBosses.end())
            {
                TC_LOG_ERROR("sql.sql", "Table `instance_encounters` specified encounter {} ({}) as last encounter but {} ({}) is already marked as one, skipped!", entry, dungeonEncounter->Name[0], itr->second->ID, itr->second->Name[0]);
                continue;
            }

            dungeonLastBosses[lastEncounterDungeon] = dungeonEncounter;
        }

        switch (creditType)
        {
            case ENCOUNTER_CREDIT_KILL_CREATURE:
            {
                CreatureTemplate const* creatureInfo = GetCreatureTemplate(creditEntry);
                if (!creatureInfo)
                {
                    TC_LOG_ERROR("sql.sql", "Table `instance_encounters` has an invalid creature (entry {}) linked to the encounter {} ({}), skipped!", creditEntry, entry, dungeonEncounter->Name[0]);
                    continue;
                }
                const_cast<CreatureTemplate*>(creatureInfo)->flags_extra |= CREATURE_FLAG_EXTRA_DUNGEON_BOSS;
                for (uint8 diff = 1; diff < MAX_DIFFICULTY; ++diff)
                {
                    if (uint32 diffEntry = creatureInfo->DifficultyEntry[diff - 1])
                    {
                        if (CreatureTemplate const* diffInfo = GetCreatureTemplate(diffEntry))
                            const_cast<CreatureTemplate*>(diffInfo)->flags_extra |= CREATURE_FLAG_EXTRA_DUNGEON_BOSS;
                    }
                }
                break;
            }
            case ENCOUNTER_CREDIT_CAST_SPELL:
                if (!sSpellMgr->GetSpellInfo(creditEntry))
                {
                    TC_LOG_ERROR("sql.sql", "Table `instance_encounters` has an invalid spell (entry {}) linked to the encounter {} ({}), skipped!", creditEntry, entry, dungeonEncounter->Name[0]);
                    continue;
                }
                break;
            default:
                TC_LOG_ERROR("sql.sql", "Table `instance_encounters` has an invalid credit type ({}) for encounter {} ({}), skipped!", creditType, entry, dungeonEncounter->Name[0]);
                continue;
        }

        DungeonEncounterList& encounters = _dungeonEncounterStore[MAKE_PAIR32(dungeonEncounter->MapID, dungeonEncounter->Difficulty)];
        encounters.emplace_back(std::make_unique<DungeonEncounter>(dungeonEncounter, EncounterCreditType(creditType), creditEntry, lastEncounterDungeon));
        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} instance encounters in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

GossipText const* ObjectMgr::GetGossipText(uint32 Text_ID) const
{
    GossipTextContainer::const_iterator itr = _gossipTextStore.find(Text_ID);
    if (itr != _gossipTextStore.end())
        return &itr->second;
    return nullptr;
}

void ObjectMgr::LoadGossipText()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT ID, "
        "text0_0, text0_1, BroadcastTextID0, lang0, Probability0, EmoteDelay0_0, Emote0_0, EmoteDelay0_1, Emote0_1, EmoteDelay0_2, Emote0_2, "
        "text1_0, text1_1, BroadcastTextID1, lang1, Probability1, EmoteDelay1_0, Emote1_0, EmoteDelay1_1, Emote1_1, EmoteDelay1_2, Emote1_2, "
        "text2_0, text2_1, BroadcastTextID2, lang2, Probability2, EmoteDelay2_0, Emote2_0, EmoteDelay2_1, Emote2_1, EmoteDelay2_2, Emote2_2, "
        "text3_0, text3_1, BroadcastTextID3, lang3, Probability3, EmoteDelay3_0, Emote3_0, EmoteDelay3_1, Emote3_1, EmoteDelay3_2, Emote3_2, "
        "text4_0, text4_1, BroadcastTextID4, lang4, Probability4, EmoteDelay4_0, Emote4_0, EmoteDelay4_1, Emote4_1, EmoteDelay4_2, Emote4_2, "
        "text5_0, text5_1, BroadcastTextID5, lang5, Probability5, EmoteDelay5_0, Emote5_0, EmoteDelay5_1, Emote5_1, EmoteDelay5_2, Emote5_2, "
        "text6_0, text6_1, BroadcastTextID6, lang6, Probability6, EmoteDelay6_0, Emote6_0, EmoteDelay6_1, Emote6_1, EmoteDelay6_2, Emote6_2, "
        "text7_0, text7_1, BroadcastTextID7, lang7, Probability7, EmoteDelay7_0, Emote7_0, EmoteDelay7_1, Emote7_1, EmoteDelay7_2, Emote7_2 "
        "FROM npc_text");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 npc texts, table is empty!");
        return;
    }

    _gossipTextStore.rehash(result->GetRowCount());

    uint32 count = 0;
    uint8 cic;

    do
    {
        ++count;
        cic = 0;

        Field* fields = result->Fetch();

        uint32 id = fields[cic++].GetUInt32();
        if (!id)
        {
            TC_LOG_ERROR("sql.sql", "Table `npc_text` has record with reserved id 0, ignore.");
            continue;
        }

        GossipText& gText = _gossipTextStore[id];

        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            GossipTextOption& gOption = gText.Options[i];
            gOption.Text_0           = fields[cic++].GetString();
            gOption.Text_1           = fields[cic++].GetString();
            gOption.BroadcastTextID  = fields[cic++].GetUInt32();
            gOption.Language         = fields[cic++].GetUInt8();
            gOption.Probability      = fields[cic++].GetFloat();

            for (uint8 j = 0; j < MAX_GOSSIP_TEXT_EMOTES; ++j)
            {
                gOption.Emotes[j]._Delay = fields[cic++].GetUInt16();
                gOption.Emotes[j]._Emote = fields[cic++].GetUInt16();
            }

            // check broadcast_text correctness
            if (gOption.BroadcastTextID)
            {
                if (BroadcastText const* bcText = sObjectMgr->GetBroadcastText(gOption.BroadcastTextID))
                {
                    if (bcText->Text[DEFAULT_LOCALE] != gOption.Text_0)
                        TC_LOG_ERROR("sql.sql", "Row {} in table `npc_text` has mismatch between text{}_0 and the corresponding Text in `broadcast_text` row {}", id, i, gOption.BroadcastTextID);
                    if (bcText->Text1[DEFAULT_LOCALE] != gOption.Text_1)
                        TC_LOG_ERROR("sql.sql", "Row {} in table `npc_text` has mismatch between text{}_1 and the corresponding Text1 in `broadcast_text` row {}", id, i, gOption.BroadcastTextID);
                }
                else
                {
                    TC_LOG_ERROR("sql.sql", "GossipText (Id: {}) in table `npc_text` has non-existing or incompatible BroadcastTextID{} {}.", id, i, gOption.BroadcastTextID);
                    gOption.BroadcastTextID = 0;
                }

            }
        }

    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} npc texts in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadNpcTextLocales()
{
    uint32 oldMSTime = getMSTime();

    _npcTextLocaleStore.clear();                              // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT ID, Locale, "
    //   2        3        4        5        6        7        8        9        10       11       12       13       14       15       16       17
        "Text0_0, Text0_1, Text1_0, Text1_1, Text2_0, Text2_1, Text3_0, Text3_1, Text4_0, Text4_1, Text5_0, Text5_1, Text6_0, Text6_1, Text7_0, Text7_1 "
        "FROM npc_text_locale");

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id               = fields[0].GetUInt32();
        std::string localeName  = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        NpcTextLocale& data = _npcTextLocaleStore[id];
        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            AddLocaleString(fields[2 + i * 2].GetString(), locale, data.Text_0[i]);
            AddLocaleString(fields[3 + i * 2].GetString(), locale, data.Text_1[i]);
        }
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} NpcText locale strings in {} ms", uint32(_npcTextLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

//not very fast function but it is called only once a day, or on starting-up
void ObjectMgr::ReturnOrDeleteOldMails(bool serverUp)
{
    uint32 oldMSTime = getMSTime();

    time_t curTime = GameTime::GetGameTime();
    tm lt;
    localtime_r(&curTime, &lt);
    uint64 basetime(curTime);
    TC_LOG_INFO("misc", "Returning mails current time: hour: {}, minute: {}, second: {} ", lt.tm_hour, lt.tm_min, lt.tm_sec);

    // Delete all old mails without item and without body immediately, if starting server
    if (!serverUp)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_EMPTY_EXPIRED_MAIL);
        stmt->setUInt64(0, basetime);
        CharacterDatabase.Execute(stmt);
    }
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_EXPIRED_MAIL);
    stmt->setUInt64(0, basetime);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> No expired mails found.");
        return;                                             // any mails need to be returned or deleted
    }

    std::map<uint32 /*messageId*/, MailItemInfoVec> itemsCache;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_EXPIRED_MAIL_ITEMS);
    stmt->setUInt32(0, (uint32)basetime);
    if (PreparedQueryResult items = CharacterDatabase.Query(stmt))
    {
        MailItemInfo item;
        do
        {
            Field* fields = items->Fetch();
            item.item_guid = fields[0].GetUInt32();
            item.item_template = fields[1].GetUInt32();
            uint32 mailId = fields[2].GetUInt32();
            itemsCache[mailId].push_back(item);
        } while (items->NextRow());
    }

    uint32 deletedCount = 0;
    uint32 returnedCount = 0;
    do
    {
        Field* fields = result->Fetch();
        ObjectGuid::LowType receiver = fields[3].GetUInt32();
        if (serverUp && ObjectAccessor::FindConnectedPlayer(ObjectGuid(HighGuid::Player, receiver)))
            continue;

        Mail* m = new Mail;
        m->messageID      = fields[0].GetUInt32();
        m->messageType    = fields[1].GetUInt8();
        m->sender         = fields[2].GetUInt32();
        m->receiver       = receiver;
        bool has_items    = fields[4].GetBool();
        m->expire_time    = time_t(fields[5].GetUInt32());
        m->deliver_time   = 0;
        m->COD            = fields[6].GetUInt32();
        m->checked        = fields[7].GetUInt8();
        m->mailTemplateId = fields[8].GetInt16();

        // Delete or return mail
        if (has_items)
        {
            // read items from cache
            m->items.swap(itemsCache[m->messageID]);

            // if it is mail from non-player, or if it's already return mail, it shouldn't be returned, but deleted
            if (m->messageType != MAIL_NORMAL || (m->checked & (MAIL_CHECK_MASK_COD_PAYMENT | MAIL_CHECK_MASK_RETURNED)))
            {
                // mail open and then not returned
                for (MailItemInfoVec::iterator itr2 = m->items.begin(); itr2 != m->items.end(); ++itr2)
                {
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEM_INSTANCE);
                    stmt->setUInt32(0, itr2->item_guid);
                    CharacterDatabase.Execute(stmt);
                }

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_ITEM_BY_ID);
                stmt->setUInt32(0, m->messageID);
                CharacterDatabase.Execute(stmt);
            }
            else
            {
                // Mail will be returned
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MAIL_RETURNED);
                stmt->setUInt32(0, m->receiver);
                stmt->setUInt32(1, m->sender);
                stmt->setUInt32(2, basetime + 30 * DAY);
                stmt->setUInt32(3, basetime);
                stmt->setUInt8 (4, uint8(MAIL_CHECK_MASK_RETURNED));
                stmt->setUInt32(5, m->messageID);
                CharacterDatabase.Execute(stmt);
                for (MailItemInfoVec::iterator itr2 = m->items.begin(); itr2 != m->items.end(); ++itr2)
                {
                    // Update receiver in mail items for its proper delivery, and in instance_item for avoid lost item at sender delete
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MAIL_ITEM_RECEIVER);
                    stmt->setUInt32(0, m->sender);
                    stmt->setUInt32(1, itr2->item_guid);
                    CharacterDatabase.Execute(stmt);

                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ITEM_OWNER);
                    stmt->setUInt32(0, m->sender);
                    stmt->setUInt32(1, itr2->item_guid);
                    CharacterDatabase.Execute(stmt);
                }
                delete m;
                ++returnedCount;
                continue;
            }
        }

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_BY_ID);
        stmt->setUInt32(0, m->messageID);
        CharacterDatabase.Execute(stmt);
        delete m;
        ++deletedCount;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Processed {} expired mails: {} deleted and {} returned in {} ms", deletedCount + returnedCount, deletedCount, returnedCount, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadQuestAreaTriggers()
{
    uint32 oldMSTime = getMSTime();

    _questAreaTriggerStore.clear();                           // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT id, quest FROM areatrigger_involvedrelation");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 quest trigger points. DB table `areatrigger_involvedrelation` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 trigger_ID = fields[0].GetUInt32();
        uint32 quest_ID   = fields[1].GetUInt32();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(trigger_ID);
        if (!atEntry)
        {
            TC_LOG_ERROR("sql.sql", "Area trigger (ID:{}) does not exist in `AreaTrigger.dbc`.", trigger_ID);
            continue;
        }

        Quest const* quest = GetQuestTemplate(quest_ID);

        if (!quest)
        {
            TC_LOG_ERROR("sql.sql", "Table `areatrigger_involvedrelation` has record (id: {}) for not existing quest {}", trigger_ID, quest_ID);
            continue;
        }

        if (!quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
        {
            TC_LOG_ERROR("sql.sql", "Table `areatrigger_involvedrelation` has record (id: {}) for not quest {}, but quest not have flag QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT. Trigger or quest flags must be fixed, quest modified to require objective.", trigger_ID, quest_ID);

            // this will prevent quest completing without objective
            const_cast<Quest*>(quest)->SetSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT);

            // continue; - quest modified to required objective and trigger can be allowed.
        }

        _questAreaTriggerStore[trigger_ID] = quest_ID;

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} quest trigger points in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

QuestGreeting const* ObjectMgr::GetQuestGreeting(ObjectGuid guid) const
{
    auto itr = _questGreetingStore.find(guid.GetTypeId());
    if (itr == _questGreetingStore.end())
        return nullptr;

    auto questItr = itr->second.find(guid.GetEntry());
    if (questItr == itr->second.end())
        return nullptr;

    return &questItr->second;
}

void ObjectMgr::LoadQuestGreetings()
{
    uint32 oldMSTime = getMSTime();

    _questGreetingStore.clear(); // need for reload case

    //                                                0   1          2                3             4
    QueryResult result = WorldDatabase.Query("SELECT ID, Type, GreetEmoteType, GreetEmoteDelay, Greeting FROM quest_greeting");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 quest greetings. DB table `quest_greeting` is empty.");
        return;
    }

    _questGreetingStore.rehash(result->GetRowCount());

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 id                   = fields[0].GetUInt32();
        uint8 type                  = fields[1].GetUInt8();
        // overwrite
        switch (type)
        {
            case 0: // Creature
                type = TYPEID_UNIT;
                if (!sObjectMgr->GetCreatureTemplate(id))
                {
                    TC_LOG_ERROR("sql.sql", "Table `quest_greeting`: creature template (entry: {}) does not exist.", id);
                    continue;
                }
                break;
            case 1: // GameObject
                type = TYPEID_GAMEOBJECT;
                if (!sObjectMgr->GetGameObjectTemplate(id))
                {
                    TC_LOG_ERROR("sql.sql", "Table `quest_greeting`: gameobject template (entry: {}) does not exist.", id);
                    continue;
                }
                break;
            default:
                TC_LOG_ERROR("sql.sql", "Table `quest_greeting`: unknown type = {} for entry = {}. Skipped.", type, id);
                continue;
        }

        uint16 greetEmoteType       = fields[2].GetUInt16();

        if (greetEmoteType > 0 && !sEmotesStore.LookupEntry(greetEmoteType))
        {
            TC_LOG_DEBUG("sql.sql", "Table `quest_greeting`: entry {} has greetEmoteType = {} but emote does not exist. Set to 0.", id, greetEmoteType);
            greetEmoteType = 0;
        }

        uint32 greetEmoteDelay      = fields[3].GetUInt32();
        std::string greeting        = fields[4].GetString();

        _questGreetingStore[type][id] = QuestGreeting(greetEmoteType, greetEmoteDelay, greeting);

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} quest_greeting in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadQuestGreetingLocales()
{
    uint32 oldMSTime = getMSTime();

    _questGreetingLocaleStore.clear();                              // need for reload case

    //                                               0     1      2       3
    QueryResult result = WorldDatabase.Query("SELECT ID, Type, Locale, Greeting FROM quest_greeting_locale");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 quest_greeting locales. DB table `quest_greeting_locale` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 id                   = fields[0].GetUInt32();
        uint8 type                  = fields[1].GetUInt8();
        // overwrite
        switch (type)
        {
            case 0: // Creature
                type = TYPEID_UNIT;
                break;
            case 1: // GameObject
                type = TYPEID_GAMEOBJECT;
                break;
            default:
                break;
        }

        std::string localeName      = fields[2].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        QuestGreetingLocale& data   = _questGreetingLocaleStore[MAKE_PAIR32(id, type)];
        AddLocaleString(fields[3].GetString(), locale, data.greeting);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} quest greeting locale strings in {} ms", uint32(_questGreetingLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadQuestOfferRewardLocale()
{
    uint32 oldMSTime = getMSTime();

    _questOfferRewardLocaleStore.clear(); // need for reload case
    //                                               0     1          2
    QueryResult result = WorldDatabase.Query("SELECT Id, locale, RewardText FROM quest_offer_reward_locale");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id = fields[0].GetUInt32();
        std::string localeName = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        QuestOfferRewardLocale& data = _questOfferRewardLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.RewardText);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Quest Offer Reward locale strings in {} ms", _questOfferRewardLocaleStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadQuestRequestItemsLocale()
{
    uint32 oldMSTime = getMSTime();

    _questRequestItemsLocaleStore.clear(); // need for reload case
    //                                               0     1          2
    QueryResult result = WorldDatabase.Query("SELECT Id, locale, CompletionText FROM quest_request_items_locale");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id = fields[0].GetUInt32();
        std::string localeName = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        QuestRequestItemsLocale& data = _questRequestItemsLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.CompletionText);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Quest Request Items locale strings in {} ms", _questRequestItemsLocaleStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadTavernAreaTriggers()
{
    uint32 oldMSTime = getMSTime();

    _tavernAreaTriggerStore.clear();                          // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT id FROM areatrigger_tavern");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 tavern triggers. DB table `areatrigger_tavern` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 Trigger_ID      = fields[0].GetUInt32();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(Trigger_ID);
        if (!atEntry)
        {
            TC_LOG_ERROR("sql.sql", "Area trigger (ID:{}) does not exist in `AreaTrigger.dbc`.", Trigger_ID);
            continue;
        }

        _tavernAreaTriggerStore.insert(Trigger_ID);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} tavern triggers in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadAreaTriggerScripts()
{
    uint32 oldMSTime = getMSTime();

    _areaTriggerScriptStore.clear();                            // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT entry, ScriptName FROM areatrigger_scripts");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 areatrigger scripts. DB table `areatrigger_scripts` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 triggerId             = fields[0].GetUInt32();
        std::string const scriptName = fields[1].GetString();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
        if (!atEntry)
        {
            TC_LOG_ERROR("sql.sql", "AreaTrigger (ID: {}) does not exist in `AreaTrigger.dbc`.", triggerId);
            continue;
        }
        _areaTriggerScriptStore[triggerId] = GetScriptId(scriptName);
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} areatrigger scripts in {} ms", _areaTriggerScriptStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

uint32 ObjectMgr::GetNearestTaxiNode(float x, float y, float z, uint32 mapid, uint32 team)
{
    bool found = false;
    float dist = 10000;
    uint32 id = 0;

    for (uint32 i = 1; i < sTaxiNodesStore.GetNumRows(); ++i)
    {
        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(i);

        if (!node || node->ContinentID != mapid || (!node->MountCreatureID[team == ALLIANCE ? 1 : 0] && node->MountCreatureID[0] != 32981)) // dk flight
            continue;

        uint32 field = uint32((node->ID - 1) / (sizeof(TaxiMask::value_type) * 8));
        TaxiMask::value_type submask = TaxiMask::value_type(1 << ((node->ID - 1) % (sizeof(TaxiMask::value_type) * 8)));

        // skip not taxi network nodes
        if ((sTaxiNodesMask[field] & submask) == 0)
            continue;

        float dist2 = (node->Pos.X - x)*(node->Pos.X - x)+(node->Pos.Y - y)*(node->Pos.Y - y)+(node->Pos.Z - z)*(node->Pos.Z - z);
        if (found)
        {
            if (dist2 < dist)
            {
                dist = dist2;
                id = i;
            }
        }
        else
        {
            found = true;
            dist = dist2;
            id = i;
        }
    }

    return id;
}

void ObjectMgr::GetTaxiPath(uint32 source, uint32 destination, uint32 &path, uint32 &cost)
{
    TaxiPathSetBySource::iterator src_i = sTaxiPathSetBySource.find(source);
    if (src_i == sTaxiPathSetBySource.end())
    {
        path = 0;
        cost = 0;
        return;
    }

    TaxiPathSetForSource& pathSet = src_i->second;

    TaxiPathSetForSource::iterator dest_i = pathSet.find(destination);
    if (dest_i == pathSet.end())
    {
        path = 0;
        cost = 0;
        return;
    }

    cost = dest_i->second.price;
    path = dest_i->second.ID;
}

uint32 ObjectMgr::GetTaxiMountDisplayId(uint32 id, uint32 team, bool allowed_alt_team /* = false */)
{
    uint32 mount_id = 0;

    // select mount creature id
    TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(id);
    if (node)
    {
        uint32 mount_entry = 0;
        if (team == ALLIANCE)
            mount_entry = node->MountCreatureID[1];
        else
            mount_entry = node->MountCreatureID[0];

        // Fix for Alliance not being able to use Acherus taxi
        // only one mount type for both sides
        if (mount_entry == 0 && allowed_alt_team)
        {
            // Simply reverse the selection. At least one team in theory should have a valid mount ID to choose.
            mount_entry = team == ALLIANCE ? node->MountCreatureID[0] : node->MountCreatureID[1];
        }

        CreatureTemplate const* mount_info = GetCreatureTemplate(mount_entry);
        if (mount_info)
        {
            mount_id = mount_info->GetRandomValidModelId();
            if (!mount_id)
            {
                TC_LOG_ERROR("sql.sql", "No displayid found for the taxi mount with the entry {}! Can't load it!", mount_entry);
                return 0;
            }
        }
    }

    // minfo is not actually used but the mount_id was updated
    GetCreatureModelRandomGender(&mount_id);

    return mount_id;
}

Quest const* ObjectMgr::GetQuestTemplate(uint32 quest_id) const
{
    auto itr = _questTemplates.find(quest_id);
    return itr != _questTemplates.end() ? itr->second.get() : nullptr;
}

void ObjectMgr::LoadGraveyardZones()
{
    uint32 oldMSTime = getMSTime();

    GraveyardStore.clear(); // need for reload case

    //                                               0   1          2
    QueryResult result = WorldDatabase.Query("SELECT ID, GhostZone, Faction FROM graveyard_zone");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 graveyard-zone links. DB table `graveyard_zone` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 safeLocId = fields[0].GetUInt32();
        uint32 zoneId = fields[1].GetUInt32();
        uint32 team   = fields[2].GetUInt16();

        WorldSafeLocsEntry const* entry = sWorldSafeLocsStore.LookupEntry(safeLocId);
        if (!entry)
        {
            TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` has a record for non-existing graveyard (WorldSafeLocsID: {}), skipped.", safeLocId);
            continue;
        }

        AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(zoneId);
        if (!areaEntry)
        {
            TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` has a record for non-existing Zone (ID: {}), skipped.", zoneId);
            continue;
        }

        if (areaEntry->ParentAreaID != 0)
        {
            TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` has a record for SubZone (ID: {}) instead of zone, skipped.", zoneId);
            continue;
        }

        if (team != 0 && team != HORDE && team != ALLIANCE)
        {
            TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` has a record for non player faction ({}), skipped.", team);
            continue;
        }

        if (!AddGraveyardLink(safeLocId, zoneId, team, false))
            TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` has a duplicate record for Graveyard (ID: {}) and Zone (ID: {}), skipped.", safeLocId, zoneId);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} graveyard-zone links in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

WorldSafeLocsEntry const* ObjectMgr::GetDefaultGraveyard(uint32 team) const
{
    enum DefaultGraveyard
    {
        HORDE_GRAVEYARD    = 10, // Crossroads
        ALLIANCE_GRAVEYARD = 4   // Westfall
    };

    if (team == HORDE)
        return sWorldSafeLocsStore.LookupEntry(HORDE_GRAVEYARD);
    else if (team == ALLIANCE)
        return sWorldSafeLocsStore.LookupEntry(ALLIANCE_GRAVEYARD);
    else return nullptr;
}

WorldSafeLocsEntry const* ObjectMgr::GetClosestGraveyard(float x, float y, float z, uint32 MapId, uint32 team) const
{
    // search for zone associated closest graveyard
    uint32 zoneId = sMapMgr->GetZoneId(PHASEMASK_NORMAL, MapId, x, y, z);

    if (!zoneId)
    {
        if (z > -500)
        {
            TC_LOG_ERROR("misc", "ZoneId not found for map {} coords ({}, {}, {})", MapId, x, y, z);
            return GetDefaultGraveyard(team);
        }
    }

    // Simulate std. algorithm:
    //   found some graveyard associated to (ghost_zone, ghost_map)
    //
    //   if mapId == graveyard.mapId (ghost in plain zone or city or battleground) and search graveyard at same map
    //     then check faction
    //   if mapId != graveyard.mapId (ghost in instance) and search any graveyard associated
    //     then check faction
    GraveyardMapBounds range = GraveyardStore.equal_range(zoneId);
    MapEntry const* map = sMapStore.LookupEntry(MapId);

    // not need to check validity of map object; MapId _MUST_ be valid here
    if (range.first == range.second && !map->IsBattlegroundOrArena())
    {
        if (zoneId != 0) // zone == 0 can't be fixed, used by bliz for bugged zones
            TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` incomplete: Zone {} Team {} does not have a linked graveyard.", zoneId, team);
        return GetDefaultGraveyard(team);
    }

    // at corpse map
    bool foundNear = false;
    float distNear = 10000;
    WorldSafeLocsEntry const* entryNear = nullptr;

    // at entrance map for corpse map
    bool foundEntr = false;
    float distEntr = 10000;
    WorldSafeLocsEntry const* entryEntr = nullptr;

    // some where other
    WorldSafeLocsEntry const* entryFar = nullptr;

    MapEntry const* mapEntry = sMapStore.LookupEntry(MapId);

    for (; range.first != range.second; ++range.first)
    {
        GraveyardData const& data = range.first->second;

        WorldSafeLocsEntry const* entry = sWorldSafeLocsStore.LookupEntry(data.safeLocId);
        if (!entry)
        {
            TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` has record for not existing graveyard (WorldSafeLocsID {}), skipped.", data.safeLocId);
            continue;
        }

        // skip enemy faction graveyard
        // team == 0 case can be at call from .neargrave
        if (data.team != 0 && team != 0 && data.team != team)
            continue;

        // find now nearest graveyard at other map
        if (MapId != entry->Continent)
        {
            // if find graveyard at different map from where entrance placed (or no entrance data), use any first
            if (!mapEntry
                || mapEntry->CorpseMapID < 0
                || uint32(mapEntry->CorpseMapID) != entry->Continent
                || (mapEntry->Corpse.X == 0 && mapEntry->Corpse.Y == 0))
            {
                // not have any corrdinates for check distance anyway
                entryFar = entry;
                continue;
            }

            // at entrance map calculate distance (2D);
            float dist2 = (entry->Loc.X - mapEntry->Corpse.X)*(entry->Loc.X - mapEntry->Corpse.X)
                +(entry->Loc.Y - mapEntry->Corpse.Y)*(entry->Loc.Y - mapEntry->Corpse.Y);
            if (foundEntr)
            {
                if (dist2 < distEntr)
                {
                    distEntr = dist2;
                    entryEntr = entry;
                }
            }
            else
            {
                foundEntr = true;
                distEntr = dist2;
                entryEntr = entry;
            }
        }
        // find now nearest graveyard at same map
        else
        {
            float dist2 = (entry->Loc.X - x)*(entry->Loc.X - x)+(entry->Loc.Y - y)*(entry->Loc.Y - y)+(entry->Loc.Z - z)*(entry->Loc.Z - z);
            if (foundNear)
            {
                if (dist2 < distNear)
                {
                    distNear = dist2;
                    entryNear = entry;
                }
            }
            else
            {
                foundNear = true;
                distNear = dist2;
                entryNear = entry;
            }
        }
    }

    if (entryNear)
        return entryNear;

    if (entryEntr)
        return entryEntr;

    return entryFar;
}

GraveyardData const* ObjectMgr::FindGraveyardData(uint32 id, uint32 zoneId) const
{
    GraveyardMapBounds range = GraveyardStore.equal_range(zoneId);
    for (; range.first != range.second; ++range.first)
    {
        GraveyardData const& data = range.first->second;
        if (data.safeLocId == id)
            return &data;
    }
    return nullptr;
}

AreaTrigger const* ObjectMgr::GetAreaTrigger(uint32 trigger) const
{
    AreaTriggerContainer::const_iterator itr = _areaTriggerStore.find(trigger);
    if (itr != _areaTriggerStore.end())
        return &itr->second;
    return nullptr;
}

AccessRequirement const* ObjectMgr::GetAccessRequirement(uint32 mapid, Difficulty difficulty) const
{
    AccessRequirementContainer::const_iterator itr = _accessRequirementStore.find(MAKE_PAIR32(mapid, difficulty));
    if (itr != _accessRequirementStore.end())
        return itr->second.get();
    return nullptr;
}

bool ObjectMgr::AddGraveyardLink(uint32 id, uint32 zoneId, uint32 team, bool persist /*= true*/)
{
    if (FindGraveyardData(id, zoneId))
        return false;

    // add link to loaded data
    GraveyardData data;
    data.safeLocId = id;
    data.team = team;

    GraveyardStore.insert(GraveyardContainer::value_type(zoneId, data));

    // add link to DB
    if (persist)
    {
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_GRAVEYARD_ZONE);

        stmt->setUInt32(0, id);
        stmt->setUInt32(1, zoneId);
        stmt->setUInt16(2, uint16(team));

        WorldDatabase.Execute(stmt);
    }

    return true;
}

void ObjectMgr::RemoveGraveyardLink(uint32 id, uint32 zoneId, uint32 team, bool persist /*= false*/)
{
    GraveyardMapBoundsNonConst range = GraveyardStore.equal_range(zoneId);
    if (range.first == range.second)
    {
        //TC_LOG_ERROR("sql.sql", "Table `graveyard_zone` incomplete: Zone {} Team {} does not have a linked graveyard.", zoneId, team);
        return;
    }

    bool found = false;

    for (; range.first != range.second; ++range.first)
    {
        GraveyardData & data = range.first->second;

        // skip not matching safezone id
        if (data.safeLocId != id)
            continue;

        // skip enemy faction graveyard at same map (normal area, city, or battleground)
        // team == 0 case can be at call from .neargrave
        if (data.team != 0 && team != 0 && data.team != team)
            continue;

        found = true;
        break;
    }

    // no match, return
    if (!found)
        return;

    // remove from links
    GraveyardStore.erase(range.first);

    // remove link from DB
    if (persist)
    {
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GRAVEYARD_ZONE);

        stmt->setUInt32(0, id);
        stmt->setUInt32(1, zoneId);
        stmt->setUInt16(2, uint16(team));

        WorldDatabase.Execute(stmt);
    }
}

void ObjectMgr::LoadAreaTriggerTeleports()
{
    uint32 oldMSTime = getMSTime();

    _areaTriggerStore.clear();                                  // need for reload case

    //                                               0        1              2                  3                  4                   5
    QueryResult result = WorldDatabase.Query("SELECT ID,  target_map, target_position_x, target_position_y, target_position_z, target_orientation FROM areatrigger_teleport");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 area trigger teleport definitions. DB table `areatrigger_teleport` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        ++count;

        uint32 Trigger_ID = fields[0].GetUInt32();

        AreaTrigger at;

        at.target_mapId             = fields[1].GetUInt16();
        at.target_X                 = fields[2].GetFloat();
        at.target_Y                 = fields[3].GetFloat();
        at.target_Z                 = fields[4].GetFloat();
        at.target_Orientation       = fields[5].GetFloat();

        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(Trigger_ID);
        if (!atEntry)
        {
            TC_LOG_ERROR("sql.sql", "Area trigger (ID:{}) does not exist in `AreaTrigger.dbc`.", Trigger_ID);
            continue;
        }

        MapEntry const* mapEntry = sMapStore.LookupEntry(at.target_mapId);
        if (!mapEntry)
        {
            TC_LOG_ERROR("sql.sql", "Area trigger (ID:{}) target map (ID: {}) does not exist in `Map.dbc`.", Trigger_ID, at.target_mapId);
            continue;
        }

        if (at.target_X == 0 && at.target_Y == 0 && at.target_Z == 0)
        {
            TC_LOG_ERROR("sql.sql", "Area trigger (ID:{}) target coordinates not provided.", Trigger_ID);
            continue;
        }

        _areaTriggerStore[Trigger_ID] = at;

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} area trigger teleport definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadAccessRequirements()
{
    uint32 oldMSTime = getMSTime();

    _accessRequirementStore.clear();                                  // need for reload case

    //                                               0      1           2          3          4           5      6             7             8                      9     10
    QueryResult result = WorldDatabase.Query("SELECT mapid, difficulty, level_min, level_max, item_level, item, item2, quest_done_A, quest_done_H, completed_achievement, quest_failed_text FROM access_requirement");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 access requirement definitions. DB table `access_requirement` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        ++count;

        uint32 mapid = fields[0].GetUInt32();
        uint8 difficulty = fields[1].GetUInt8();
        uint32 requirement_ID = MAKE_PAIR32(mapid, difficulty);

        auto& ar = _accessRequirementStore[requirement_ID];
        ar = std::make_unique<AccessRequirement>();

        ar->levelMin = fields[2].GetUInt8();
        ar->levelMax = fields[3].GetUInt8();
        ar->item_level = fields[4].GetUInt16();
        ar->item = fields[5].GetUInt32();
        ar->item2 = fields[6].GetUInt32();
        ar->quest_A = fields[7].GetUInt32();
        ar->quest_H = fields[8].GetUInt32();
        ar->achievement = fields[9].GetUInt32();
        ar->questFailedText = fields[10].GetString();

        if (ar->item)
        {
            ItemTemplate const* pProto = GetItemTemplate(ar->item);
            if (!pProto)
            {
                TC_LOG_ERROR("misc", "Key item {} does not exist for map {} difficulty {}, removing key requirement.", ar->item, mapid, difficulty);
                ar->item = 0;
            }
        }

        if (ar->item2)
        {
            ItemTemplate const* pProto = GetItemTemplate(ar->item2);
            if (!pProto)
            {
                TC_LOG_ERROR("misc", "Second item {} does not exist for map {} difficulty {}, removing key requirement.", ar->item2, mapid, difficulty);
                ar->item2 = 0;
            }
        }

        if (ar->quest_A)
        {
            if (!GetQuestTemplate(ar->quest_A))
            {
                TC_LOG_ERROR("sql.sql", "Required Alliance Quest {} not exist for map {} difficulty {}, remove quest done requirement.", ar->quest_A, mapid, difficulty);
                ar->quest_A = 0;
            }
        }

        if (ar->quest_H)
        {
            if (!GetQuestTemplate(ar->quest_H))
            {
                TC_LOG_ERROR("sql.sql", "Required Horde Quest {} not exist for map {} difficulty {}, remove quest done requirement.", ar->quest_H, mapid, difficulty);
                ar->quest_H = 0;
            }
        }

        if (ar->achievement)
        {
            if (!sAchievementStore.LookupEntry(ar->achievement))
            {
                TC_LOG_ERROR("sql.sql", "Required Achievement {} not exist for map {} difficulty {}, remove quest done requirement.", ar->achievement, mapid, difficulty);
                ar->achievement = 0;
            }
        }
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} access requirement definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/*
 * Searches for the areatrigger which teleports players out of the given map with instance_template.parent field support
 */
AreaTrigger const* ObjectMgr::GetGoBackTrigger(uint32 Map) const
{
    bool useParentDbValue = false;
    uint32 parentId = 0;
    MapEntry const* mapEntry = sMapStore.LookupEntry(Map);
    if (!mapEntry || mapEntry->CorpseMapID < 0)
        return nullptr;

    if (mapEntry->IsDungeon())
    {
        InstanceTemplate const* iTemplate = sObjectMgr->GetInstanceTemplate(Map);

        if (!iTemplate)
            return nullptr;

        parentId = iTemplate->Parent;
        useParentDbValue = true;
    }

    uint32 entrance_map = uint32(mapEntry->CorpseMapID);
    for (AreaTriggerContainer::const_iterator itr = _areaTriggerStore.begin(); itr != _areaTriggerStore.end(); ++itr)
        if ((!useParentDbValue && itr->second.target_mapId == entrance_map) || (useParentDbValue && itr->second.target_mapId == parentId))
        {
            AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(itr->first);
            if (atEntry && atEntry->ContinentID == Map)
                return &itr->second;
        }
    return nullptr;
}

/**
 * Searches for the areatrigger which teleports players to the given map
 */
AreaTrigger const* ObjectMgr::GetMapEntranceTrigger(uint32 Map) const
{
    for (AreaTriggerContainer::const_iterator itr = _areaTriggerStore.begin(); itr != _areaTriggerStore.end(); ++itr)
    {
        if (itr->second.target_mapId == Map)
        {
            AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(itr->first);
            if (atEntry)
                return &itr->second;
        }
    }
    return nullptr;
}

void ObjectMgr::SetHighestGuids()
{
    QueryResult result = CharacterDatabase.Query("SELECT MAX(guid) FROM characters");
    if (result)
        GetGenerator<HighGuid::Player>().Set((*result)[0].GetUInt32() + 1);

    result = CharacterDatabase.Query("SELECT MAX(guid) FROM item_instance");
    if (result)
        GetGenerator<HighGuid::Item>().Set((*result)[0].GetUInt32() + 1);

    // Cleanup other tables from nonexistent guids ( >= _hiItemGuid)
    CharacterDatabase.PExecute("DELETE FROM character_inventory WHERE item >= '{}'", GetGenerator<HighGuid::Item>().GetNextAfterMaxUsed());     // One-time query
    CharacterDatabase.PExecute("DELETE FROM mail_items WHERE item_guid >= '{}'", GetGenerator<HighGuid::Item>().GetNextAfterMaxUsed());         // One-time query
    CharacterDatabase.PExecute("DELETE a, ab FROM auctionhouse a LEFT JOIN auctionbidders ab ON ab.id = a.id WHERE itemguid >= '{}'",
        GetGenerator<HighGuid::Item>().GetNextAfterMaxUsed());                                                                                  // One-time query
    CharacterDatabase.PExecute("DELETE FROM guild_bank_item WHERE item_guid >= '{}'", GetGenerator<HighGuid::Item>().GetNextAfterMaxUsed());    // One-time query

    result = WorldDatabase.Query("SELECT MAX(guid) FROM transports");
    if (result)
        GetGenerator<HighGuid::Mo_Transport>().Set((*result)[0].GetUInt32() + 1);

    result = CharacterDatabase.Query("SELECT MAX(id) FROM auctionhouse");
    if (result)
        _auctionId = (*result)[0].GetUInt32()+1;

    result = CharacterDatabase.Query("SELECT MAX(id) FROM mail");
    if (result)
        _mailId = (*result)[0].GetUInt32()+1;

    result = CharacterDatabase.Query("SELECT MAX(arenateamid) FROM arena_team");
    if (result)
        sArenaTeamMgr->SetNextArenaTeamId((*result)[0].GetUInt32()+1);

    result = CharacterDatabase.Query("SELECT MAX(setguid) FROM character_equipmentsets");
    if (result)
        _equipmentSetGuid = (*result)[0].GetUInt64()+1;

    result = CharacterDatabase.Query("SELECT MAX(guildId) FROM guild");
    if (result)
        sGuildMgr->SetNextGuildId((*result)[0].GetUInt32()+1);

    result = CharacterDatabase.Query("SELECT MAX(guid) FROM `groups`");
    if (result)
        sGroupMgr->SetGroupDbStoreSize((*result)[0].GetUInt32()+1);

    result = WorldDatabase.Query("SELECT MAX(guid) FROM creature");
    if (result)
        _creatureSpawnId = (*result)[0].GetUInt32() + 1;

    result = WorldDatabase.Query("SELECT MAX(guid) FROM gameobject");
    if (result)
        _gameObjectSpawnId = (*result)[0].GetUInt32() + 1;
}

ObjectGuidGenerator& ObjectMgr::GetGuidSequenceGenerator(HighGuid high)
{
    auto itr = _guidGenerators.find(high);
    if (itr == _guidGenerators.end())
        itr = _guidGenerators.insert(std::make_pair(high, std::make_unique<ObjectGuidGenerator>(high))).first;

    return *itr->second;
}

uint32 ObjectMgr::GenerateAuctionID()
{
    if (_auctionId >= 0xFFFFFFFE)
    {
        TC_LOG_ERROR("misc", "Auctions ids overflow!! Can't continue, shutting down server. Search on forum for TCE00007 for more info. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _auctionId++;
}

uint64 ObjectMgr::GenerateEquipmentSetGuid()
{
    if (_equipmentSetGuid >= uint64(0xFFFFFFFFFFFFFFFELL))
    {
        TC_LOG_ERROR("misc", "EquipmentSet guid overflow!! Can't continue, shutting down server. Search on forum for TCE00007 for more info. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _equipmentSetGuid++;
}

uint32 ObjectMgr::GenerateMailID()
{
    if (_mailId >= 0xFFFFFFFE)
    {
        TC_LOG_ERROR("misc", "Mail ids overflow!! Can't continue, shutting down server. Search on forum for TCE00007 for more info. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _mailId++;
}

uint32 ObjectMgr::GeneratePetNumber()
{
    if (_hiPetNumber >= 0xFFFFFFFE)
    {
        TC_LOG_ERROR("misc", "_hiPetNumber Id overflow!! Can't continue, shutting down server. Search on forum for TCE00007 for more info.");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _hiPetNumber++;
}

uint32 ObjectMgr::GenerateCreatureSpawnId()
{
    if (_creatureSpawnId >= uint32(0xFFFFFF))
    {
        TC_LOG_ERROR("misc", "Creature spawn id overflow!! Can't continue, shutting down server. Search on forum for TCE00007 for more info.");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _creatureSpawnId++;
}

uint32 ObjectMgr::GenerateGameObjectSpawnId()
{
    if (_gameObjectSpawnId >= uint32(0xFFFFFF))
    {
        TC_LOG_ERROR("misc", "GameObject spawn id overflow!! Can't continue, shutting down server. Search on forum for TCE00007 for more info. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return _gameObjectSpawnId++;
}

void ObjectMgr::LoadGameObjectLocales()
{
    uint32 oldMSTime = getMSTime();

    _gameObjectLocaleStore.clear(); // need for reload case

    //                                               0      1       2     3
    QueryResult result = WorldDatabase.Query("SELECT entry, locale, name, castBarCaption FROM gameobject_template_locale");
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id                   = fields[0].GetUInt32();
        std::string localeName      = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        GameObjectLocale& data = _gameObjectLocaleStore[id];
        AddLocaleString(fields[2].GetString(), locale, data.Name);
        AddLocaleString(fields[3].GetString(), locale, data.CastBarCaption);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gameobject_template_locale strings in {} ms", uint32(_gameObjectLocaleStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

inline void CheckGOLockId(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    if (sLockStore.LookupEntry(dataN))
        return;

    TC_LOG_ERROR("sql.sql", "Gameobject (Entry: {} GoType: {}) have data{}={} but lock (Id: {}) not found.",
        goInfo->entry, goInfo->type, N, goInfo->door.lockId, goInfo->door.lockId);
}

inline void CheckGOLinkedTrapId(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    if (GameObjectTemplate const* trapInfo = sObjectMgr->GetGameObjectTemplate(dataN))
    {
        if (trapInfo->type != GAMEOBJECT_TYPE_TRAP)
            TC_LOG_ERROR("sql.sql", "Gameobject (Entry: {} GoType: {}) have data{}={} but GO (Entry {}) have not GAMEOBJECT_TYPE_TRAP ({}) type.",
            goInfo->entry, goInfo->type, N, dataN, dataN, GAMEOBJECT_TYPE_TRAP);
    }
}

inline void CheckGOSpellId(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    if (sSpellMgr->GetSpellInfo(dataN))
        return;

    TC_LOG_ERROR("sql.sql", "Gameobject (Entry: {} GoType: {}) have data{}={} but Spell (Entry {}) not exist.",
        goInfo->entry, goInfo->type, N, dataN, dataN);
}

inline void CheckAndFixGOChairHeightId(GameObjectTemplate const* goInfo, uint32& dataN, uint32 N)
{
    if (dataN <= (UNIT_STAND_STATE_SIT_HIGH_CHAIR-UNIT_STAND_STATE_SIT_LOW_CHAIR))
        return;

    TC_LOG_ERROR("sql.sql", "Gameobject (Entry: {} GoType: {}) have data{}={} but correct chair height in range 0..{}.",
        goInfo->entry, goInfo->type, N, dataN, UNIT_STAND_STATE_SIT_HIGH_CHAIR-UNIT_STAND_STATE_SIT_LOW_CHAIR);

    // prevent client and server unexpected work
    dataN = 0;
}

inline void CheckGONoDamageImmuneId(GameObjectTemplate* goTemplate, uint32 dataN, uint32 N)
{
    // 0/1 correct values
    if (dataN <= 1)
        return;

    TC_LOG_ERROR("sql.sql", "Gameobject (Entry: {} GoType: {}) have data{}={} but expected boolean (0/1) noDamageImmune field value.", goTemplate->entry, goTemplate->type, N, dataN);
}

inline void CheckGOConsumable(GameObjectTemplate const* goInfo, uint32 dataN, uint32 N)
{
    // 0/1 correct values
    if (dataN <= 1)
        return;

    TC_LOG_ERROR("sql.sql", "Gameobject (Entry: {} GoType: {}) have data{}={} but expected boolean (0/1) consumable field value.",
        goInfo->entry, goInfo->type, N, dataN);
}

void ObjectMgr::LoadGameObjectTemplate()
{
    uint32 oldMSTime = getMSTime();

    //                                                 0      1      2        3       4             5          6     7
    QueryResult result = WorldDatabase.Query("SELECT entry, type, displayId, name, IconName, castBarCaption, unk1, size, "
    //                                         8      9      10     11     12     13     14     15     16     17     18      19      20
                                             "Data0, Data1, Data2, Data3, Data4, Data5, Data6, Data7, Data8, Data9, Data10, Data11, Data12, "
    //                                         21      22      23      24      25      26      27      28      29      30      31      32      33          34
                                             "Data13, Data14, Data15, Data16, Data17, Data18, Data19, Data20, Data21, Data22, Data23, AIName, ScriptName, StringId "
                                             "FROM gameobject_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gameobject definitions. DB table `gameobject_template` is empty.");
        return;
    }

    _gameObjectTemplateStore.reserve(result->GetRowCount());
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        GameObjectTemplate& got = _gameObjectTemplateStore[entry];
        got.entry          = entry;
        got.type           = uint32(fields[1].GetUInt8());
        got.displayId      = fields[2].GetUInt32();
        got.name           = fields[3].GetString();
        got.IconName       = fields[4].GetString();
        got.castBarCaption = fields[5].GetString();
        got.unk1           = fields[6].GetString();
        got.size           = fields[7].GetFloat();

        for (uint8 i = 0; i < MAX_GAMEOBJECT_DATA; ++i)
            got.raw.data[i] = fields[8 + i].GetInt32(); // data1 and data6 can be -1

        got.AIName = fields[32].GetString();
        got.ScriptId = GetScriptId(fields[33].GetString());
        got.StringId = fields[34].GetString();

        // Checks
        if (!got.AIName.empty() && !sGameObjectAIRegistry->HasItem(got.AIName))
        {
            TC_LOG_ERROR("sql.sql", "GameObject (Entry: {}) has non-registered `AIName` '{}' set, removing", got.entry, got.AIName);
            got.AIName.clear();
        }

        switch (got.type)
        {
            case GAMEOBJECT_TYPE_DOOR:                      //0
            {
                if (got.door.lockId)
                    CheckGOLockId(&got, got.door.lockId, 1);
                CheckGONoDamageImmuneId(&got, got.door.noDamageImmune, 3);
                break;
            }
            case GAMEOBJECT_TYPE_BUTTON:                    //1
            {
                if (got.button.lockId)
                    CheckGOLockId(&got, got.button.lockId, 1);
                CheckGONoDamageImmuneId(&got, got.button.noDamageImmune, 4);
                break;
            }
            case GAMEOBJECT_TYPE_QUESTGIVER:                //2
            {
                if (got.questgiver.lockId)
                    CheckGOLockId(&got, got.questgiver.lockId, 0);
                CheckGONoDamageImmuneId(&got, got.questgiver.noDamageImmune, 5);
                break;
            }
            case GAMEOBJECT_TYPE_CHEST:                     //3
            {
                if (got.chest.lockId)
                    CheckGOLockId(&got, got.chest.lockId, 0);

                CheckGOConsumable(&got, got.chest.consumable, 3);

                if (got.chest.linkedTrapId)              // linked trap
                    CheckGOLinkedTrapId(&got, got.chest.linkedTrapId, 7);
                break;
            }
            case GAMEOBJECT_TYPE_TRAP:                      //6
            {
                if (got.trap.lockId)
                    CheckGOLockId(&got, got.trap.lockId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_CHAIR:                     //7
                CheckAndFixGOChairHeightId(&got, got.chair.height, 1);
                break;
            case GAMEOBJECT_TYPE_SPELL_FOCUS:               //8
            {
                if (got.spellFocus.focusId)
                {
                    if (!sSpellFocusObjectStore.LookupEntry(got.spellFocus.focusId))
                        TC_LOG_ERROR("sql.sql", "GameObject (Entry: {} GoType: {}) have data0={} but SpellFocus (Id: {}) not exist.",
                        entry, got.type, got.spellFocus.focusId, got.spellFocus.focusId);
                }

                if (got.spellFocus.linkedTrapId)        // linked trap
                    CheckGOLinkedTrapId(&got, got.spellFocus.linkedTrapId, 2);
                break;
            }
            case GAMEOBJECT_TYPE_GOOBER:                    //10
            {
                if (got.goober.lockId)
                    CheckGOLockId(&got, got.goober.lockId, 0);

                CheckGOConsumable(&got, got.goober.consumable, 3);

                if (got.goober.pageId)                  // pageId
                {
                    if (!GetPageText(got.goober.pageId))
                        TC_LOG_ERROR("sql.sql", "GameObject (Entry: {} GoType: {}) have data7={} but PageText (Entry {}) not exist.",
                        entry, got.type, got.goober.pageId, got.goober.pageId);
                }
                CheckGONoDamageImmuneId(&got, got.goober.noDamageImmune, 11);
                if (got.goober.linkedTrapId)            // linked trap
                    CheckGOLinkedTrapId(&got, got.goober.linkedTrapId, 12);
                break;
            }
            case GAMEOBJECT_TYPE_AREADAMAGE:                //12
            {
                if (got.areadamage.lockId)
                    CheckGOLockId(&got, got.areadamage.lockId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_CAMERA:                    //13
            {
                if (got.camera.lockId)
                    CheckGOLockId(&got, got.camera.lockId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_MO_TRANSPORT:              //15
            {
                if (got.moTransport.taxiPathId)
                {
                    if (got.moTransport.taxiPathId >= sTaxiPathNodesByPath.size() || sTaxiPathNodesByPath[got.moTransport.taxiPathId].empty())
                        TC_LOG_ERROR("sql.sql", "GameObject (Entry: {} GoType: {}) have data0={} but TaxiPath (Id: {}) not exist.",
                            entry, got.type, got.moTransport.taxiPathId, got.moTransport.taxiPathId);
                }
                if (uint32 transportMap = got.moTransport.mapID)
                    _transportMaps.insert(transportMap);
                break;
            }
            case GAMEOBJECT_TYPE_SUMMONING_RITUAL:          //18
                break;
            case GAMEOBJECT_TYPE_SPELLCASTER:               //22
            {
                // always must have spell
                CheckGOSpellId(&got, got.spellcaster.spellId, 0);
                break;
            }
            case GAMEOBJECT_TYPE_FLAGSTAND:                 //24
            {
                if (got.flagstand.lockId)
                    CheckGOLockId(&got, got.flagstand.lockId, 0);
                CheckGONoDamageImmuneId(&got, got.flagstand.noDamageImmune, 5);
                break;
            }
            case GAMEOBJECT_TYPE_FISHINGHOLE:               //25
            {
                if (got.fishinghole.lockId)
                    CheckGOLockId(&got, got.fishinghole.lockId, 4);
                break;
            }
            case GAMEOBJECT_TYPE_FLAGDROP:                  //26
            {
                if (got.flagdrop.lockId)
                    CheckGOLockId(&got, got.flagdrop.lockId, 0);
                CheckGONoDamageImmuneId(&got, got.flagdrop.noDamageImmune, 3);
                break;
            }
            case GAMEOBJECT_TYPE_BARBER_CHAIR:              //32
                CheckAndFixGOChairHeightId(&got, got.barberChair.chairheight, 0);
                break;
        }
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} game object templates in {} ms", _gameObjectTemplateStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadGameObjectTemplateAddons()
{
    uint32 oldMSTime = getMSTime();

    //                                                0       1       2      3        4       5        6        7        8
    QueryResult result = WorldDatabase.Query("SELECT entry, faction, flags, mingold, maxgold, artkit0, artkit1, artkit2, artkit3 FROM gameobject_template_addon");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gameobject template addon definitions. DB table `gameobject_template_addon` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        GameObjectTemplate const* got = sObjectMgr->GetGameObjectTemplate(entry);
        if (!got)
        {
            TC_LOG_ERROR("sql.sql", "GameObject template (Entry: {}) does not exist but has a record in `gameobject_template_addon`", entry);
            continue;
        }

        GameObjectTemplateAddon& gameObjectAddon = _gameObjectTemplateAddonStore[entry];
        gameObjectAddon.Faction = uint32(fields[1].GetUInt16());
        gameObjectAddon.Flags   = fields[2].GetUInt32();
        gameObjectAddon.Mingold = fields[3].GetUInt32();
        gameObjectAddon.Maxgold = fields[4].GetUInt32();

        for (uint32 i = 0; i < gameObjectAddon.artKits.size(); ++i)
        {
            uint32 artKitID = fields[5 + i].GetUInt32();
            if (!artKitID)
                continue;

            if (!sGameObjectArtKitStore.LookupEntry(artKitID))
            {
                TC_LOG_ERROR("sql.sql", "GameObject (Entry: {}) has invalid `artkit{}` ({}) defined, set to zero instead.", entry, i, artKitID);
                continue;
            }

            gameObjectAddon.artKits[i] = artKitID;
        }

        // checks
        if (gameObjectAddon.Faction && !sFactionTemplateStore.LookupEntry(gameObjectAddon.Faction))
            TC_LOG_ERROR("sql.sql", "GameObject (Entry: {}) has invalid faction ({}) defined in `gameobject_template_addon`.", entry, gameObjectAddon.Faction);

        if (gameObjectAddon.Maxgold > 0)
        {
            switch (got->type)
            {
                case GAMEOBJECT_TYPE_CHEST:
                case GAMEOBJECT_TYPE_FISHINGHOLE:
                    break;
                default:
                    TC_LOG_ERROR("sql.sql", "GameObject (Entry {} GoType: {}) cannot be looted but has maxgold set in `gameobject_template_addon`.", entry, got->type);
                    break;
            }
        }

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} game object template addons in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadGameObjectOverrides()
{
    uint32 oldMSTime = getMSTime();

    //                                                     0        1      2
    QueryResult result = WorldDatabase.Query("SELECT spawnId, faction, flags FROM gameobject_overrides");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gameobject faction and flags overrides. DB table `gameobject_overrides` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        ObjectGuid::LowType spawnId = fields[0].GetUInt32();
        GameObjectData const* goData = GetGameObjectData(spawnId);
        if (!goData)
        {
            TC_LOG_ERROR("sql.sql", "GameObject (SpawnId: {}) does not exist but has a record in `gameobject_overrides`", spawnId);
            continue;
        }

        GameObjectOverride& gameObjectOverride = _gameObjectOverrideStore[spawnId];
        gameObjectOverride.Faction = fields[1].GetUInt16();
        gameObjectOverride.Flags = fields[2].GetUInt32();

        if (gameObjectOverride.Faction && !sFactionTemplateStore.LookupEntry(gameObjectOverride.Faction))
            TC_LOG_ERROR("sql.sql", "GameObject (SpawnId: {}) has invalid faction ({}) defined in `gameobject_overrides`.", spawnId, gameObjectOverride.Faction);

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gameobject faction and flags overrides in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadExplorationBaseXP()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT level, basexp FROM exploration_basexp");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 BaseXP definitions. DB table `exploration_basexp` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint8 level  = fields[0].GetUInt8();
        uint32 basexp = fields[1].GetInt32();
        _baseXPTable[level] = basexp;
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} BaseXP definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

uint32 ObjectMgr::GetBaseXP(uint8 level)
{
    return _baseXPTable[level] ? _baseXPTable[level] : 0;
}

uint32 ObjectMgr::GetXPForLevel(uint8 level) const
{
    if (level < _playerXPperLevel.size())
        return _playerXPperLevel[level];
    return 0;
}

void ObjectMgr::LoadPetNames()
{
    uint32 oldMSTime = getMSTime();
    //                                                0     1      2
    QueryResult result = WorldDatabase.Query("SELECT word, entry, half FROM pet_name_generation");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 pet name parts. DB table `pet_name_generation` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        std::string word = fields[0].GetString();
        uint32 entry     = fields[1].GetUInt32();
        bool   half      = fields[2].GetBool();
        if (half)
            _petHalfName1[entry].push_back(word);
        else
            _petHalfName0[entry].push_back(word);
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} pet name parts in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadPetNumber()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = CharacterDatabase.Query("SELECT MAX(id) FROM character_pet");
    if (result)
    {
        Field* fields = result->Fetch();
        _hiPetNumber = fields[0].GetUInt32()+1;
    }

    TC_LOG_INFO("server.loading", ">> Loaded the max pet number: {} in {} ms", _hiPetNumber-1, GetMSTimeDiffToNow(oldMSTime));
}

std::string ObjectMgr::GeneratePetName(uint32 entry)
{
    std::vector<std::string>& list0 = _petHalfName0[entry];
    std::vector<std::string>& list1 = _petHalfName1[entry];

    if (list0.empty() || list1.empty())
    {
        CreatureTemplate const* cinfo = GetCreatureTemplate(entry);
        if (!cinfo)
            return std::string();

        char const* petname = GetPetName(cinfo->family, sWorld->GetDefaultDbcLocale());
        if (petname)
            return std::string(petname);
        else
            return cinfo->Name;
    }

    return *(list0.begin()+urand(0, list0.size()-1)) + *(list1.begin()+urand(0, list1.size()-1));
}

void ObjectMgr::LoadReputationRewardRate()
{
    uint32 oldMSTime = getMSTime();

    _repRewardRateStore.clear();                             // for reload case

    uint32 count = 0; //                                0          1             2                  3                  4                 5                      6             7
    QueryResult result = WorldDatabase.Query("SELECT faction, quest_rate, quest_daily_rate, quest_weekly_rate, quest_monthly_rate, quest_repeatable_rate, creature_rate, spell_rate FROM reputation_reward_rate");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded `reputation_reward_rate`, table is empty!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 factionId            = fields[0].GetUInt32();

        RepRewardRate repRate;

        repRate.questRate           = fields[1].GetFloat();
        repRate.questDailyRate      = fields[2].GetFloat();
        repRate.questWeeklyRate     = fields[3].GetFloat();
        repRate.questMonthlyRate    = fields[4].GetFloat();
        repRate.questRepeatableRate = fields[5].GetFloat();
        repRate.creatureRate        = fields[6].GetFloat();
        repRate.spellRate           = fields[7].GetFloat();

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry)
        {
            TC_LOG_ERROR("sql.sql", "Faction (faction.dbc) {} does not exist but is used in `reputation_reward_rate`", factionId);
            continue;
        }

        if (repRate.questRate < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table reputation_reward_rate has quest_rate with invalid rate {}, skipping data for faction {}", repRate.questRate, factionId);
            continue;
        }

        if (repRate.questDailyRate < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table reputation_reward_rate has quest_daily_rate with invalid rate {}, skipping data for faction {}", repRate.questDailyRate, factionId);
            continue;
        }

        if (repRate.questWeeklyRate < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table reputation_reward_rate has quest_weekly_rate with invalid rate {}, skipping data for faction {}", repRate.questWeeklyRate, factionId);
            continue;
        }

        if (repRate.questMonthlyRate < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table reputation_reward_rate has quest_monthly_rate with invalid rate {}, skipping data for faction {}", repRate.questMonthlyRate, factionId);
            continue;
        }

        if (repRate.questRepeatableRate < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table reputation_reward_rate has quest_repeatable_rate with invalid rate {}, skipping data for faction {}", repRate.questRepeatableRate, factionId);
            continue;
        }

        if (repRate.creatureRate < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table reputation_reward_rate has creature_rate with invalid rate {}, skipping data for faction {}", repRate.creatureRate, factionId);
            continue;
        }

        if (repRate.spellRate < 0.0f)
        {
            TC_LOG_ERROR("sql.sql", "Table reputation_reward_rate has spell_rate with invalid rate {}, skipping data for faction {}", repRate.spellRate, factionId);
            continue;
        }

        _repRewardRateStore[factionId] = repRate;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} reputation_reward_rate in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadReputationOnKill()
{
    uint32 oldMSTime = getMSTime();

    // For reload case
    _repOnKillStore.clear();

    uint32 count = 0;

    //                                                0            1                     2
    QueryResult result = WorldDatabase.Query("SELECT creature_id, RewOnKillRepFaction1, RewOnKillRepFaction2, "
    //   3             4             5                   6             7             8                   9
        "IsTeamAward1, MaxStanding1, RewOnKillRepValue1, IsTeamAward2, MaxStanding2, RewOnKillRepValue2, TeamDependent "
        "FROM creature_onkill_reputation");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature award reputation definitions. DB table `creature_onkill_reputation` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 creature_id = fields[0].GetUInt32();

        ReputationOnKillEntry repOnKill;
        repOnKill.RepFaction1          = fields[1].GetInt16();
        repOnKill.RepFaction2          = fields[2].GetInt16();
        repOnKill.IsTeamAward1        = fields[3].GetBool();
        repOnKill.ReputationMaxCap1  = fields[4].GetUInt8();
        repOnKill.RepValue1            = fields[5].GetInt32();
        repOnKill.IsTeamAward2        = fields[6].GetBool();
        repOnKill.ReputationMaxCap2  = fields[7].GetUInt8();
        repOnKill.RepValue2            = fields[8].GetInt32();
        repOnKill.TeamDependent       = fields[9].GetBool();

        if (!GetCreatureTemplate(creature_id))
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_onkill_reputation` has data for nonexistent creature entry ({}), skipped", creature_id);
            continue;
        }

        if (repOnKill.RepFaction1)
        {
            FactionEntry const* factionEntry1 = sFactionStore.LookupEntry(repOnKill.RepFaction1);
            if (!factionEntry1)
            {
                TC_LOG_ERROR("sql.sql", "Faction (faction.dbc) {} does not exist but is used in `creature_onkill_reputation`", repOnKill.RepFaction1);
                continue;
            }
        }

        if (repOnKill.RepFaction2)
        {
            FactionEntry const* factionEntry2 = sFactionStore.LookupEntry(repOnKill.RepFaction2);
            if (!factionEntry2)
            {
                TC_LOG_ERROR("sql.sql", "Faction (faction.dbc) {} does not exist but is used in `creature_onkill_reputation`", repOnKill.RepFaction2);
                continue;
            }
        }

        _repOnKillStore[creature_id] = repOnKill;

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature award reputation definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadReputationSpilloverTemplate()
{
    uint32 oldMSTime = getMSTime();

    _repSpilloverTemplateStore.clear();                      // for reload case

    uint32 count = 0; //                                0         1        2       3        4       5       6         7        8      9        10       11     12
    QueryResult result = WorldDatabase.Query("SELECT faction, faction1, rate_1, rank_1, faction2, rate_2, rank_2, faction3, rate_3, rank_3, faction4, rate_4, rank_4 FROM reputation_spillover_template");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded `reputation_spillover_template`, table is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 factionId                = fields[0].GetUInt16();

        RepSpilloverTemplate repTemplate;

        repTemplate.faction[0]          = fields[1].GetUInt16();
        repTemplate.faction_rate[0]     = fields[2].GetFloat();
        repTemplate.faction_rank[0]     = fields[3].GetUInt8();
        repTemplate.faction[1]          = fields[4].GetUInt16();
        repTemplate.faction_rate[1]     = fields[5].GetFloat();
        repTemplate.faction_rank[1]     = fields[6].GetUInt8();
        repTemplate.faction[2]          = fields[7].GetUInt16();
        repTemplate.faction_rate[2]     = fields[8].GetFloat();
        repTemplate.faction_rank[2]     = fields[9].GetUInt8();
        repTemplate.faction[3]          = fields[10].GetUInt16();
        repTemplate.faction_rate[3]     = fields[11].GetFloat();
        repTemplate.faction_rank[3]     = fields[12].GetUInt8();

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);

        if (!factionEntry)
        {
            TC_LOG_ERROR("sql.sql", "Faction (faction.dbc) {} does not exist but is used in `reputation_spillover_template`", factionId);
            continue;
        }

        if (factionEntry->ParentFactionID == 0)
        {
            TC_LOG_ERROR("sql.sql", "Faction (faction.dbc) {} in `reputation_spillover_template` does not belong to any team, skipping", factionId);
            continue;
        }

        bool invalidSpilloverFaction = false;
        for (uint32 i = 0; i < MAX_SPILLOVER_FACTIONS; ++i)
        {
            if (repTemplate.faction[i])
            {
                FactionEntry const* factionSpillover = sFactionStore.LookupEntry(repTemplate.faction[i]);

                if (!factionSpillover)
                {
                    TC_LOG_ERROR("sql.sql", "Spillover faction (faction.dbc) {} does not exist but is used in `reputation_spillover_template` for faction {}, skipping", repTemplate.faction[i], factionId);
                    invalidSpilloverFaction = true;
                    break;
                }

                if (factionSpillover->ReputationIndex < 0)
                {
                    TC_LOG_ERROR("sql.sql", "Spillover faction (faction.dbc) {} for faction {} in `reputation_spillover_template` can not be listed for client, and then useless, skipping", repTemplate.faction[i], factionId);
                    invalidSpilloverFaction = true;
                    break;
                }

                if (repTemplate.faction_rank[i] >= MAX_REPUTATION_RANK)
                {
                    TC_LOG_ERROR("sql.sql", "Rank {} used in `reputation_spillover_template` for spillover faction {} is not valid, skipping", repTemplate.faction_rank[i], repTemplate.faction[i]);
                    invalidSpilloverFaction = true;
                    break;
                }
            }
        }

        if (invalidSpilloverFaction)
            continue;

        _repSpilloverTemplateStore[factionId] = repTemplate;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} reputation_spillover_template in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadPointsOfInterest()
{
    uint32 oldMSTime = getMSTime();

    _pointsOfInterestStore.clear();                              // need for reload case

    uint32 count = 0;

    //                                               0       1          2        3     4      5    6
    QueryResult result = WorldDatabase.Query("SELECT ID, PositionX, PositionY, Icon, Flags, Importance, Name FROM points_of_interest");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 Points of Interest definitions. DB table `points_of_interest` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 id = fields[0].GetUInt32();

        PointOfInterest pointOfInterest;
        pointOfInterest.ID         = id;
        pointOfInterest.PositionX  = fields[1].GetFloat();
        pointOfInterest.PositionY  = fields[2].GetFloat();
        pointOfInterest.Icon       = fields[3].GetUInt32();
        pointOfInterest.Flags      = fields[4].GetUInt32();
        pointOfInterest.Importance = fields[5].GetUInt32();
        pointOfInterest.Name       = fields[6].GetString();

        if (!Trinity::IsValidMapCoord(pointOfInterest.PositionX, pointOfInterest.PositionY))
        {
            TC_LOG_ERROR("sql.sql", "Table `points_of_interest` (ID: {}) have invalid coordinates (X: {} Y: {}), ignored.", id, pointOfInterest.PositionX, pointOfInterest.PositionY);
            continue;
        }

        _pointsOfInterestStore[id] = pointOfInterest;

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Points of Interest definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadQuestPOI()
{
    uint32 oldMSTime = getMSTime();

    _questPOIStore.clear();                              // need for reload case

    //                                               0        1          2          3           4          5       6        7
    QueryResult result = WorldDatabase.Query("SELECT QuestID, id, ObjectiveIndex, MapID, WorldMapAreaId, Floor, Priority, Flags FROM quest_poi");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 quest POI definitions. DB table `quest_poi` is empty.");
        return;
    }

    _questPOIStore.reserve(result->GetRowCount());

    //                                                  0       1   2  3
    QueryResult points = WorldDatabase.Query("SELECT QuestID, Idx1, X, Y FROM quest_poi_points ORDER BY QuestID DESC, Idx2");

    std::vector<std::vector<std::vector<QuestPOIBlobPoint>>> POIs;
    if (points)
    {
        // The first result should have the highest questId
        Field* fields = points->Fetch();
        uint32 const maxQuestPOIId = fields[0].GetUInt32();
        POIs.resize(maxQuestPOIId + 1);

        do
        {
            fields = points->Fetch();

            uint32 questId            = fields[0].GetUInt32();
            uint32 id                 = fields[1].GetUInt32();
            int32  x                  = fields[2].GetInt32();
            int32  y                  = fields[3].GetInt32();

            if (POIs[questId].size() <= id + 1)
                POIs[questId].resize(id + 10);

            QuestPOIBlobPoint point;
            point.X = x;
            point.Y = y;

            POIs[questId][id].push_back(point);
        } while (points->NextRow());
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 questId            = fields[0].GetUInt32();
        uint32 id                 = fields[1].GetUInt32();
        int32 objIndex            = fields[2].GetInt32();
        uint32 mapId              = fields[3].GetUInt32();
        uint32 WorldMapAreaId     = fields[4].GetUInt32();
        uint32 FloorId            = fields[5].GetUInt32();
        uint32 unk3               = fields[6].GetUInt32();
        uint32 unk4               = fields[7].GetUInt32();

        QuestPOIBlobData POI;
        POI.BlobIndex = id;
        POI.ObjectiveIndex = objIndex;
        POI.MapID = mapId;
        POI.WorldMapAreaID = WorldMapAreaId;
        POI.Floor = FloorId;
        POI.Unk3 = unk3;
        POI.Unk4 = unk4;

        if (questId < POIs.size() && id < POIs[questId].size())
        {
            POI.QuestPOIBlobPointStats = POIs[questId][id];

            auto itr = _questPOIStore.find(questId);
            if (itr == _questPOIStore.end())
            {
                QuestPOIWrapper wrapper;
                QuestPOIData data;
                data.QuestID = questId;
                wrapper.POIData = data;

                std::tie(itr, std::ignore) = _questPOIStore.emplace(questId, std::move(wrapper));
            }

            itr->second.POIData.QuestPOIBlobDataStats.push_back(POI);
        }
        else
            TC_LOG_ERROR("sql.sql", "Table quest_poi references unknown quest points for quest {} POI id {}", questId, id);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} quest POI definitions in {} ms", _questPOIStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadNPCSpellClickSpells()
{
    uint32 oldMSTime = getMSTime();

    _spellClickInfoStore.clear();
    //                                                0          1         2            3
    QueryResult result = WorldDatabase.Query("SELECT npc_entry, spell_id, cast_flags, user_type FROM npc_spellclick_spells");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spellclick spells. DB table `npc_spellclick_spells` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 npc_entry = fields[0].GetUInt32();
        CreatureTemplate const* cInfo = GetCreatureTemplate(npc_entry);
        if (!cInfo)
        {
            TC_LOG_ERROR("sql.sql", "Table npc_spellclick_spells references unknown creature_template {}. Skipping entry.", npc_entry);
            continue;
        }

        uint32 spellid = fields[1].GetUInt32();
        SpellInfo const* spellinfo = sSpellMgr->GetSpellInfo(spellid);
        if (!spellinfo)
        {
            TC_LOG_ERROR("sql.sql", "Table npc_spellclick_spells creature: {} references unknown spellid {}. Skipping entry.", npc_entry, spellid);
            continue;
        }

        uint8 userType = fields[3].GetUInt16();
        if (userType >= SPELL_CLICK_USER_MAX)
            TC_LOG_ERROR("sql.sql", "Table npc_spellclick_spells creature: {} references unknown user type {}. Skipping entry.", npc_entry, uint32(userType));

        uint8 castFlags = fields[2].GetUInt8();
        SpellClickInfo info;
        info.spellId = spellid;
        info.castFlags = castFlags;
        info.userType = SpellClickUserTypes(userType);
        _spellClickInfoStore.insert(SpellClickInfoContainer::value_type(npc_entry, info));

        ++count;
    }
    while (result->NextRow());

    // all spellclick data loaded, now we check if there are creatures with NPC_FLAG_SPELLCLICK but with no data
    // NOTE: It *CAN* be the other way around: no spellclick flag but with spellclick data, in case of creature-only vehicle accessories
    for (auto& creatureTemplatePair : _creatureTemplateStore)
    {
        if ((creatureTemplatePair.second.npcflag & UNIT_NPC_FLAG_SPELLCLICK) && !_spellClickInfoStore.count(creatureTemplatePair.first))
        {
            TC_LOG_ERROR("sql.sql", "npc_spellclick_spells: Creature template {} has UNIT_NPC_FLAG_SPELLCLICK but no data in spellclick table! Removing flag", creatureTemplatePair.first);
            creatureTemplatePair.second.npcflag &= ~UNIT_NPC_FLAG_SPELLCLICK;
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} spellclick definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::DeleteCreatureData(ObjectGuid::LowType guid)
{
    // remove mapid*cellid -> guid_set map
    CreatureData const* data = GetCreatureData(guid);
    if (data)
    {
        RemoveCreatureFromGrid(guid, data);
        OnDeleteSpawnData(data);
    }

    _creatureDataStore.erase(guid);
}

void ObjectMgr::DeleteGameObjectData(ObjectGuid::LowType guid)
{
    // remove mapid*cellid -> guid_set map
    GameObjectData const* data = GetGameObjectData(guid);
    if (data)
    {
        RemoveGameobjectFromGrid(guid, data);
        OnDeleteSpawnData(data);
    }

    _gameObjectDataStore.erase(guid);
}

void ObjectMgr::LoadQuestRelationsHelper(QuestRelations& map, std::string const& table)
{
    uint32 oldMSTime = getMSTime();

    map.clear();                                            // need for reload case

    uint32 count = 0;

    QueryResult result = WorldDatabase.PQuery("SELECT id, quest FROM {}", table);

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 quest relations from `{}`, table is empty.", table);
        return;
    }

    do
    {
        uint32 id     = result->Fetch()[0].GetUInt32();
        uint32 quest  = result->Fetch()[1].GetUInt32();

        if (!_questTemplates.count(quest))
        {
            TC_LOG_ERROR("sql.sql", "Table `{}`: Quest {} listed for entry {} does not exist.", table, quest, id);
            continue;
        }

        map.insert(QuestRelations::value_type(id, quest));
        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} quest relations from {} in {} ms", count, table, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadGameobjectQuestStarters()
{
    LoadQuestRelationsHelper(_goQuestRelations, "gameobject_queststarter");

    for (QuestRelations::iterator itr = _goQuestRelations.begin(); itr != _goQuestRelations.end(); ++itr)
    {
        GameObjectTemplate const* goInfo = GetGameObjectTemplate(itr->first);
        if (!goInfo)
            TC_LOG_ERROR("sql.sql", "Table `gameobject_queststarter` has data for nonexistent gameobject entry ({}) and existed quest {}", itr->first, itr->second);
        else if (goInfo->type != GAMEOBJECT_TYPE_QUESTGIVER)
            TC_LOG_ERROR("sql.sql", "Table `gameobject_queststarter` has data gameobject entry ({}) for quest {}, but GO is not GAMEOBJECT_TYPE_QUESTGIVER", itr->first, itr->second);
    }
}

void ObjectMgr::LoadGameobjectQuestEnders()
{
    LoadQuestRelationsHelper(_goQuestInvolvedRelations, "gameobject_questender");

    for (QuestRelations::iterator itr = _goQuestInvolvedRelations.begin(); itr != _goQuestInvolvedRelations.end(); ++itr)
    {
        GameObjectTemplate const* goInfo = GetGameObjectTemplate(itr->first);
        if (!goInfo)
            TC_LOG_ERROR("sql.sql", "Table `gameobject_questender` has data for nonexistent gameobject entry ({}) and existed quest {}", itr->first, itr->second);
        else if (goInfo->type != GAMEOBJECT_TYPE_QUESTGIVER)
            TC_LOG_ERROR("sql.sql", "Table `gameobject_questender` has data gameobject entry ({}) for quest {}, but GO is not GAMEOBJECT_TYPE_QUESTGIVER", itr->first, itr->second);
    }
}

void ObjectMgr::LoadCreatureQuestStarters()
{
    LoadQuestRelationsHelper(_creatureQuestRelations, "creature_queststarter");

    for (QuestRelations::iterator itr = _creatureQuestRelations.begin(); itr != _creatureQuestRelations.end(); ++itr)
    {
        CreatureTemplate const* cInfo = GetCreatureTemplate(itr->first);
        if (!cInfo)
            TC_LOG_ERROR("sql.sql", "Table `creature_queststarter` has data for nonexistent creature entry ({}) and existed quest {}", itr->first, itr->second);
        else if (!(cInfo->npcflag & UNIT_NPC_FLAG_QUESTGIVER))
            TC_LOG_ERROR("sql.sql", "Table `creature_queststarter` has creature entry ({}) for quest {}, but npcflag does not include UNIT_NPC_FLAG_QUESTGIVER", itr->first, itr->second);
    }
}

void ObjectMgr::LoadCreatureQuestEnders()
{
    LoadQuestRelationsHelper(_creatureQuestInvolvedRelations, "creature_questender");

    for (QuestRelations::iterator itr = _creatureQuestInvolvedRelations.begin(); itr != _creatureQuestInvolvedRelations.end(); ++itr)
    {
        CreatureTemplate const* cInfo = GetCreatureTemplate(itr->first);
        if (!cInfo)
            TC_LOG_ERROR("sql.sql", "Table `creature_questender` has data for nonexistent creature entry ({}) and existed quest {}", itr->first, itr->second);
        else if (!(cInfo->npcflag & UNIT_NPC_FLAG_QUESTGIVER))
            TC_LOG_ERROR("sql.sql", "Table `creature_questender` has creature entry ({}) for quest {}, but npcflag does not include UNIT_NPC_FLAG_QUESTGIVER", itr->first, itr->second);
    }
}

void QuestRelationResult::Iterator::_skip()
{
    while ((_it != _end) && !Quest::IsTakingQuestEnabled(_it->second))
        ++_it;
}

bool QuestRelationResult::HasQuest(uint32 questId) const
{
    return (std::find_if(_begin, _end, [questId](QuestRelations::value_type const& pair) { return (pair.second == questId); }) != _end) && (!_onlyActive || Quest::IsTakingQuestEnabled(questId));
}

void ObjectMgr::LoadReservedPlayersNames()
{
    uint32 oldMSTime = getMSTime();

    _reservedNamesStore.clear();                                // need for reload case

    QueryResult result = CharacterDatabase.Query("SELECT name FROM reserved_name");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 reserved player names. DB table `reserved_name` is empty!");
        return;
    }

    uint32 count = 0;

    Field* fields;
    do
    {
        fields = result->Fetch();
        std::string name= fields[0].GetString();

        std::wstring wstr;
        if (!Utf8toWStr (name, wstr))
        {
            TC_LOG_ERROR("misc", "Table `reserved_name` has invalid name: {}", name);
            continue;
        }

        wstrToLower(wstr);

        _reservedNamesStore.insert(wstr);
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} reserved player names in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

bool ObjectMgr::IsReservedName(std::string_view name) const
{
    std::wstring wstr;
    if (!Utf8toWStr (name, wstr))
        return false;

    wstrToLower(wstr);

    return _reservedNamesStore.find(wstr) != _reservedNamesStore.end();
}

enum LanguageType
{
    LT_BASIC_LATIN    = 0x0000,
    LT_EXTENDEN_LATIN = 0x0001,
    LT_CYRILLIC       = 0x0002,
    LT_EAST_ASIA      = 0x0004,
    LT_ANY            = 0xFFFF
};

static LanguageType GetRealmLanguageType(bool create)
{
    switch (sWorld->getIntConfig(CONFIG_REALM_ZONE))
    {
        case REALM_ZONE_UNKNOWN:                            // any language
        case REALM_ZONE_DEVELOPMENT:
        case REALM_ZONE_TEST_SERVER:
        case REALM_ZONE_QA_SERVER:
            return LT_ANY;
        case REALM_ZONE_UNITED_STATES:                      // extended-Latin
        case REALM_ZONE_OCEANIC:
        case REALM_ZONE_LATIN_AMERICA:
        case REALM_ZONE_ENGLISH:
        case REALM_ZONE_GERMAN:
        case REALM_ZONE_FRENCH:
        case REALM_ZONE_SPANISH:
            return LT_EXTENDEN_LATIN;
        case REALM_ZONE_KOREA:                              // East-Asian
        case REALM_ZONE_TAIWAN:
        case REALM_ZONE_CHINA:
            return LT_EAST_ASIA;
        case REALM_ZONE_RUSSIAN:                            // Cyrillic
            return LT_CYRILLIC;
        default:
            return create ? LT_BASIC_LATIN : LT_ANY;        // basic-Latin at create, any at login
    }
}

bool isValidString(const std::wstring& wstr, uint32 strictMask, bool numericOrSpace, bool create = false)
{
    if (strictMask == 0)                                       // any language, ignore realm
    {
        if (isExtendedLatinString(wstr, numericOrSpace))
            return true;
        if (isCyrillicString(wstr, numericOrSpace))
            return true;
        if (isEastAsianString(wstr, numericOrSpace))
            return true;
        return false;
    }

    if (strictMask & 0x2)                                    // realm zone specific
    {
        LanguageType lt = GetRealmLanguageType(create);
        if (lt & LT_EXTENDEN_LATIN)
            if (isExtendedLatinString(wstr, numericOrSpace))
                return true;
        if (lt & LT_CYRILLIC)
            if (isCyrillicString(wstr, numericOrSpace))
                return true;
        if (lt & LT_EAST_ASIA)
            if (isEastAsianString(wstr, numericOrSpace))
                return true;
    }

    if (strictMask & 0x1)                                    // basic Latin
    {
        if (isBasicLatinString(wstr, numericOrSpace))
            return true;
    }

    return false;
}

ResponseCodes ObjectMgr::CheckPlayerName(std::string_view name, LocaleConstant locale, bool create /*= false*/)
{
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return CHAR_NAME_INVALID_CHARACTER;

    if (wname.size() > MAX_PLAYER_NAME)
        return CHAR_NAME_TOO_LONG;

    uint32 minName = sWorld->getIntConfig(CONFIG_MIN_PLAYER_NAME);
    if (wname.size() < minName)
        return CHAR_NAME_TOO_SHORT;

    uint32 strictMask = sWorld->getIntConfig(CONFIG_STRICT_PLAYER_NAMES);
    if (!isValidString(wname, strictMask, false, create))
        return CHAR_NAME_MIXED_LANGUAGES;

    wstrToLower(wname);
    for (size_t i = 2; i < wname.size(); ++i)
        if (wname[i] == wname[i-1] && wname[i] == wname[i-2])
            return CHAR_NAME_THREE_CONSECUTIVE;

    return ValidateName(wname, locale);
}

bool ObjectMgr::IsValidCharterName(std::string_view name)
{
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return false;

    if (wname.size() > MAX_CHARTER_NAME)
        return false;

    uint32 minName = sWorld->getIntConfig(CONFIG_MIN_CHARTER_NAME);
    if (wname.size() < minName)
        return false;

    uint32 strictMask = sWorld->getIntConfig(CONFIG_STRICT_CHARTER_NAMES);

    return isValidString(wname, strictMask, true);
}

PetNameInvalidReason ObjectMgr::CheckPetName(std::string_view name, LocaleConstant locale)
{
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return PET_NAME_INVALID;

    if (wname.size() > MAX_PET_NAME)
        return PET_NAME_TOO_LONG;

    uint32 minName = sWorld->getIntConfig(CONFIG_MIN_PET_NAME);
    if (wname.size() < minName)
        return PET_NAME_TOO_SHORT;

    uint32 strictMask = sWorld->getIntConfig(CONFIG_STRICT_PET_NAMES);
    if (!isValidString(wname, strictMask, false))
        return PET_NAME_MIXED_LANGUAGES;

    switch (ValidateName(wname, locale))
    {
        case CHAR_NAME_PROFANE:
            return PET_NAME_PROFANE;
        case CHAR_NAME_RESERVED:
            return PET_NAME_RESERVED;
        case RESPONSE_FAILURE:
            return PET_NAME_INVALID;
        default:
            break;
    }
    return PET_NAME_SUCCESS;
}

void ObjectMgr::LoadGameObjectForQuests()
{
    uint32 oldMSTime = getMSTime();

    _gameObjectForQuestStore.clear();                         // need for reload case

    if (_gameObjectTemplateStore.empty())
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 GameObjects for quests");
        return;
    }

    uint32 count = 0;

    // collect GO entries for GO that must activated
    for (auto const& gameObjectTemplatePair : _gameObjectTemplateStore)
    {
        switch (gameObjectTemplatePair.second.type)
        {
            case GAMEOBJECT_TYPE_QUESTGIVER:
                break;
            case GAMEOBJECT_TYPE_CHEST:
            {
                // scan GO chest with loot including quest items
                uint32 lootId = gameObjectTemplatePair.second.GetLootId();
                // find quest loot for GO
                if (gameObjectTemplatePair.second.chest.questId || LootTemplates_Gameobject.HaveQuestLootFor(lootId))
                    break;
                continue;
            }
            case GAMEOBJECT_TYPE_GENERIC:
            {
                if (gameObjectTemplatePair.second._generic.questID > 0)            //quests objects
                    break;
                continue;
            }
            case GAMEOBJECT_TYPE_GOOBER:
            {
                if (gameObjectTemplatePair.second.goober.questId > 0)              //quests objects
                    break;
                continue;
            }
            default:
                continue;
        }

        _gameObjectForQuestStore.insert(gameObjectTemplatePair.first);
        ++count;
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} GameObjects for quests in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

bool ObjectMgr::LoadTrinityStrings()
{
    uint32 oldMSTime = getMSTime();

    _trinityStringStore.clear(); // for reload case

    QueryResult result = WorldDatabase.Query("SELECT entry, content_default, content_loc1, content_loc2, content_loc3, content_loc4, content_loc5, content_loc6, content_loc7, content_loc8 FROM trinity_string");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 trinity strings. DB table `trinity_string` is empty. You have imported an incorrect database for more info search for TCE00003 on forum.");
        return false;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        TrinityString& data = _trinityStringStore[entry];

        data.Content.resize(DEFAULT_LOCALE + 1);

        for (int8 i = TOTAL_LOCALES - 1; i >= 0; --i)
            AddLocaleString(fields[i + 1].GetString(), LocaleConstant(i), data.Content);
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} trinity strings in {} ms", _trinityStringStore.size(), GetMSTimeDiffToNow(oldMSTime));
    return true;
}

char const* ObjectMgr::GetTrinityString(uint32 entry, LocaleConstant locale) const
{
    if (TrinityString const* ts = GetTrinityString(entry))
    {
        if (ts->Content.size() > size_t(locale) && !ts->Content[locale].empty())
            return ts->Content[locale].c_str();
        return ts->Content[DEFAULT_LOCALE].c_str();
    }

    TC_LOG_ERROR("sql.sql", "Trinity string entry {} not found in DB.", entry);
    return "<error>";
}

void ObjectMgr::LoadFishingBaseSkillLevel()
{
    uint32 oldMSTime = getMSTime();

    _fishingBaseForAreaStore.clear();                            // for reload case

    QueryResult result = WorldDatabase.Query("SELECT entry, skill FROM skill_fishing_base_level");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 areas for fishing base skill level. DB table `skill_fishing_base_level` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 entry  = fields[0].GetUInt32();
        int32 skill   = fields[1].GetInt16();

        AreaTableEntry const* fArea = sAreaTableStore.LookupEntry(entry);
        if (!fArea)
        {
            TC_LOG_ERROR("sql.sql", "AreaId {} defined in `skill_fishing_base_level` does not exist", entry);
            continue;
        }

        _fishingBaseForAreaStore[entry] = skill;
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} areas for fishing base skill level in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

bool ObjectMgr::CheckDeclinedNames(const std::wstring& w_ownname, DeclinedName const& names)
{
    // get main part of the name
    std::wstring mainpart = GetMainPartOfName(w_ownname, 0);
    // prepare flags
    bool x = true;
    bool y = true;

    // check declined names
    for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
    {
        std::wstring wname;
        if (!Utf8toWStr(names.name[i], wname))
            return false;

        if (mainpart != GetMainPartOfName(wname, i+1))
            x = false;

        if (w_ownname != wname)
            y = false;
    }
    return (x || y);
}

uint32 ObjectMgr::GetAreaTriggerScriptId(uint32 trigger_id) const
{
    AreaTriggerScriptContainer::const_iterator i = _areaTriggerScriptStore.find(trigger_id);
    if (i!= _areaTriggerScriptStore.end())
        return i->second;
    return 0;
}

SpellScriptsBounds ObjectMgr::GetSpellScriptsBounds(uint32 spellId)
{
    return _spellScriptsStore.equal_range(spellId);
}

// this allows calculating base reputations to offline players, just by race and class
int32 ObjectMgr::GetBaseReputationOf(FactionEntry const* factionEntry, uint8 race, uint8 playerClass) const
{
    if (!factionEntry)
        return 0;

    uint32 raceMask = (1 << (race - 1));
    uint32 classMask = (1 << (playerClass-1));

    for (uint8 i = 0; i < 4; ++i)
    {
        if ((!factionEntry->ReputationClassMask[i] ||
            factionEntry->ReputationClassMask[i] & classMask) &&
            (!factionEntry->ReputationRaceMask[i] ||
            factionEntry->ReputationRaceMask[i] & raceMask))
            return factionEntry->ReputationBase[i];
    }

    return 0;
}

SkillRangeType GetSkillRangeType(SkillRaceClassInfoEntry const* rcEntry)
{
    SkillLineEntry const* skill = sSkillLineStore.LookupEntry(rcEntry->SkillID);
    if (!skill)
        return SKILL_RANGE_NONE;

    if (sSkillTiersStore.LookupEntry(rcEntry->SkillTierID))
        return SKILL_RANGE_RANK;

    if (rcEntry->SkillID == SKILL_RUNEFORGING)
        return SKILL_RANGE_MONO;

    switch (skill->CategoryID)
    {
        case SKILL_CATEGORY_ARMOR:
            return SKILL_RANGE_MONO;
        case SKILL_CATEGORY_LANGUAGES:
            return SKILL_RANGE_LANGUAGE;
    }

    return SKILL_RANGE_LEVEL;
}

void ObjectMgr::LoadGameTele()
{
    uint32 oldMSTime = getMSTime();

    _gameTeleStore.clear();                                  // for reload case

    //                                                0       1           2           3           4        5     6
    QueryResult result = WorldDatabase.Query("SELECT id, position_x, position_y, position_z, orientation, map, name FROM game_tele");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 GameTeleports. DB table `game_tele` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 id         = fields[0].GetUInt32();

        GameTele gt;

        gt.position_x     = fields[1].GetFloat();
        gt.position_y     = fields[2].GetFloat();
        gt.position_z     = fields[3].GetFloat();
        gt.orientation    = fields[4].GetFloat();
        gt.mapId          = fields[5].GetUInt16();
        gt.name           = fields[6].GetString();

        if (!MapManager::IsValidMapCoord(gt.mapId, gt.position_x, gt.position_y, gt.position_z, gt.orientation))
        {
            TC_LOG_ERROR("sql.sql", "Wrong position for id {} (name: {}) in `game_tele` table, ignoring.", id, gt.name);
            continue;
        }

        if (!Utf8toWStr(gt.name, gt.wnameLow))
        {
            TC_LOG_ERROR("sql.sql", "Wrong UTF8 name for id {} in `game_tele` table, ignoring.", id);
            continue;
        }

        wstrToLower(gt.wnameLow);

        _gameTeleStore[id] = gt;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} GameTeleports in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

GameTele const* ObjectMgr::GetGameTele(std::string_view name) const
{
    // explicit name case
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return nullptr;

    // converting string that we try to find to lower case
    wstrToLower(wname);

    // Alternative first GameTele what contains wnameLow as substring in case no GameTele location found
    GameTele const* alt = nullptr;
    for (GameTeleContainer::const_iterator itr = _gameTeleStore.begin(); itr != _gameTeleStore.end(); ++itr)
    {
        if (itr->second.wnameLow == wname)
            return &itr->second;
        else if (!alt && itr->second.wnameLow.find(wname) != std::wstring::npos)
            alt = &itr->second;
    }

    return alt;
}

GameTele const* ObjectMgr::GetGameTeleExactName(std::string_view name) const
{
    // explicit name case
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return nullptr;

    // converting string that we try to find to lower case
    wstrToLower(wname);

    for (GameTeleContainer::const_iterator itr = _gameTeleStore.begin(); itr != _gameTeleStore.end(); ++itr)
    {
        if (itr->second.wnameLow == wname)
            return &itr->second;
    }

    return nullptr;
}

bool ObjectMgr::AddGameTele(GameTele& tele)
{
    // find max id
    uint32 new_id = 0;
    for (GameTeleContainer::const_iterator itr = _gameTeleStore.begin(); itr != _gameTeleStore.end(); ++itr)
        if (itr->first > new_id)
            new_id = itr->first;

    // use next
    ++new_id;

    if (!Utf8toWStr(tele.name, tele.wnameLow))
        return false;

    wstrToLower(tele.wnameLow);

    _gameTeleStore[new_id] = tele;

    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_GAME_TELE);

    stmt->setUInt32(0, new_id);
    stmt->setFloat(1, tele.position_x);
    stmt->setFloat(2, tele.position_y);
    stmt->setFloat(3, tele.position_z);
    stmt->setFloat(4, tele.orientation);
    stmt->setUInt16(5, uint16(tele.mapId));
    stmt->setString(6, tele.name);

    WorldDatabase.Execute(stmt);

    return true;
}

bool ObjectMgr::DeleteGameTele(std::string_view name)
{
    // explicit name case
    std::wstring wname;
    if (!Utf8toWStr(name, wname))
        return false;

    // converting string that we try to find to lower case
    wstrToLower(wname);

    for (GameTeleContainer::iterator itr = _gameTeleStore.begin(); itr != _gameTeleStore.end(); ++itr)
    {
        if (itr->second.wnameLow == wname)
        {
            WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAME_TELE);

            stmt->setString(0, itr->second.name);

            WorldDatabase.Execute(stmt);

            _gameTeleStore.erase(itr);
            return true;
        }
    }

    return false;
}

void ObjectMgr::LoadMailLevelRewards()
{
    uint32 oldMSTime = getMSTime();

    _mailLevelRewardStore.clear();                           // for reload case

    //                                                 0        1             2            3
    QueryResult result = WorldDatabase.Query("SELECT level, raceMask, mailTemplateId, senderEntry FROM mail_level_reward");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 level dependent mail rewards. DB table `mail_level_reward` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint8 level           = fields[0].GetUInt8();
        uint32 raceMask       = fields[1].GetUInt32();
        uint32 mailTemplateId = fields[2].GetUInt32();
        uint32 senderEntry    = fields[3].GetUInt32();

        if (level > MAX_LEVEL)
        {
            TC_LOG_ERROR("sql.sql", "Table `mail_level_reward` has data for level {} that more supported by client ({}), ignoring.", level, MAX_LEVEL);
            continue;
        }

        if (!(raceMask & RACEMASK_ALL_PLAYABLE))
        {
            TC_LOG_ERROR("sql.sql", "Table `mail_level_reward` has raceMask ({}) for level {} that not include any player races, ignoring.", raceMask, level);
            continue;
        }

        if (!sMailTemplateStore.LookupEntry(mailTemplateId))
        {
            TC_LOG_ERROR("sql.sql", "Table `mail_level_reward` has invalid mailTemplateId ({}) for level {} that invalid not include any player races, ignoring.", mailTemplateId, level);
            continue;
        }

        if (!GetCreatureTemplate(senderEntry))
        {
            TC_LOG_ERROR("sql.sql", "Table `mail_level_reward` has nonexistent sender creature entry ({}) for level {} that invalid not include any player races, ignoring.", senderEntry, level);
            continue;
        }

        _mailLevelRewardStore[level].push_back(MailLevelReward(raceMask, mailTemplateId, senderEntry));

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} level dependent mail rewards in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadTrainers()
{
    uint32 oldMSTime = getMSTime();

    // For reload case
    _trainers.clear();

    std::unordered_map<int32, std::vector<Trainer::Spell>> spellsByTrainer;
    if (QueryResult trainerSpellsResult = WorldDatabase.Query("SELECT TrainerId, SpellId, MoneyCost, ReqSkillLine, ReqSkillRank, ReqAbility1, ReqAbility2, ReqAbility3, ReqLevel FROM trainer_spell"))
    {
        do
        {
            Field* fields = trainerSpellsResult->Fetch();

            Trainer::Spell spell;
            uint32 trainerId = fields[0].GetUInt32();
            spell.SpellId = fields[1].GetUInt32();
            spell.MoneyCost = fields[2].GetUInt32();
            spell.ReqSkillLine = fields[3].GetUInt32();
            spell.ReqSkillRank = fields[4].GetUInt32();
            spell.ReqAbility[0] = fields[5].GetUInt32();
            spell.ReqAbility[1] = fields[6].GetUInt32();
            spell.ReqAbility[2] = fields[7].GetUInt32();
            spell.ReqLevel = fields[8].GetUInt8();

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell.SpellId);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "Table `trainer_spell` references non-existing spell (SpellId: {}) for TrainerId {}, ignoring", spell.SpellId, trainerId);
                continue;
            }

            if (GetTalentSpellCost(spell.SpellId))
            {
                TC_LOG_ERROR("sql.sql", "Table `trainer_spell` references non-existing spell (SpellId: {}) which is a talent, for TrainerId {}, ignoring", spell.SpellId, trainerId);
                continue;
            }

            if (spell.ReqSkillLine && !sSkillLineStore.LookupEntry(spell.ReqSkillLine))
            {
                TC_LOG_ERROR("sql.sql", "Table `trainer_spell` references non-existing skill (ReqSkillLine: {}) for TrainerId {} and SpellId {}, ignoring",
                    spell.ReqSkillLine, trainerId, spell.SpellId);
                continue;
            }

            bool allReqValid = true;
            for (std::size_t i = 0; i < spell.ReqAbility.size(); ++i)
            {
                uint32 requiredSpell = spell.ReqAbility[i];
                if (requiredSpell && !sSpellMgr->GetSpellInfo(requiredSpell))
                {
                    TC_LOG_ERROR("sql.sql", "Table `trainer_spell` references non-existing spell (ReqAbility{}: {}) for TrainerId {} and SpellId {}, ignoring",
                        i + 1, requiredSpell, trainerId, spell.SpellId);
                    allReqValid = false;
                }
            }

            if (!allReqValid)
                continue;

            spellsByTrainer[trainerId].push_back(spell);
        } while (trainerSpellsResult->NextRow());
    }

    if (QueryResult trainersResult = WorldDatabase.Query("SELECT Id, Type, Requirement, Greeting FROM trainer"))
    {
        do
        {
            Field* fields = trainersResult->Fetch();

            uint32 trainerId = fields[0].GetUInt32();
            Trainer::Type trainerType = Trainer::Type(fields[1].GetUInt8());
            uint32 requirement = fields[2].GetUInt32();
            std::string greeting = fields[3].GetString();
            std::vector<Trainer::Spell> spells;
            auto spellsItr = spellsByTrainer.find(trainerId);
            if (spellsItr != spellsByTrainer.end())
            {
                spells = std::move(spellsItr->second);
                spellsByTrainer.erase(spellsItr);
            }

            switch (trainerType)
            {
                case Trainer::Type::Class:
                case Trainer::Type::Pet:
                    if (requirement && !sChrClassesStore.LookupEntry(requirement))
                    {
                        TC_LOG_ERROR("sql.sql", "Table `trainer` references non-existing class requirement {} for TrainerId {}, ignoring", requirement, trainerId);
                        continue;
                    }
                    break;
                case Trainer::Type::Mount:
                    if (requirement && !sChrRacesStore.LookupEntry(requirement))
                    {
                        TC_LOG_ERROR("sql.sql", "Table `trainer` references non-existing race requirement {} for TrainerId {}, ignoring", requirement, trainerId);
                        continue;
                    }
                    break;
                case Trainer::Type::Tradeskill:
                    if (requirement && !sSpellMgr->GetSpellInfo(requirement))
                    {
                        TC_LOG_ERROR("sql.sql", "Table `trainer` references non-existing spell requirement {} for TrainerId {}, ignoring", requirement, trainerId);
                        continue;
                    }
                    break;
                default:
                    break;
            }

            auto [it, isNew] = _trainers.emplace(std::piecewise_construct, std::forward_as_tuple(trainerId), std::forward_as_tuple(trainerId, trainerType, requirement, std::move(greeting), std::move(spells)));
            ASSERT(isNew);
            if (trainerType == Trainer::Type::Class)
                _classTrainers[requirement].push_back(&it->second);
        } while (trainersResult->NextRow());
    }

    for (auto const& unusedSpells : spellsByTrainer)
    {
        for (Trainer::Spell const& unusedSpell : unusedSpells.second)
        {
            TC_LOG_ERROR("sql.sql", "Table `trainer_spell` references non-existing trainer (TrainerId: {}) for SpellId {}, ignoring", unusedSpells.first, unusedSpell.SpellId);
        }
    }

    if (QueryResult trainerLocalesResult = WorldDatabase.Query("SELECT Id, locale, Greeting_lang FROM trainer_locale"))
    {
        do
        {
            Field* fields = trainerLocalesResult->Fetch();
            uint32 trainerId = fields[0].GetUInt32();
            std::string localeName = fields[1].GetString();

            LocaleConstant locale = GetLocaleByName(localeName);
            if (locale == LOCALE_enUS)
                continue;

            if (Trainer::Trainer* trainer = Trinity::Containers::MapGetValuePtr(_trainers, trainerId))
                trainer->AddGreetingLocale(locale, fields[2].GetString());
            else
                TC_LOG_ERROR("sql.sql", "Table `trainer_locale` references non-existing trainer (TrainerId: {}) for locale {}, ignoring",
                    trainerId, localeName);
        } while (trainerLocalesResult->NextRow());
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} Trainers in {} ms", _trainers.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureDefaultTrainers()
{
    uint32 oldMSTime = getMSTime();

    _creatureDefaultTrainers.clear();

    if (QueryResult result = WorldDatabase.Query("SELECT CreatureId, TrainerId FROM creature_default_trainer"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 creatureId = fields[0].GetUInt32();
            uint32 trainerId = fields[1].GetUInt32();

            if (!GetCreatureTemplate(creatureId))
            {
                TC_LOG_ERROR("sql.sql", "Table `creature_default_trainer` references non-existing creature template (CreatureId: {}), ignoring", creatureId);
                continue;
            }

            Trainer::Trainer* trainer = Trinity::Containers::MapGetValuePtr(_trainers, trainerId);
            if (!trainer)
            {
                TC_LOG_ERROR("sql.sql", "Table `creature_default_trainer` references non-existing trainer (TrainerId: {}) for CreatureId {}, ignoring", trainerId, creatureId);
                continue;
            }

            _creatureDefaultTrainers[creatureId] = trainer;

        } while (result->NextRow());
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} default trainers in {} ms", _creatureDefaultTrainers.size(), GetMSTimeDiffToNow(oldMSTime));
}

uint32 ObjectMgr::LoadReferenceVendor(int32 vendor, int32 item, std::set<uint32>* skip_vendors)
{
    // find all items from the reference vendor
    WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_NPC_VENDOR_REF);
    stmt->setUInt32(0, uint32(item));
    PreparedQueryResult result = WorldDatabase.Query(stmt);

    if (!result)
        return 0;

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        int32 item_id = fields[0].GetInt32();

        // if item is a negative, its a reference
        if (item_id < 0)
            count += LoadReferenceVendor(vendor, -item_id, skip_vendors);
        else
        {
            int32  maxcount     = fields[1].GetUInt8();
            uint32 incrtime     = fields[2].GetUInt32();
            uint32 ExtendedCost = fields[3].GetUInt32();

            if (!IsVendorItemValid(vendor, item_id, maxcount, incrtime, ExtendedCost, nullptr, skip_vendors))
                continue;

            VendorItemData& vList = _cacheVendorItemStore[vendor];

            vList.AddItem(item_id, maxcount, incrtime, ExtendedCost);
            ++count;
        }
    } while (result->NextRow());

    return count;
}

void ObjectMgr::LoadVendors()
{
    uint32 oldMSTime = getMSTime();

    // For reload case
    for (CacheVendorItemContainer::iterator itr = _cacheVendorItemStore.begin(); itr != _cacheVendorItemStore.end(); ++itr)
        itr->second.Clear();
    _cacheVendorItemStore.clear();

    std::set<uint32> skip_vendors;

    QueryResult result = WorldDatabase.Query("SELECT entry, item, maxcount, incrtime, ExtendedCost FROM npc_vendor ORDER BY entry, slot ASC");
    if (!result)
    {

        TC_LOG_ERROR("sql.sql", ">>  Loaded 0 Vendors. DB table `npc_vendor` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry        = fields[0].GetUInt32();
        int32 item_id      = fields[1].GetInt32();

        // if item is a negative, its a reference
        if (item_id < 0)
            count += LoadReferenceVendor(entry, -item_id, &skip_vendors);
        else
        {
            uint32 maxcount     = fields[2].GetUInt8();
            uint32 incrtime     = fields[3].GetUInt32();
            uint32 ExtendedCost = fields[4].GetUInt32();

            if (!IsVendorItemValid(entry, item_id, maxcount, incrtime, ExtendedCost, nullptr, &skip_vendors))
                continue;

            VendorItemData& vList = _cacheVendorItemStore[entry];

            vList.AddItem(item_id, maxcount, incrtime, ExtendedCost);
            ++count;
        }
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Vendors in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadGossipMenu()
{
    uint32 oldMSTime = getMSTime();

    _gossipMenusStore.clear();

    //                                               0       1
    QueryResult result = WorldDatabase.Query("SELECT MenuID, TextID FROM gossip_menu");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gossip_menu IDs. DB table `gossip_menu` is empty!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        GossipMenus gMenu;

        gMenu.MenuID = fields[0].GetUInt16();
        gMenu.TextID = fields[1].GetUInt32();

        if (!GetGossipText(gMenu.TextID))
        {
            TC_LOG_ERROR("sql.sql", "Table gossip_menu: ID {} is using non-existing TextID {}", gMenu.MenuID, gMenu.TextID);
            continue;
        }

        _gossipMenusStore.insert(GossipMenusContainer::value_type(gMenu.MenuID, gMenu));
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gossip_menu IDs in {} ms", uint32(_gossipMenusStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadGossipMenuItems()
{
    uint32 oldMSTime = getMSTime();

    _gossipMenuItemsStore.clear();

    QueryResult result = WorldDatabase.Query(
        //      0       1       2           3           4                      5           6              7             8            9         10        11       12
        "SELECT MenuID, OptionID, OptionIcon, OptionText, OptionBroadcastTextID, OptionType, OptionNpcFlag, ActionMenuID, ActionPoiID, BoxCoded, BoxMoney, BoxText, BoxBroadcastTextID "
        "FROM gossip_menu_option ORDER BY MenuID, OptionID");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gossip_menu_option IDs. DB table `gossip_menu_option` is empty!");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        GossipMenuItems gMenuItem;

        gMenuItem.MenuID                = fields[0].GetUInt16();
        gMenuItem.OptionID              = fields[1].GetUInt16();
        gMenuItem.OptionIcon            = GossipOptionIcon(fields[2].GetUInt32());
        gMenuItem.OptionText            = fields[3].GetString();
        gMenuItem.OptionBroadcastTextID = fields[4].GetUInt32();
        gMenuItem.OptionType            = fields[5].GetUInt8();
        gMenuItem.OptionNpcFlag         = fields[6].GetUInt32();
        gMenuItem.ActionMenuID          = fields[7].GetUInt32();
        gMenuItem.ActionPoiID           = fields[8].GetUInt32();
        gMenuItem.BoxCoded              = fields[9].GetBool();
        gMenuItem.BoxMoney              = fields[10].GetUInt32();
        gMenuItem.BoxText               = fields[11].GetString();
        gMenuItem.BoxBroadcastTextID    = fields[12].GetUInt32();

        if (gMenuItem.OptionIcon >= GOSSIP_ICON_MAX)
        {
            TC_LOG_ERROR("sql.sql", "Table `gossip_menu_option` for menu {}, id {} has unknown icon id {}. Replacing with GOSSIP_ICON_CHAT", gMenuItem.MenuID, gMenuItem.OptionID, gMenuItem.OptionIcon);
            gMenuItem.OptionIcon = GOSSIP_ICON_CHAT;
        }

        if (gMenuItem.OptionBroadcastTextID)
        {
            if (!GetBroadcastText(gMenuItem.OptionBroadcastTextID))
            {
                TC_LOG_ERROR("sql.sql", "Table `gossip_menu_option` for menu {}, id {} has non-existing or incompatible OptionBroadcastTextID {}, ignoring.", gMenuItem.MenuID, gMenuItem.OptionID, gMenuItem.OptionBroadcastTextID);
                gMenuItem.OptionBroadcastTextID = 0;
            }
        }

        if (gMenuItem.OptionType >= GOSSIP_OPTION_MAX)
            TC_LOG_ERROR("sql.sql", "Table `gossip_menu_option` for menu {}, id {} has unknown option id {}. Option will not be used", gMenuItem.MenuID, gMenuItem.OptionID, gMenuItem.OptionType);

        if (gMenuItem.ActionPoiID && !GetPointOfInterest(gMenuItem.ActionPoiID))
        {
            TC_LOG_ERROR("sql.sql", "Table `gossip_menu_option` for menu {}, id {} use non-existing ActionPoiID {}, ignoring", gMenuItem.MenuID, gMenuItem.OptionID, gMenuItem.ActionPoiID);
            gMenuItem.ActionPoiID = 0;
        }

        if (gMenuItem.BoxBroadcastTextID)
        {
            if (!GetBroadcastText(gMenuItem.BoxBroadcastTextID))
            {
                TC_LOG_ERROR("sql.sql", "Table `gossip_menu_option` for menu {}, id {} has non-existing or incompatible BoxBroadcastTextID {}, ignoring.", gMenuItem.MenuID, gMenuItem.OptionID, gMenuItem.BoxBroadcastTextID);
                gMenuItem.BoxBroadcastTextID = 0;
            }
        }

        _gossipMenuItemsStore.insert(GossipMenuItemsContainer::value_type(gMenuItem.MenuID, gMenuItem));
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gossip_menu_option entries in {} ms", uint32(_gossipMenuItemsStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

Trainer::Trainer const* ObjectMgr::GetTrainer(uint32 creatureId) const
{
    auto itr = _creatureDefaultTrainers.find(creatureId);
    if (itr != _creatureDefaultTrainers.end())
        return itr->second;

    return nullptr;
}

void ObjectMgr::AddVendorItem(uint32 entry, uint32 item, int32 maxcount, uint32 incrtime, uint32 extendedCost, bool persist /*= true*/)
{
    VendorItemData& vList = _cacheVendorItemStore[entry];
    vList.AddItem(item, maxcount, incrtime, extendedCost);

    if (persist)
    {
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_NPC_VENDOR);

        stmt->setUInt32(0, entry);
        stmt->setUInt32(1, item);
        stmt->setUInt8(2, maxcount);
        stmt->setUInt32(3, incrtime);
        stmt->setUInt32(4, extendedCost);

        WorldDatabase.Execute(stmt);
    }
}

bool ObjectMgr::RemoveVendorItem(uint32 entry, uint32 item, bool persist /*= true*/)
{
    CacheVendorItemContainer::iterator iter = _cacheVendorItemStore.find(entry);
    if (iter == _cacheVendorItemStore.end())
        return false;

    if (!iter->second.RemoveItem(item))
        return false;

    if (persist)
    {
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_NPC_VENDOR);

        stmt->setUInt32(0, entry);
        stmt->setUInt32(1, item);

        WorldDatabase.Execute(stmt);
    }

    return true;
}

bool ObjectMgr::IsVendorItemValid(uint32 vendor_entry, uint32 id, int32 maxcount, uint32 incrtime, uint32 ExtendedCost, Player* player, std::set<uint32>* skip_vendors, uint32 ORnpcflag) const
{
    CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(vendor_entry);
    if (!cInfo)
    {
        if (player)
            ChatHandler(player->GetSession()).SendSysMessage(LANG_COMMAND_VENDORSELECTION);
        else
            TC_LOG_ERROR("sql.sql", "Table `(game_event_)npc_vendor` has data for nonexistent creature template (Entry: {}), ignore", vendor_entry);
        return false;
    }

    if (!((cInfo->npcflag | ORnpcflag) & UNIT_NPC_FLAG_VENDOR))
    {
        if (!skip_vendors || skip_vendors->count(vendor_entry) == 0)
        {
            if (player)
                ChatHandler(player->GetSession()).SendSysMessage(LANG_COMMAND_VENDORSELECTION);
            else
                TC_LOG_ERROR("sql.sql", "Table `(game_event_)npc_vendor` has data for creature template (Entry: {}) without vendor flag, ignore", vendor_entry);

            if (skip_vendors)
                skip_vendors->insert(vendor_entry);
        }
        return false;
    }

    if (!sObjectMgr->GetItemTemplate(id))
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ITEM_NOT_FOUND, id);
        else
            TC_LOG_ERROR("sql.sql", "Table `(game_event_)npc_vendor` for Vendor (Entry: {}) have in item list non-existed item ({}), ignore", vendor_entry, id);
        return false;
    }

    if (ExtendedCost && !sItemExtendedCostStore.LookupEntry(ExtendedCost))
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_EXTENDED_COST_NOT_EXIST, ExtendedCost);
        else
            TC_LOG_ERROR("sql.sql", "Table `(game_event_)npc_vendor` has Item (Entry: {}) with wrong ExtendedCost ({}) for vendor ({}), ignore", id, ExtendedCost, vendor_entry);
        return false;
    }

    if (maxcount > 0 && incrtime == 0)
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage("MaxCount != 0 (%u) but IncrTime == 0", maxcount);
        else
            TC_LOG_ERROR("sql.sql", "Table `(game_event_)npc_vendor` has `maxcount` ({}) for item {} of vendor (Entry: {}) but `incrtime`=0, ignore", maxcount, id, vendor_entry);
        return false;
    }
    else if (maxcount == 0 && incrtime > 0)
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage("MaxCount == 0 but IncrTime<>= 0");
        else
            TC_LOG_ERROR("sql.sql", "Table `(game_event_)npc_vendor` has `maxcount`=0 for item {} of vendor (Entry: {}) but `incrtime`<>0, ignore", id, vendor_entry);
        return false;
    }

    VendorItemData const* vItems = GetNpcVendorItemList(vendor_entry);
    if (!vItems)
        return true;                                        // later checks for non-empty lists

    if (vItems->FindItemCostPair(id, ExtendedCost))
    {
        if (player)
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ITEM_ALREADY_IN_LIST, id, ExtendedCost);
        else
            TC_LOG_ERROR("sql.sql", "Table `npc_vendor` has duplicate items {} (with extended cost {}) for vendor (Entry: {}), ignoring", id, ExtendedCost, vendor_entry);
        return false;
    }

    return true;
}

void ObjectMgr::LoadScriptNames()
{
    uint32 oldMSTime = getMSTime();

    // We insert an empty placeholder here so we can use the
    // script id 0 as dummy for "no script found".
    _scriptNamesStore.emplace_back("");

    QueryResult result = WorldDatabase.Query(
        "SELECT DISTINCT(ScriptName) FROM achievement_criteria_data WHERE ScriptName <> '' AND type = 11 "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM battlefield_template WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM battleground_template WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM creature WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM creature_template WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM gameobject WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM gameobject_template WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM item_template WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM areatrigger_scripts WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM spell_script_names WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM transports WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM game_weather WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM conditions WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(ScriptName) FROM outdoorpvp_template WHERE ScriptName <> '' "
        "UNION "
        "SELECT DISTINCT(script) FROM instance_template WHERE script <> ''");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded empty set of Script Names!");
        return;
    }

    _scriptNamesStore.reserve(result->GetRowCount() + 1);

    do
    {
        _scriptNamesStore.push_back((*result)[0].GetString());
    }
    while (result->NextRow());

    std::sort(_scriptNamesStore.begin(), _scriptNamesStore.end());

    TC_LOG_INFO("server.loading", ">> Loaded {} ScriptNames in {} ms", _scriptNamesStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

ObjectMgr::ScriptNameContainer const& ObjectMgr::GetAllScriptNames() const
{
    return _scriptNamesStore;
}

std::string const& ObjectMgr::GetScriptName(uint32 id) const
{
    static std::string const empty = "";
    return (id < _scriptNamesStore.size()) ? _scriptNamesStore[id] : empty;
}

uint32 ObjectMgr::GetScriptId(std::string const& name)
{
    // use binary search to find the script name in the sorted vector
    // assume "" is the first element
    if (name.empty())
        return 0;

    ScriptNameContainer::const_iterator itr = std::lower_bound(_scriptNamesStore.begin(), _scriptNamesStore.end(), name);
    if (itr == _scriptNamesStore.end() || (*itr != name))
        return 0;

    return uint32(itr - _scriptNamesStore.begin());
}

void ObjectMgr::LoadBroadcastTexts()
{
    uint32 oldMSTime = getMSTime();

    _broadcastTextStore.clear(); // for reload case

    //                                               0   1            2      3      4         5         6         7            8            9            10              11        12
    QueryResult result = WorldDatabase.Query("SELECT ID, LanguageID, `Text`, Text1, EmoteID1, EmoteID2, EmoteID3, EmoteDelay1, EmoteDelay2, EmoteDelay3, SoundEntriesID, EmotesID, Flags FROM broadcast_text");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 broadcast texts. DB table `broadcast_text` is empty.");
        return;
    }

    _broadcastTextStore.rehash(result->GetRowCount());

    do
    {
        Field* fields = result->Fetch();

        BroadcastText bct;

        bct.Id = fields[0].GetUInt32();
        bct.LanguageID = fields[1].GetUInt32();
        bct.Text[DEFAULT_LOCALE] = fields[2].GetString();
        bct.Text1[DEFAULT_LOCALE] = fields[3].GetString();
        bct.EmoteId1 = fields[4].GetUInt32();
        bct.EmoteId2 = fields[5].GetUInt32();
        bct.EmoteId3 = fields[6].GetUInt32();
        bct.EmoteDelay1 = fields[7].GetUInt32();
        bct.EmoteDelay2 = fields[8].GetUInt32();
        bct.EmoteDelay3 = fields[9].GetUInt32();
        bct.SoundEntriesID = fields[10].GetUInt32();
        bct.EmotesID = fields[11].GetUInt32();
        bct.Flags = fields[12].GetUInt32();

        if (bct.SoundEntriesID)
        {
            if (!sSoundEntriesStore.LookupEntry(bct.SoundEntriesID))
            {
                TC_LOG_DEBUG("broadcasttext", "BroadcastText (Id: {}) in table `broadcast_text` has SoundEntriesID {} but sound does not exist.", bct.Id, bct.SoundEntriesID);
                bct.SoundEntriesID = 0;
            }
        }

        if (!GetLanguageDescByID(bct.LanguageID))
        {
            TC_LOG_DEBUG("broadcasttext", "BroadcastText (Id: {}) in table `broadcast_text` using LanguageID {} but Language does not exist.", bct.Id, bct.LanguageID);
            bct.LanguageID = LANG_UNIVERSAL;
        }

        if (bct.EmoteId1)
        {
            if (!sEmotesStore.LookupEntry(bct.EmoteId1))
            {
                TC_LOG_DEBUG("broadcasttext", "BroadcastText (Id: {}) in table `broadcast_text` has EmoteId1 {} but emote does not exist.", bct.Id, bct.EmoteId1);
                bct.EmoteId1 = 0;
            }
        }

        if (bct.EmoteId2)
        {
            if (!sEmotesStore.LookupEntry(bct.EmoteId2))
            {
                TC_LOG_DEBUG("broadcasttext", "BroadcastText (Id: {}) in table `broadcast_text` has EmoteId2 {} but emote does not exist.", bct.Id, bct.EmoteId2);
                bct.EmoteId2 = 0;
            }
        }

        if (bct.EmoteId3)
        {
            if (!sEmotesStore.LookupEntry(bct.EmoteId3))
            {
                TC_LOG_DEBUG("broadcasttext", "BroadcastText (Id: {}) in table `broadcast_text` has EmoteId3 {} but emote does not exist.", bct.Id, bct.EmoteId3);
                bct.EmoteId3 = 0;
            }
        }

        _broadcastTextStore[bct.Id] = bct;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} broadcast texts in {} ms", _broadcastTextStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadBroadcastTextLocales()
{
    uint32 oldMSTime = getMSTime();

    //                                               0   1        2     3
    QueryResult result = WorldDatabase.Query("SELECT ID, locale, `Text`, Text1 FROM broadcast_text_locale");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 broadcast text locales. DB table `broadcast_text_locale` is empty.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        uint32 id               = fields[0].GetUInt32();
        std::string localeName  = fields[1].GetString();

        LocaleConstant locale = GetLocaleByName(localeName);
        if (locale == LOCALE_enUS)
            continue;

        BroadcastTextContainer::iterator bct = _broadcastTextStore.find(id);
        if (bct == _broadcastTextStore.end())
        {
            TC_LOG_ERROR("sql.sql", "BroadcastText (Id: {}) in table `broadcast_text_locale` does not exist. Skipped!", id);
            continue;
        }

        AddLocaleString(fields[2].GetString(), locale, bct->second.Text);
        AddLocaleString(fields[3].GetString(), locale, bct->second.Text1);
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} broadcast text locales in {} ms", uint32(_broadcastTextStore.size()), GetMSTimeDiffToNow(oldMSTime));
}

CreatureBaseStats const* ObjectMgr::GetCreatureBaseStats(uint8 level, uint8 unitClass)
{
    CreatureBaseStatsContainer::const_iterator it = _creatureBaseStatsStore.find(MAKE_PAIR16(level, unitClass));

    if (it != _creatureBaseStatsStore.end())
        return &(it->second);

    struct DefaultCreatureBaseStats : public CreatureBaseStats
    {
        DefaultCreatureBaseStats()
        {
            BaseArmor = 1;
            for (uint8 j = 0; j < MAX_EXPANSIONS; ++j)
            {
                BaseHealth[j] = 1;
                BaseDamage[j] = 0.0f;
            }
            BaseMana = 0;
            AttackPower = 0;
            RangedAttackPower = 0;
        }
    };
    static const DefaultCreatureBaseStats defStats;
    return &defStats;
}

void ObjectMgr::LoadCreatureClassLevelStats()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT level, class, basehp0, basehp1, basehp2, basemana, basearmor, attackpower, rangedattackpower, damage_base, damage_exp1, damage_exp2 FROM creature_classlevelstats");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature base stats. DB table `creature_classlevelstats` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint8 Level = fields[0].GetUInt8();
        uint8 Class = fields[1].GetUInt8();

        if (!Class || ((1 << (Class - 1)) & CLASSMASK_ALL_CREATURES) == 0)
            TC_LOG_ERROR("sql.sql", "Creature base stats for level {} has invalid class {}", Level, Class);

        CreatureBaseStats stats;

        for (uint8 i = 0; i < MAX_EXPANSIONS; ++i)
        {
            stats.BaseHealth[i] = fields[2 + i].GetUInt32();

            if (stats.BaseHealth[i] == 0)
            {
                TC_LOG_ERROR("sql.sql", "Creature base stats for class {}, level {} has invalid zero base HP[{}] - set to 1", Class, Level, i);
                stats.BaseHealth[i] = 1;
            }

            stats.BaseDamage[i] = fields[9 + i].GetFloat();
            if (stats.BaseDamage[i] < 0.0f)
            {
                TC_LOG_ERROR("sql.sql", "Creature base stats for class {}, level {} has invalid negative base damage[{}] - set to 0.0", Class, Level, i);
                stats.BaseDamage[i] = 0.0f;
            }
        }

        stats.BaseMana = fields[5].GetUInt32();
        stats.BaseArmor = fields[6].GetUInt32();

        stats.AttackPower = fields[7].GetUInt16();
        stats.RangedAttackPower = fields[8].GetUInt16();

        _creatureBaseStatsStore[MAKE_PAIR16(Level, Class)] = stats;

        ++count;
    }
    while (result->NextRow());

    for (auto const& creatureTemplatePair : _creatureTemplateStore)
    {
        for (uint16 lvl = creatureTemplatePair.second.minlevel; lvl <= creatureTemplatePair.second.maxlevel; ++lvl)
        {
            if (!_creatureBaseStatsStore.count(MAKE_PAIR16(lvl, creatureTemplatePair.second.unit_class)))
                TC_LOG_ERROR("sql.sql", "Missing base stats for creature class {} level {}", creatureTemplatePair.second.unit_class, lvl);
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} creature base stats in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadFactionChangeAchievements()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT alliance_id, horde_id FROM player_factionchange_achievement");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 faction change achievement pairs. DB table `player_factionchange_achievement` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 alliance = fields[0].GetUInt32();
        uint32 horde = fields[1].GetUInt32();

        if (!sAchievementStore.LookupEntry(alliance))
            TC_LOG_ERROR("sql.sql", "Achievement {} (alliance_id) referenced in `player_factionchange_achievement` does not exist, pair skipped!", alliance);
        else if (!sAchievementStore.LookupEntry(horde))
            TC_LOG_ERROR("sql.sql", "Achievement {} (horde_id) referenced in `player_factionchange_achievement` does not exist, pair skipped!", horde);
        else
            FactionChangeAchievements[alliance] = horde;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} faction change achievement pairs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadFactionChangeItems()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT alliance_id, horde_id FROM player_factionchange_items");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 faction change item pairs. DB table `player_factionchange_items` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 alliance = fields[0].GetUInt32();
        uint32 horde = fields[1].GetUInt32();

        if (!GetItemTemplate(alliance))
            TC_LOG_ERROR("sql.sql", "Item {} (alliance_id) referenced in `player_factionchange_items` does not exist, pair skipped!", alliance);
        else if (!GetItemTemplate(horde))
            TC_LOG_ERROR("sql.sql", "Item {} (horde_id) referenced in `player_factionchange_items` does not exist, pair skipped!", horde);
        else
            FactionChangeItems[alliance] = horde;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} faction change item pairs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadFactionChangeQuests()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT alliance_id, horde_id FROM player_factionchange_quests");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 faction change quest pairs. DB table `player_factionchange_quests` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 alliance = fields[0].GetUInt32();
        uint32 horde = fields[1].GetUInt32();

        if (!sObjectMgr->GetQuestTemplate(alliance))
            TC_LOG_ERROR("sql.sql", "Quest {} (alliance_id) referenced in `player_factionchange_quests` does not exist, pair skipped!", alliance);
        else if (!sObjectMgr->GetQuestTemplate(horde))
            TC_LOG_ERROR("sql.sql", "Quest {} (horde_id) referenced in `player_factionchange_quests` does not exist, pair skipped!", horde);
        else
            FactionChangeQuests[alliance] = horde;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} faction change quest pairs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadFactionChangeReputations()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT alliance_id, horde_id FROM player_factionchange_reputations");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 faction change reputation pairs. DB table `player_factionchange_reputations` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 alliance = fields[0].GetUInt32();
        uint32 horde = fields[1].GetUInt32();

        if (!sFactionStore.LookupEntry(alliance))
            TC_LOG_ERROR("sql.sql", "Reputation {} (alliance_id) referenced in `player_factionchange_reputations` does not exist, pair skipped!", alliance);
        else if (!sFactionStore.LookupEntry(horde))
            TC_LOG_ERROR("sql.sql", "Reputation {} (horde_id) referenced in `player_factionchange_reputations` does not exist, pair skipped!", horde);
        else
            FactionChangeReputation[alliance] = horde;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} faction change reputation pairs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadFactionChangeSpells()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT alliance_id, horde_id FROM player_factionchange_spells");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 faction change spell pairs. DB table `player_factionchange_spells` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 alliance = fields[0].GetUInt32();
        uint32 horde = fields[1].GetUInt32();

        if (!sSpellMgr->GetSpellInfo(alliance))
            TC_LOG_ERROR("sql.sql", "Spell {} (alliance_id) referenced in `player_factionchange_spells` does not exist, pair skipped!", alliance);
        else if (!sSpellMgr->GetSpellInfo(horde))
            TC_LOG_ERROR("sql.sql", "Spell {} (horde_id) referenced in `player_factionchange_spells` does not exist, pair skipped!", horde);
        else
            FactionChangeSpells[alliance] = horde;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} faction change spell pairs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadFactionChangeTitles()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = WorldDatabase.Query("SELECT alliance_id, horde_id FROM player_factionchange_titles");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 faction change title pairs. DB table `player_factionchange_title` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 alliance = fields[0].GetUInt32();
        uint32 horde = fields[1].GetUInt32();

        if (!sCharTitlesStore.LookupEntry(alliance))
            TC_LOG_ERROR("sql.sql", "Title {} (alliance_id) referenced in `player_factionchange_title` does not exist, pair skipped!", alliance);
        else if (!sCharTitlesStore.LookupEntry(horde))
            TC_LOG_ERROR("sql.sql", "Title {} (horde_id) referenced in `player_factionchange_title` does not exist, pair skipped!", horde);
        else
            FactionChangeTitles[alliance] = horde;

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} faction change title pairs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

GameObjectTemplate const* ObjectMgr::GetGameObjectTemplate(uint32 entry) const
{
    return Trinity::Containers::MapGetValuePtr(_gameObjectTemplateStore, entry);
}

GameObjectTemplateAddon const* ObjectMgr::GetGameObjectTemplateAddon(uint32 entry) const
{
    auto itr = _gameObjectTemplateAddonStore.find(entry);
    if (itr != _gameObjectTemplateAddonStore.end())
        return &itr->second;

    return nullptr;
}

GameObjectOverride const* ObjectMgr::GetGameObjectOverride(ObjectGuid::LowType spawnId) const
{
    return Trinity::Containers::MapGetValuePtr(_gameObjectOverrideStore, spawnId);
}

CreatureTemplate const* ObjectMgr::GetCreatureTemplate(uint32 entry) const
{
    return Trinity::Containers::MapGetValuePtr(_creatureTemplateStore, entry);
}

QuestPOIWrapper const* ObjectMgr::GetQuestPOIWrapper(uint32 questId) const
{
    return Trinity::Containers::MapGetValuePtr(_questPOIStore, questId);
}

VehicleTemplate const* ObjectMgr::GetVehicleTemplate(Vehicle* veh) const
{
    return Trinity::Containers::MapGetValuePtr(_vehicleTemplateStore, veh->GetCreatureEntry());
}

VehicleAccessoryList const* ObjectMgr::GetVehicleAccessoryList(Vehicle* veh) const
{
    if (Creature* cre = veh->GetBase()->ToCreature())
    {
        // Give preference to GUID-based accessories
        VehicleAccessoryContainer::const_iterator itr = _vehicleAccessoryStore.find(cre->GetSpawnId());
        if (itr != _vehicleAccessoryStore.end())
            return &itr->second;
    }

    // Otherwise return entry-based
    VehicleAccessoryContainer::const_iterator itr = _vehicleTemplateAccessoryStore.find(veh->GetCreatureEntry());
    if (itr != _vehicleTemplateAccessoryStore.end())
        return &itr->second;
    return nullptr;
}

DungeonEncounterList const* ObjectMgr::GetDungeonEncounterList(uint32 mapId, Difficulty difficulty) const
{
    auto itr = _dungeonEncounterStore.find(MAKE_PAIR32(mapId, difficulty));
    if (itr != _dungeonEncounterStore.end())
        return &itr->second;
    return nullptr;
}

PlayerInfo const* ObjectMgr::GetPlayerInfo(uint32 race, uint32 class_) const
{
    if (race >= MAX_RACES)
        return nullptr;
    if (class_ >= MAX_CLASSES)
        return nullptr;
    auto const& info = _playerInfo[race][class_];
    if (!info)
        return nullptr;
    return info.get();
}

void ObjectMgr::LoadGameObjectQuestItems()
{
    uint32 oldMSTime = getMSTime();

    //                                               0                1       2
    QueryResult result = WorldDatabase.Query("SELECT GameObjectEntry, ItemId, Idx FROM gameobject_questitem ORDER BY Idx ASC");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 gameobject quest items. DB table `gameobject_questitem` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        uint32 item  = fields[1].GetUInt32();
        uint32 idx   = fields[2].GetUInt32();

        GameObjectTemplate const* goInfo = GetGameObjectTemplate(entry);
        if (!goInfo)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject_questitem` has data for nonexistent gameobject (entry: {}, idx: {}), skipped", entry, idx);
            continue;
        };

        ItemEntry const* db2Data = sItemStore.LookupEntry(item);
        if (!db2Data)
        {
            TC_LOG_ERROR("sql.sql", "Table `gameobject_questitem` has nonexistent item (ID: {}) in gameobject (entry: {}, idx: {}), skipped", item, entry, idx);
            continue;
        };

        _gameObjectQuestItemStore[entry].push_back(item);

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} gameobject quest items in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::LoadCreatureQuestItems()
{
    uint32 oldMSTime = getMSTime();

    //                                               0              1       2
    QueryResult result = WorldDatabase.Query("SELECT CreatureEntry, ItemId, Idx FROM creature_questitem ORDER BY Idx ASC");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 creature quest items. DB table `creature_questitem` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        uint32 item  = fields[1].GetUInt32();
        uint32 idx   = fields[2].GetUInt32();

        CreatureTemplate const* creatureInfo = GetCreatureTemplate(entry);
        if (!creatureInfo)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_questitem` has data for nonexistent creature (entry: {}, idx: {}), skipped", entry, idx);
            continue;
        };

        ItemEntry const* db2Data = sItemStore.LookupEntry(item);
        if (!db2Data)
        {
            TC_LOG_ERROR("sql.sql", "Table `creature_questitem` has nonexistent item (ID: {}) in creature (entry: {}, idx: {}), skipped", item, entry, idx);
            continue;
        };

        _creatureQuestItemStore[entry].push_back(item);

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} creature quest items in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void ObjectMgr::InitializeQueriesData(QueryDataGroup mask)
{
    uint32 oldMSTime = getMSTime();

    // cache disabled
    if (!sWorld->getBoolConfig(CONFIG_CACHE_DATA_QUERIES))
    {
        TC_LOG_INFO("server.loading", ">> Query data caching is disabled. Skipped initialization.");
        return;
    }

    Trinity::ThreadPool pool;

    // Initialize Query data for creatures
    if (mask & QUERY_DATA_CREATURES)
        for (auto& creatureTemplatePair : _creatureTemplateStore)
            pool.PostWork([creature = &creatureTemplatePair.second]() { creature->InitializeQueryData(); });

    // Initialize Query Data for gameobjects
    if (mask & QUERY_DATA_GAMEOBJECTS)
        for (auto& gameObjectTemplatePair : _gameObjectTemplateStore)
            pool.PostWork([gobj = &gameObjectTemplatePair.second]() { gobj->InitializeQueryData(); });

    // Initialize Query Data for items
    if (mask & QUERY_DATA_ITEMS)
        for (auto& itemTemplatePair : _itemTemplateStore)
            pool.PostWork([item = &itemTemplatePair.second]() { item->InitializeQueryData(); });

    // Initialize Query Data for quests
    if (mask & QUERY_DATA_QUESTS)
        for (auto& questTemplatePair : _questTemplates)
            pool.PostWork([quest = questTemplatePair.second.get()]() { quest->InitializeQueryData(); });

    // Initialize Quest POI data
    if (mask & QUERY_DATA_POIS)
        for (auto& poiWrapperPair : _questPOIStore)
            pool.PostWork([poi = &poiWrapperPair.second]() { poi->InitializeQueryData(); });

    pool.Join();

    TC_LOG_INFO("server.loading", ">> Initialized query cache data in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

void QuestPOIWrapper::InitializeQueryData()
{
    QueryDataBuffer = BuildQueryData();
}

ByteBuffer QuestPOIWrapper::BuildQueryData() const
{
    ByteBuffer tempBuffer;
    tempBuffer << uint32(POIData.QuestID);                                      // quest ID
    tempBuffer << uint32(POIData.QuestPOIBlobDataStats.size());                 // POI count

    for (QuestPOIBlobData const& questPOIBlobData : POIData.QuestPOIBlobDataStats)
    {
        tempBuffer << uint32(questPOIBlobData.BlobIndex);                       // POI index
        tempBuffer << int32(questPOIBlobData.ObjectiveIndex);                   // objective index
        tempBuffer << uint32(questPOIBlobData.MapID);                           // mapid
        tempBuffer << uint32(questPOIBlobData.WorldMapAreaID);                  // areaid
        tempBuffer << uint32(questPOIBlobData.Floor);                           // floorid
        tempBuffer << uint32(questPOIBlobData.Unk3);                            // unknown
        tempBuffer << uint32(questPOIBlobData.Unk4);                            // unknown
        tempBuffer << uint32(questPOIBlobData.QuestPOIBlobPointStats.size());   // POI points count

        for (QuestPOIBlobPoint const& questPOIBlobPoint : questPOIBlobData.QuestPOIBlobPointStats)
        {
            tempBuffer << int32(questPOIBlobPoint.X); // POI point x
            tempBuffer << int32(questPOIBlobPoint.Y); // POI point y
        }
    }

    return tempBuffer;
}
