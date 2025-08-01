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
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "steam_vault.h"

ObjectData const gameObjectData[] =
{
    { GO_ACCESS_PANEL_HYDRO, DATA_ACCESS_PANEL_HYDRO },
    { GO_ACCESS_PANEL_MEK,   DATA_ACCESS_PANEL_MEK   },
    { GO_MAIN_CHAMBERS_DOOR, DATA_MAIN_DOOR          },
    { 0,                     0                       } // END
};

ObjectData const creatureData[] =
{
    { NPC_HYDROMANCER_THESPIA,      DATA_HYDROMANCER_THESPIA   },
    { NPC_MEKGINEER_STEAMRIGGER,    DATA_MEKGINEER_STEAMRIGGER },
    { NPC_WARLORD_KALITHRESH,       DATA_WARLORD_KALITHRESH    },
    { NPC_COILFANG_DOOR_CONTROLLER, DATA_DOOR_CONTROLLER       },
    { 0,                            0                          } // END
};

class instance_steam_vault : public InstanceMapScript
{
    public:
        instance_steam_vault() : InstanceMapScript(SteamVaultScriptName, 545) { }

        struct instance_steam_vault_InstanceMapScript : public InstanceScript
        {
            instance_steam_vault_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadObjectData(creatureData, gameObjectData);
            }

            void OnGameObjectCreate(GameObject* go) override
            {
                InstanceScript::OnGameObjectCreate(go);
                if (go->GetEntry() == GO_MAIN_CHAMBERS_DOOR)
                    CheckMainDoor();
            }

            void CheckMainDoor()
            {
                if (GetBossState(DATA_HYDROMANCER_THESPIA) == DONE && GetBossState(DATA_MEKGINEER_STEAMRIGGER) == DONE)
                {
                    if (Creature* controller = GetCreature(DATA_DOOR_CONTROLLER))
                        controller->AI()->Talk(CONTROLLER_TEXT_MAIN_DOOR_OPEN);

                    if (GameObject* mainDoor = GetGameObject(DATA_MAIN_DOOR))
                    {
                        HandleGameObject(ObjectGuid::Empty, true, mainDoor);
                       mainDoor->SetFlag(GO_FLAG_NOT_SELECTABLE);
                    }
                }
            }

            void SetData(uint32 type, uint32 /*data*/) override
            {
                if (type == ACTION_OPEN_DOOR)
                    CheckMainDoor();
            }

            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_HYDROMANCER_THESPIA:
                        if (state == DONE)
                            if (GameObject* panel = GetGameObject(DATA_ACCESS_PANEL_HYDRO))
                                panel->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    case DATA_MEKGINEER_STEAMRIGGER:
                        if (state == DONE)
                            if (GameObject* panel = GetGameObject(DATA_ACCESS_PANEL_MEK))
                                panel->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        break;
                    default:
                        break;
                }

                return true;
            }
        };

        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_steam_vault_InstanceMapScript(map);
        }
};

void AddSC_instance_steam_vault()
{
    new instance_steam_vault();
}
