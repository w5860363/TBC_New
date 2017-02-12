
#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Item.h"
#include "GameObject.h"
#include "Opcodes.h"
#include "Chat.h"
#include "MapManager.h"
#include "Language.h"
#include "World.h"
#include "GameEventMgr.h"
#include "SpellMgr.h"
#include "AccountMgr.h"
#include "WaypointManager.h"
#include "Util.h"
#include <cctype>
#include <iostream>
#include <fstream>
#include <map>
#include "GlobalEvents.h"
#include "ChannelMgr.h"
#include "Transport.h"
#include "CharacterDatabase.h"
#include "LoginDatabase.h"
#include "ArenaTeam.h"
#include "LogsDatabaseAccessor.h"
#include "CharacterCache.h"

#include "TargetedMovementGenerator.h"                      // for HandleNpcUnFollowCommand
#include "Management/MMapManager.h"                         // for mmap manager
#include "Management/MMapFactory.h"                         // for mmap factory
#include "PathGenerator.h"                                     // for mmap commands

#include "WaypointMovementGenerator.h"

static uint32 ReputationRankStrIndex[MAX_REPUTATION_RANK] =
{
    LANG_REP_HATED,    LANG_REP_HOSTILE, LANG_REP_UNFRIENDLY, LANG_REP_NEUTRAL,
    LANG_REP_FRIENDLY, LANG_REP_HONORED, LANG_REP_REVERED,    LANG_REP_EXALTED
};

//mute player for some times
bool ChatHandler::HandleMuteCommand(const char* args)
{
    ARGS_CHECK

    char* charname = strtok((char*)args, " ");
    if (!charname)
        return false;

    std::string cname = charname;

    char* timetonotspeak = strtok(nullptr, " ");
    if(!timetonotspeak)
        return false;

    char* mutereason = strtok(nullptr, "");
    std::string mutereasonstr;
    if (!mutereason)
        return false;
    
    mutereasonstr = mutereason;
        
    uint32 notspeaktime = (uint32) atoi(timetonotspeak);

    if(!normalizePlayerName(cname))
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    uint64 guid = sCharacterCache->GetCharacterGuidByName(cname.c_str());
    if(!guid)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = sObjectMgr->GetPlayer(guid);

    // check security
    uint32 account_id = 0;
    uint32 security = 0;

    if (chr)
    {
        account_id = chr->GetSession()->GetAccountId();
        security = chr->GetSession()->GetSecurity();
    }
    else
    {
        account_id = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        security = sAccountMgr->GetSecurity(account_id);
    }

    if(m_session && security >= m_session->GetSecurity())
    {
        SendSysMessage(LANG_YOURS_SECURITY_IS_LOW);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 duration = notspeaktime*MINUTE;
    time_t mutetime = time(nullptr) + duration;

    if (chr)
        chr->GetSession()->m_muteTime = mutetime;
        
    // Prevent SQL injection
    LogsDatabase.EscapeString(mutereasonstr);
    LoginDatabase.PExecute("UPDATE account SET mutetime = " UI64FMTD " WHERE id = '%u'", uint64(mutetime), account_id );

    LogsDatabaseAccessor::Sanction(m_session, account_id, 0, SANCTION_MUTE_ACCOUNT, duration, mutereasonstr);

    if(chr)
        ChatHandler(chr).PSendSysMessage(LANG_YOUR_CHAT_DISABLED, notspeaktime, mutereasonstr.c_str());

    PSendSysMessage(LANG_YOU_DISABLE_CHAT, cname.c_str(), notspeaktime, mutereasonstr.c_str());

    return true;
}

//unmute player
bool ChatHandler::HandleUnmuteCommand(const char* args)
{
    ARGS_CHECK

    char *charname = strtok((char*)args, " ");
    if (!charname)
        return false;

    std::string cname = charname;

    if(!normalizePlayerName(cname))
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    uint64 guid = sCharacterCache->GetCharacterGuidByName(cname.c_str());
    if(!guid)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = sObjectMgr->GetPlayer(guid);

    // check security
    uint32 account_id = 0;
    uint32 security = 0;

    if (chr)
    {
        account_id = chr->GetSession()->GetAccountId();
        security = chr->GetSession()->GetSecurity();
    }
    else
    {
        account_id = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        security = sAccountMgr->GetSecurity(account_id);
    }

    if(m_session && security >= m_session->GetSecurity())
    {
        SendSysMessage(LANG_YOURS_SECURITY_IS_LOW);
        SetSentErrorMessage(true);
        return false;
    }

    if (chr)
    {
        if(chr->CanSpeak())
        {
            SendSysMessage(LANG_CHAT_ALREADY_ENABLED);
            SetSentErrorMessage(true);
            return false;
        }

        chr->GetSession()->m_muteTime = 0;
    }

    LoginDatabase.PExecute("UPDATE account SET mutetime = '0' WHERE id = '%u'", account_id );
    LogsDatabaseAccessor::RemoveSanction(m_session, account_id, 0, "", SANCTION_MUTE_ACCOUNT);

    if(chr)
        ChatHandler(chr).PSendSysMessage(LANG_YOUR_CHAT_ENABLED);

    PSendSysMessage(LANG_YOU_ENABLE_CHAT, cname.c_str());
    return true;
}

bool ChatHandler::HandleTargetObjectCommand(const char* args)
{
    

    Player* pl = m_session->GetPlayer();
    QueryResult result;
    GameEventMgr::ActiveEvents const& activeEventsList = sGameEventMgr->GetActiveEventList();
    if(*args)
    {
        int32 id = atoi((char*)args);
        if(id)
            result = WorldDatabase.PQuery("SELECT guid, id, position_x, position_y, position_z, orientation, map, (POW(position_x - '%f', 2) + POW(position_y - '%f', 2) + POW(position_z - '%f', 2)) AS order_ FROM gameobject WHERE map = '%i' AND id = '%u' ORDER BY order_ ASC LIMIT 1",
                pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ(), pl->GetMapId(),id);
        else
        {
            std::string name = args;
            WorldDatabase.EscapeString(name);
            result = WorldDatabase.PQuery(
                "SELECT guid, id, position_x, position_y, position_z, orientation, map, (POW(position_x - %f, 2) + POW(position_y - %f, 2) + POW(position_z - %f, 2)) AS order_ "
                "FROM gameobject,gameobject_template WHERE gameobject_template.entry = gameobject.id AND map = %i AND name " _LIKE_ " " _CONCAT3_ ("'%%'","'%s'","'%%'")" ORDER BY order_ ASC LIMIT 1",
                pl->GetPositionX(), pl->GetPositionY(), pl->GetPositionZ(), pl->GetMapId(),name.c_str());
        }
    }
    else
    {
        std::ostringstream eventFilter;
        eventFilter << " AND (event IS NULL ";
        bool initString = true;

        for (unsigned short itr : activeEventsList)
        {
            if (initString)
            {
                eventFilter  <<  "OR event IN (" <<itr;
                initString =false;
            }
            else
                eventFilter << "," << itr;
        }

        if (!initString)
            eventFilter << "))";
        else
            eventFilter << ")";

        result = WorldDatabase.PQuery("SELECT gameobject.guid, id, position_x, position_y, position_z, orientation, map, "
            "(POW(position_x - %f, 2) + POW(position_y - %f, 2) + POW(position_z - %f, 2)) AS order_ FROM gameobject "
            "LEFT OUTER JOIN game_event_gameobject on gameobject.guid=game_event_gameobject.guid WHERE map = '%i' %s ORDER BY order_ ASC LIMIT 1",
            m_session->GetPlayer()->GetPositionX(), m_session->GetPlayer()->GetPositionY(), m_session->GetPlayer()->GetPositionZ(), m_session->GetPlayer()->GetMapId(),eventFilter.str().c_str());
    }

    if (!result)
    {
        SendSysMessage(LANG_COMMAND_TARGETOBJNOTFOUND);
        return true;
    }

    Field *fields = result->Fetch();
    uint32 lowguid = fields[0].GetUInt32();
    uint32 id = fields[1].GetUInt32();
    float x = fields[2].GetFloat();
    float y = fields[3].GetFloat();
    float z = fields[4].GetFloat();
    float o = fields[5].GetFloat();
    int mapid = fields[6].GetUInt16();

    GameObjectTemplate const* goI = sObjectMgr->GetGameObjectTemplate(id);

    if (!goI)
    {
        PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST,id);
        return false;
    }

    GameObject* target = ObjectAccessor::GetGameObject(*m_session->GetPlayer(),MAKE_NEW_GUID(lowguid,id,HIGHGUID_GAMEOBJECT));

    PSendSysMessage(LANG_GAMEOBJECT_DETAIL, lowguid, goI->name.c_str(), lowguid, id, x, y, z, mapid, o);

    if(target)
    {
        int32 curRespawnDelay = target->GetRespawnTimeEx()-time(nullptr);
        if(curRespawnDelay < 0)
            curRespawnDelay = 0;

        std::string curRespawnDelayStr = secsToTimeString(curRespawnDelay,true);
        std::string defRespawnDelayStr = secsToTimeString(target->GetRespawnDelay(),true);

        PSendSysMessage(LANG_COMMAND_RAWPAWNTIMES, defRespawnDelayStr.c_str(),curRespawnDelayStr.c_str());
    }
    return true;
}

bool ChatHandler::HandleGoObjectCommand(const char* args)
{
    ARGS_CHECK

    Player* _player = m_session->GetPlayer();
    
    char* cId = strtok((char*)args, " ");
    if (!cId)
        return false;
        
    float x, y, z, ort;
    uint32 mapid;

    if (strcmp(cId, "id") == 0) {
        char* cEntry = strtok(nullptr, "");
        if (!cEntry)
            return false;

        uint32 entry = atoi(cEntry);
        if (!entry)
            return false;
            
        QueryResult result = WorldDatabase.PQuery("SELECT position_x, position_y, position_z, orientation, map FROM gameobject WHERE id = %u LIMIT 1", entry);
        if (!result) {
            SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
            return true;
        }
        
        Field* fields = result->Fetch();
        x = fields[0].GetFloat();
        y = fields[1].GetFloat();
        z = fields[2].GetFloat();
        ort = fields[3].GetFloat();
        mapid = fields[4].GetUInt32();
    }
    else {
        uint32 guid = atoi(cId);
        if (!guid)
            return false;

        if (GameObjectData const* go_data = sObjectMgr->GetGOData(guid))
        {
            x = go_data->posX;
            y = go_data->posY;
            z = go_data->posZ;
            ort = go_data->orientation;
            mapid = go_data->mapid;
        }
        else
        {
            SendSysMessage(LANG_COMMAND_GOOBJNOTFOUND);
            SetSentErrorMessage(true);
            return false;
        }
    }

    if(!MapManager::IsValidMapCoord(mapid,x,y,z,ort))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,x,y,mapid);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->IsInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->CleanupAfterTaxiFlight();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(mapid, x, y, z, ort);
    return true;
}

bool ChatHandler::HandleGoTicketCommand(const char * args)
{
    ARGS_CHECK

    char *cstrticket_id = strtok((char*)args, " ");

    if(!cstrticket_id)
        return false;

    uint64 ticket_id = atoi(cstrticket_id);
    if(!ticket_id)
        return false;

    GM_Ticket *ticket = sObjectMgr->GetGMTicket(ticket_id);
    if(!ticket)
    {
        SendSysMessage(LANG_COMMAND_TICKETNOTEXIST);
        return true;
    }

    float x, y, z;
    int mapid;

    x = ticket->pos_x;
    y = ticket->pos_y;
    z = ticket->pos_z;
    mapid = ticket->map;

    Player* _player = m_session->GetPlayer();
    if(_player->IsInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->CleanupAfterTaxiFlight();
    }
     else
        _player->SaveRecallPosition();

    _player->TeleportTo(mapid, x, y, z, 1, 0);
    return true;
}

bool ChatHandler::HandleGoTriggerCommand(const char* args)
{
    ARGS_CHECK

    Player* _player = m_session->GetPlayer();

    char *atId = strtok((char*)args, " ");
    if (!atId)
        return false;

    int32 i_atId = atoi(atId);

    if(!i_atId)
        return false;

    AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(i_atId);
    if (!at)
    {
        PSendSysMessage(LANG_COMMAND_GOAREATRNOTFOUND,i_atId);
        SetSentErrorMessage(true);
        return false;
    }

    if(!MapManager::IsValidMapCoord(at->mapid,at->x,at->y,at->z))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,at->x,at->y,at->mapid);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->IsInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->CleanupAfterTaxiFlight();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(at->mapid, at->x, at->y, at->z, _player->GetOrientation());
    return true;
}

bool ChatHandler::HandleGoGraveyardCommand(const char* args)
{
    

    Player* _player = m_session->GetPlayer();

    if (!*args)
        return false;

    char *gyId = strtok((char*)args, " ");
    if (!gyId)
        return false;

    int32 i_gyId = atoi(gyId);

    if(!i_gyId)
        return false;

    WorldSafeLocsEntry const* gy = sWorldSafeLocsStore.LookupEntry(i_gyId);
    if (!gy)
    {
        PSendSysMessage(LANG_COMMAND_GRAVEYARDNOEXIST,i_gyId);
        SetSentErrorMessage(true);
        return false;
    }

    if(!MapManager::IsValidMapCoord(gy->map_id,gy->x,gy->y,gy->z))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,gy->x,gy->y,gy->map_id);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->IsInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->CleanupAfterTaxiFlight();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(gy->map_id, gy->x, gy->y, gy->z, _player->GetOrientation());
    return true;
}

/** \brief Teleport the GM to the specified creature
 *
 * .gocreature <GUID>      --> TP using creature.guid
 * .gocreature azuregos    --> TP player to the mob with this name
 *                             Warning: If there is more than one mob with this name
 *                                      you will be teleported to the first one that is found.
 * .gocreature id 6109     --> TP player to the mob, that has this creature_template.entry
 *                             Warning: If there is more than one mob with this "id"
 *                                      you will be teleported to the first one that is found.
 */
//teleport to creature
bool ChatHandler::HandleGoCreatureCommand(const char* args)
{
    ARGS_CHECK

    Player* _player = m_session->GetPlayer();

    // "id" or number or [name] Shift-click form |color|Hcreature_entry:creature_id|h[name]|h|r
    char* pParam1 = extractKeyFromLink((char*)args,"Hcreature");
    if (!pParam1)
        return false;

    std::ostringstream whereClause;

    // User wants to teleport to the NPC's template entry
    if( strcmp(pParam1, "id") == 0 )
    {
        //TC_LOG_ERROR("command","DEBUG: ID found");

        // Get the "creature_template.entry"
        // number or [name] Shift-click form |color|Hcreature_entry:creature_id|h[name]|h|r
        char* tail = strtok(nullptr,"");
        if(!tail)
            return false;
        char* cId = extractKeyFromLink(tail,"Hcreature_entry");
        if(!cId)
            return false;

        int32 tEntry = atoi(cId);
        //TC_LOG_ERROR("DEBUG: ID value: %d", tEntry);
        if(!tEntry)
            return false;

        whereClause << "WHERE id = '" << tEntry << "'";
    }
    else
    {
        //TC_LOG_ERROR("command","DEBUG: ID *not found*");

        int32 guid = atoi(pParam1);

        // Number is invalid - maybe the user specified the mob's name
        if(!guid)
        {
            std::string name = pParam1;
            WorldDatabase.EscapeString(name);
            whereClause << ", creature_template WHERE creature.id = creature_template.entry AND creature_template.name " _LIKE_ " '" << name << "'";
        }
        else
        {
            whereClause <<  "WHERE guid = '" << guid << "'";
            QueryResult result = WorldDatabase.PQuery("SELECT id FROM creature WHERE guid = %u LIMIT 1", guid);
            if (result) {
                Field *fields = result->Fetch();
                uint32 creatureentry = fields[0].GetUInt32();

                uint64 packedguid = MAKE_NEW_GUID(guid, creatureentry, HIGHGUID_UNIT);
                if (Unit *cre = ObjectAccessor::GetUnit((*_player), packedguid)) {
                    PSendSysMessage("Creature found, you are now teleported on its current location!");

                    // stop flight if need
                    if(_player->IsInFlight())
                    {
                        _player->GetMotionMaster()->MovementExpired();
                        _player->CleanupAfterTaxiFlight();
                    }
                    // save only in non-flight case
                    else
                        _player->SaveRecallPosition();

                    _player->TeleportTo(cre->GetMapId(), cre->GetPositionX(), cre->GetPositionY(), cre->GetPositionZ(), cre->GetOrientation());
                    if(Transport* transport = cre->GetTransport())
                        transport->AddPassenger(_player);
                    return true;
                }
            }
        }
    }
    //TC_LOG_ERROR("DEBUG: %s", whereClause.c_str());

    QueryResult result = WorldDatabase.PQuery("SELECT position_x,position_y,position_z,orientation,map FROM creature %s", whereClause.str().c_str() );
    if (!result)
    {
        SendSysMessage(LANG_COMMAND_GOCREATNOTFOUND);
        SetSentErrorMessage(true);
        return false;
    }
    if( result->GetRowCount() > 1 )
    {
        SendSysMessage(LANG_COMMAND_GOCREATMULTIPLE);
    }

    Field *fields = result->Fetch();
    float x = fields[0].GetFloat();
    float y = fields[1].GetFloat();
    float z = fields[2].GetFloat();
    float ort = fields[3].GetFloat();
    int mapid = fields[4].GetUInt16();

    if(!MapManager::IsValidMapCoord(mapid,x,y,z,ort))
    {
        PSendSysMessage(LANG_INVALID_TARGET_COORD,x,y,mapid);
        SetSentErrorMessage(true);
        return false;
    }

    // stop flight if need
    if(_player->IsInFlight())
    {
        _player->GetMotionMaster()->MovementExpired();
        _player->CleanupAfterTaxiFlight();
    }
    // save only in non-flight case
    else
        _player->SaveRecallPosition();

    _player->TeleportTo(mapid, x, y, z, ort);
    return true;
}

bool ChatHandler::HandleGUIDCommand(const char* /*args*/)
{
    

    uint64 guid = m_session->GetPlayer()->GetTarget();

    if (guid == 0)
    {
        SendSysMessage(LANG_NO_SELECTION);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_OBJECT_GUID, GUID_LOPART(guid), GUID_HIPART(guid));
    return true;
}

bool ChatHandler::HandleLookupFactionCommand(const char* args)
{
    ARGS_CHECK

    // Can be NULL at console call
    Player *target = GetSelectedPlayer();

    std::string namepart = args;
    std::wstring wnamepart;

    if (!Utf8toWStr (namepart,wnamepart))
        return false;

    // converting string that we try to find to lower case
    wstrToLower (wnamepart);

    uint32 counter = 0;                                     // Counter for figure out that we found smth.

    for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)
    {
        FactionEntry const *factionEntry = sFactionStore.LookupEntry (id);
        if (factionEntry)
        {
            FactionState const* repState = nullptr;
            if(target)
            {
                FactionStateList::const_iterator repItr = target->m_factions.find (factionEntry->reputationListID);
                if(repItr != target->m_factions.end())
                    repState = &repItr->second;
            }

            int loc = GetSessionDbcLocale();
            std::string name = factionEntry->name[loc];
            if(name.empty())
                continue;

            if (!Utf8FitTo(name, wnamepart))
            {
                loc = 0;
                for(; loc < TOTAL_LOCALES; ++loc)
                {
                    if(GetSessionDbcLocale())
                        continue;

                    name = factionEntry->name[loc];
                    if(name.empty())
                        continue;

                    if (Utf8FitTo(name, wnamepart))
                        break;
                }
            }

            if(loc < TOTAL_LOCALES)
            {
                // send faction in "id - [faction] rank reputation [visible] [at war] [own team] [unknown] [invisible] [inactive]" format
                // or              "id - [faction] [no reputation]" format
                std::ostringstream ss;
                if (m_session)
                    ss << id << " - |cffffffff|Hfaction:" << id << "|h[" << name << " " << localeNames[loc] << "]|h|r";
                else
                    ss << id << " - " << name << " " << localeNames[loc];

                if (repState)                               // and then target!=NULL also
                {
                    ReputationRank rank = target->GetReputationRank(factionEntry);
                    std::string rankName = GetTrinityString(ReputationRankStrIndex[rank]);

                    ss << " " << rankName << "|h|r (" << target->GetReputation(factionEntry) << ")";

                    if(repState->Flags & FACTION_FLAG_VISIBLE)
                        ss << GetTrinityString(LANG_FACTION_VISIBLE);
                    if(repState->Flags & FACTION_FLAG_AT_WAR)
                        ss << GetTrinityString(LANG_FACTION_ATWAR);
                    if(repState->Flags & FACTION_FLAG_PEACE_FORCED)
                        ss << GetTrinityString(LANG_FACTION_PEACE_FORCED);
                    if(repState->Flags & FACTION_FLAG_HIDDEN)
                        ss << GetTrinityString(LANG_FACTION_HIDDEN);
                    if(repState->Flags & FACTION_FLAG_INVISIBLE_FORCED)
                        ss << GetTrinityString(LANG_FACTION_INVISIBLE_FORCED);
                    if(repState->Flags & FACTION_FLAG_INACTIVE)
                        ss << GetTrinityString(LANG_FACTION_INACTIVE);
                }
                else
                    ss << GetTrinityString(LANG_FACTION_NOREPUTATION);

                SendSysMessage(ss.str().c_str());
                counter++;
            }
        }
    }

    if (counter == 0)                                       // if counter == 0 then we found nth
        SendSysMessage(LANG_COMMAND_FACTION_NOTFOUND);
    return true;
}

bool ChatHandler::HandleModifyRepCommand(const char * args)
{
    ARGS_CHECK

    Player* target = GetSelectedPlayerOrSelf();
    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    char* factionTxt = extractKeyFromLink((char*)args,"Hfaction");
    if(!factionTxt)
        return false;

    uint32 factionId = atoi(factionTxt);

    int32 amount = 0;
    char *rankTxt = strtok(nullptr, " ");
    if (!factionTxt || !rankTxt)
        return false;

    amount = atoi(rankTxt);
    if ((amount == 0) && (rankTxt[0] != '-') && !isdigit(rankTxt[0]))
    {
        std::string rankStr = rankTxt;
        std::wstring wrankStr;
        if(!Utf8toWStr(rankStr,wrankStr))
            return false;
        wstrToLower( wrankStr );

        int r = 0;
        amount = -42000;
        for (; r < MAX_REPUTATION_RANK; ++r)
        {
            std::string rank = GetTrinityString(ReputationRankStrIndex[r]);
            if(rank.empty())
                continue;

            std::wstring wrank;
            if(!Utf8toWStr(rank,wrank))
                continue;

            wstrToLower(wrank);

            if(wrank.substr(0,wrankStr.size())==wrankStr)
            {
                char *deltaTxt = strtok(nullptr, " ");
                if (deltaTxt)
                {
                    int32 delta = atoi(deltaTxt);
                    if ((delta < 0) || (delta > Player::ReputationRank_Length[r] -1))
                    {
                        PSendSysMessage(LANG_COMMAND_FACTION_DELTA, (Player::ReputationRank_Length[r]-1));
                        SetSentErrorMessage(true);
                        return false;
                    }
                    amount += delta;
                }
                break;
            }
            amount += Player::ReputationRank_Length[r];
        }
        if (r >= MAX_REPUTATION_RANK)
        {
            PSendSysMessage(LANG_COMMAND_FACTION_INVPARAM, rankTxt);
            SetSentErrorMessage(true);
            return false;
        }
    }

    FactionEntry const *factionEntry = sFactionStore.LookupEntry(factionId);

    if (!factionEntry)
    {
        PSendSysMessage(LANG_COMMAND_FACTION_UNKNOWN, factionId);
        SetSentErrorMessage(true);
        return false;
    }

    if (factionEntry->reputationListID < 0)
    {
        PSendSysMessage(LANG_COMMAND_FACTION_NOREP_ERROR, factionEntry->name[GetSessionDbcLocale()], factionId);
        SetSentErrorMessage(true);
        return false;
    }

    target->SetFactionReputation(factionEntry,amount);
    PSendSysMessage(LANG_COMMAND_MODIFY_REP, factionEntry->name[GetSessionDbcLocale()], factionId, target->GetName().c_str(), target->GetReputation(factionId));
    return true;
}

bool ChatHandler::HandleNameCommand(const char* args)
{
    ARGS_CHECK

    uint32 id = atoi(args);

    Creature* pCreature = GetSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    if (CreatureTemplate const *cinfo = sObjectMgr->GetCreatureTemplate(id))
        pCreature->SetUInt32Value(OBJECT_FIELD_ENTRY, id);
    else
        PSendSysMessage("Creature template with id %u not found", id);

    return true;
}

//move item to other slot
bool ChatHandler::HandleItemMoveCommand(const char* args)
{
    ARGS_CHECK

    uint8 srcslot, dstslot;

    char* pParam1 = strtok((char*)args, " ");
    if (!pParam1)
        return false;

    char* pParam2 = strtok(nullptr, " ");
    if (!pParam2)
        return false;

    srcslot = (uint8)atoi(pParam1);
    dstslot = (uint8)atoi(pParam2);

    if(srcslot==dstslot)
        return true;

    if(!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0,srcslot))
        return false;

    if(!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0,dstslot))
        return false;

    uint16 src = ((INVENTORY_SLOT_BAG_0 << 8) | srcslot);
    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | dstslot);

    m_session->GetPlayer()->SwapItem( src, dst );

    return true;
}

//add spawn of creature
bool ChatHandler::HandleNpcAddCommand(const char* args)
{
    ARGS_CHECK
    char* cId = strtok((char*)args, " ");
    if (!cId)
        return false;

    uint32 id  = atoi(cId);

    Player *chr = m_session->GetPlayer();
    float x = chr->GetPositionX();
    float y = chr->GetPositionY();
    float z = chr->GetPositionZ();
    float o = chr->GetOrientation();
    Map *map = chr->GetMap();

    if (Transport* tt = chr->GetTransport())
        if (MotionTransport* trans = tt->ToMotionTransport())
        {
            uint32 guid = sObjectMgr->GenerateLowGuid(HIGHGUID_UNIT);
            CreatureData& data = sObjectMgr->NewOrExistCreatureData(guid);
            data.id = id;
            data.posX = chr->GetTransOffsetX();
            data.posY = chr->GetTransOffsetY();
            data.posZ = chr->GetTransOffsetZ();
            data.orientation = chr->GetTransOffsetO();
            data.displayid = 0;
            data.equipmentId = 0;
            data.spawntimesecs = 300;
            data.spawndist = 0;
            data.movementType = 1;
            data.spawnMask = 1;

            if(!trans->GetGOInfo())
            {
                SendSysMessage("Error: cannot save creature on transport because trans->GetGOInfo() == NULL");
                return true;
            }
            if(Creature* creature = trans->CreateNPCPassenger(guid, &data))
            {
                sObjectMgr->AddCreatureToGrid(guid, &data);
                creature->SaveToDB(trans->GetGOInfo()->moTransport.mapID, 1 << map->GetSpawnMode());
            } else {
                SendSysMessage("Error: cannot create NPC Passenger.");
            }
            return true;
        }

    auto  pCreature = new Creature;
    if (!pCreature->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_UNIT), map, id))
    {
        delete pCreature;
        return false;
    }

    pCreature->Relocate(x,y,z,o);

    if(!pCreature->IsPositionValid())
    {
        TC_LOG_ERROR("command","ERROR: Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",pCreature->GetGUIDLow(),pCreature->GetEntry(),pCreature->GetPositionX(),pCreature->GetPositionY());
        delete pCreature;
        return false;
    }

    pCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()));

    uint32 db_guid = pCreature->GetDBTableGUIDLow();

    // To call _LoadGoods(); _LoadQuests(); CreateTrainerSpells();
    pCreature->LoadFromDB(db_guid, map);

    map->Add(pCreature);
    sObjectMgr->AddCreatureToGrid(db_guid, sObjectMgr->GetCreatureData(db_guid));
    return true;
}

bool ChatHandler::HandleNpcDeleteCommand(const char* args)
{
    

    Creature* unit = nullptr;

    if(*args)
    {
        // number or [name] Shift-click form |color|Hcreature:creature_guid|h[name]|h|r
        char* cId = extractKeyFromLink((char*)args,"Hcreature");
        if(!cId)
            return false;

        uint32 lowguid = atoi(cId);
        if(!lowguid)
            return false;

        if (CreatureData const* cr_data = sObjectMgr->GetCreatureData(lowguid))
            unit = ObjectAccessor::GetCreature(*m_session->GetPlayer(), MAKE_NEW_GUID(lowguid, cr_data->id, HIGHGUID_UNIT));
    }
    else
        unit = GetSelectedCreature();

    if(!unit || unit->IsPet() || unit->IsTotem())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    // Delete the creature
    unit->DeleteFromDB();
    unit->AddObjectToRemoveList();

    SendSysMessage(LANG_COMMAND_DELCREATMESSAGE);

    return true;
}

//delete object by selection or guid
bool ChatHandler::HandleDelObjectCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hgameobject:go_guid|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = nullptr;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr->GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    uint64 owner_guid = obj->GetOwnerGUID();
    if(owner_guid)
    {
        Unit* owner = ObjectAccessor::GetUnit(*m_session->GetPlayer(),owner_guid);
        if(!owner && !IS_PLAYER_GUID(owner_guid))
        {
            PSendSysMessage(LANG_COMMAND_DELOBJREFERCREATURE, GUID_LOPART(owner_guid), obj->GetGUIDLow());
            SetSentErrorMessage(true);
            return false;
        }

        if(owner)
            owner->RemoveGameObject(obj,false);
    }

    obj->SetRespawnTime(0);                                 // not save respawn time
    obj->Delete();
    obj->DeleteFromDB();

    PSendSysMessage(LANG_COMMAND_DELOBJMESSAGE, obj->GetGUIDLow());

    return true;
}

//turn selected object
bool ChatHandler::HandleTurnObjectCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hgameobject:go_id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = nullptr;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr->GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    char* po = strtok(nullptr, " ");
    float o;

    if (po)
    {
        o = (float)atof(po);
    }
    else
    {
        Player *chr = m_session->GetPlayer();
        o = chr->GetOrientation();
    }

    float rot2 = sin(o/2);
    float rot3 = cos(o/2);

    Map* map = sMapMgr->CreateMap(obj->GetMapId(),obj);
    map->Remove(obj,false);

    obj->Relocate(obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(), o);

    obj->SetFloatValue(GAMEOBJECT_FACING, o);
    obj->SetFloatValue(GAMEOBJECT_PARENTROTATION+2, rot2);
    obj->SetFloatValue(GAMEOBJECT_PARENTROTATION+3, rot3);

    map->Add(obj);

    obj->SaveToDB();
    obj->Refresh();

    PSendSysMessage(LANG_COMMAND_TURNOBJMESSAGE, obj->GetGUIDLow(), o);

    return true;
}

//move selected creature
bool ChatHandler::HandleNpcMoveCommand(const char* args)
{
    

    uint32 lowguid = 0;

    Creature* pCreature = GetSelectedCreature();

    if(!pCreature)
    {
        // number or [name] Shift-click form |color|Hcreature:creature_guid|h[name]|h|r
        char* cId = extractKeyFromLink((char*)args,"Hcreature");
        if(!cId)
            return false;

        uint32 lowguid = atoi(cId);

        /* FIXME: impossibel without entry
        if(lowguid)
            pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_GUID(lowguid,HIGHGUID_UNIT));
        */

        // Attempting creature load from DB data
        if(!pCreature)
        {
            CreatureData const* data = sObjectMgr->GetCreatureData(lowguid);
            if(!data)
            {
                PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowguid);
                SetSentErrorMessage(true);
                return false;
            }

            uint32 map_id = data->mapid;

            if(m_session->GetPlayer()->GetMapId()!=map_id)
            {
                PSendSysMessage(LANG_COMMAND_CREATUREATSAMEMAP, lowguid);
                SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            lowguid = pCreature->GetDBTableGUIDLow();
        }
    }
    else
    {
        lowguid = pCreature->GetDBTableGUIDLow();
    }
    float x,y,z,o;

    if(Transport* trans = m_session->GetPlayer()->GetTransport())
    {
        x = m_session->GetPlayer()->GetTransOffsetX();
        y = m_session->GetPlayer()->GetTransOffsetY();
        z = m_session->GetPlayer()->GetTransOffsetZ();
        o = m_session->GetPlayer()->GetTransOffsetO();
    } else {
        x = m_session->GetPlayer()->GetPositionX();
        y = m_session->GetPlayer()->GetPositionY();
        z = m_session->GetPlayer()->GetPositionZ();
        o = m_session->GetPlayer()->GetOrientation();
    }

    if (pCreature)
    {
        if(CreatureData const* data = sObjectMgr->GetCreatureData(pCreature->GetDBTableGUIDLow()))
        {
            const_cast<CreatureData*>(data)->posX = x;
            const_cast<CreatureData*>(data)->posY = y;
            const_cast<CreatureData*>(data)->posZ = z;
            const_cast<CreatureData*>(data)->orientation = o;
        }
        pCreature->SetPosition(x, y, z, o);
        pCreature->InitCreatureAddon(true);
        pCreature->GetMotionMaster()->Initialize();
        if(pCreature->IsAlive())                            // dead creature will reset movement generator at respawn
        {
            pCreature->SetDeathState(JUST_DIED);
            pCreature->Respawn();
        }
    }

    WorldDatabase.PExecute("UPDATE creature SET position_x = '%f', position_y = '%f', position_z = '%f', orientation = '%f' WHERE guid = '%u'", x, y, z, o, lowguid);

    PSendSysMessage(LANG_COMMAND_CREATUREMOVED);
    return true;
}

bool ChatHandler::HandleNpcFlyCommand(const char* args)
{
    Creature* pCreature = GetSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    ARGS_CHECK

    std::string argstr = (char*)args;

    if (argstr == "on")
    {
        pCreature->SetCanFly(true);
        PSendSysMessage("Creature is now fly-capable");
        return true;
    } else if (argstr == "off")
    {
        pCreature->SetCanFly(false);
        PSendSysMessage("Creature is now not fly-capable");
        return true;
    }

    return false;
}

bool ChatHandler::HandleNpcGotoCommand(const char* args)
{
    Creature* pCreature = GetSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    ARGS_CHECK

    return true;
}

//move selected object
bool ChatHandler::HandleMoveObjectCommand(const char* args)
{
    // number or [name] Shift-click form |color|Hgameobject:go_guid|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = nullptr;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr->GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    char* px = strtok(nullptr, " ");
    char* py = strtok(nullptr, " ");
    char* pz = strtok(nullptr, " ");

    if (!px)
    {
        Player *chr = m_session->GetPlayer();

        Map* map = sMapMgr->CreateMap(obj->GetMapId(),obj);
        map->Remove(obj,false);

        obj->Relocate(chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ(), obj->GetOrientation());
        obj->SetFloatValue(GAMEOBJECT_POS_X, chr->GetPositionX());
        obj->SetFloatValue(GAMEOBJECT_POS_Y, chr->GetPositionY());
        obj->SetFloatValue(GAMEOBJECT_POS_Z, chr->GetPositionZ());

        map->Add(obj, true);
    }
    else
    {
        if(!py || !pz)
            return false;

        float x = (float)atof(px);
        float y = (float)atof(py);
        float z = (float)atof(pz);

        if(!MapManager::IsValidMapCoord(obj->GetMapId(),x,y,z))
        {
            PSendSysMessage(LANG_INVALID_TARGET_COORD,x,y,obj->GetMapId());
            SetSentErrorMessage(true);
            return false;
        }

        Map* map = sMapMgr->CreateMap(obj->GetMapId(),obj);
        map->Remove(obj,false);

        obj->Relocate(x, y, z, obj->GetOrientation());
        obj->SetFloatValue(GAMEOBJECT_POS_X, x);
        obj->SetFloatValue(GAMEOBJECT_POS_Y, y);
        obj->SetFloatValue(GAMEOBJECT_POS_Z, z);

        map->Add(obj, true);
    }

    obj->SaveToDB();
    obj->Refresh();

    PSendSysMessage(LANG_COMMAND_MOVEOBJMESSAGE, obj->GetGUIDLow());

    return true;
}

//Set a new mail and check if a change is pending
bool ChatHandler::HandleAccountMailChangeCommand(const char* args)
{
    ARGS_CHECK

    char* sAccount = strtok((char*)args, " ");
    char* mail = strtok(nullptr, " ");

    if (!sAccount || !mail)
        return false;

    std::string account_name = sAccount;
    if(!AccountMgr::normalizeString(account_name))
    {
        PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 targetAccountId = sAccountMgr->GetId(account_name);
    if (!targetAccountId)
    {
        PSendSysMessage(LANG_ACCOUNT_NOT_EXIST,account_name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

     LoginDatabase.PExecute("UPDATE account SET email='%s', newMail=NULL, newMailTS='0' WHERE id=%u", mail, targetAccountId);
     //TODO translate
     PSendSysMessage("Mail changed.");
     return true;
}

//demorph player or unit
bool ChatHandler::HandleDeMorphCommand(const char* /*args*/)
{
    Unit *target = GetSelectedUnit();
    if(!target)
        target = m_session->GetPlayer();

    target->DeMorph();

    return true;
}

//add item in vendorlist
bool ChatHandler::HandleAddVendorItemCommand(const char* args)
{
    ARGS_CHECK

    char* pitem  = extractKeyFromLink((char*)args,"Hitem");
    if (!pitem)
    {
        SendSysMessage(LANG_COMMAND_NEEDITEMSEND);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 itemId = atol(pitem);

    char* fmaxcount = strtok(nullptr, " ");                    //add maxcount, default: 0
    uint32 maxcount = 0;
    if (fmaxcount)
        maxcount = atol(fmaxcount);

    char* fincrtime = strtok(nullptr, " ");                    //add incrtime, default: 0
    uint32 incrtime = 0;
    if (fincrtime)
        incrtime = atol(fincrtime);

    char* fextendedcost = strtok(nullptr, " ");                //add ExtendedCost, default: 0
    uint32 extendedcost = fextendedcost ? atol(fextendedcost) : 0;

    Creature* vendor = GetSelectedCreature();

    uint32 vendor_entry = vendor ? vendor->GetEntry() : 0;

    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemId);
    if(!pProto)
    {
        PSendSysMessage("Invalid id");
        return true;
    }

    if(!sObjectMgr->IsVendorItemValid(vendor_entry,pProto,maxcount,incrtime,extendedcost,m_session->GetPlayer()))
    {
        SetSentErrorMessage(true);
        return false;
    }

    sObjectMgr->AddVendorItem(vendor_entry,pProto,maxcount,incrtime,extendedcost);


    PSendSysMessage(LANG_ITEM_ADDED_TO_LIST,itemId,pProto->Name1.c_str(),maxcount,incrtime,extendedcost);
    return true;
}

//del item from vendor list
bool ChatHandler::HandleDelVendorItemCommand(const char* args)
{
    ARGS_CHECK

    Creature* vendor = GetSelectedCreature();
    if (!vendor || !vendor->IsVendor())
    {
        SendSysMessage(LANG_COMMAND_VENDORSELECTION);
        SetSentErrorMessage(true);
        return false;
    }

    char* pitem  = extractKeyFromLink((char*)args,"Hitem");
    if (!pitem)
    {
        SendSysMessage(LANG_COMMAND_NEEDITEMSEND);
        SetSentErrorMessage(true);
        return false;
    }
    uint32 itemId = atol(pitem);
    
    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemId);
    if(!pProto)
    {
        PSendSysMessage("Invalid id");
        return true;
    }

    if(!sObjectMgr->RemoveVendorItem(vendor->GetEntry(),pProto))
    {
        PSendSysMessage(LANG_ITEM_NOT_IN_LIST,itemId);
        SetSentErrorMessage(true);
        return false;
    }


    PSendSysMessage(LANG_ITEM_DELETED_FROM_LIST,itemId,pProto->Name1.c_str());
    return true;
}

/**
 * Set the movement type for an NPC.<br/>
 * <br/>
 * Valid movement types are:
 * <ul>
 * <li> stay - NPC wont move </li>
 * <li> random - NPC will move randomly according to the spawndist </li>
 * <li> way - NPC will move with given waypoints set </li>
 * </ul>
 * additional parameter: NODEL - so no waypoints are deleted, if you
 *                       change the movement type
 */
bool ChatHandler::HandleNpcSetMoveTypeCommand(const char* args)
{
    ARGS_CHECK

    // 3 arguments:
    // GUID (optional - you can also select the creature)
    // stay|random|way (determines the kind of movement)
    // NODEL (optional - tells the system NOT to delete any waypoints)
    //        this is very handy if you want to do waypoints, that are
    //        later switched on/off according to special events (like escort
    //        quests, etc)
    char* guid_str = strtok((char*)args, " ");
    char* type_str = strtok((char*)nullptr, " ");
    char* dontdel_str = strtok((char*)nullptr, " ");

    bool doNotDelete = false;

    if(!guid_str)
        return false;

    uint32 lowguid = 0;
    Creature* pCreature = nullptr;

    if( dontdel_str )
    {
        //TC_LOG_ERROR("command","DEBUG: All 3 params are set");

        // All 3 params are set
        // GUID
        // type
        // doNotDEL
        if( stricmp( dontdel_str, "NODEL" ) == 0 )
        {
            //TC_LOG_ERROR("command","DEBUG: doNotDelete = true;");
            doNotDelete = true;
        }
    }
    else
    {
        // Only 2 params - but maybe NODEL is set
        if( type_str )
        {
            TC_LOG_ERROR("command","DEBUG: Only 2 params ");
            if( stricmp( type_str, "NODEL" ) == 0 )
            {
                //TC_LOG_ERROR("command","DEBUG: type_str, NODEL ");
                doNotDelete = true;
                type_str = nullptr;
            }
        }
    }

    if(!type_str)                                           // case .setmovetype $move_type (with selected creature)
    {
        type_str = guid_str;
        pCreature = GetSelectedCreature();
        if(!pCreature || pCreature->IsPet())
            return false;
        lowguid = pCreature->GetDBTableGUIDLow();
    }
    else                                                    // case .setmovetype #creature_guid $move_type (with selected creature)
    {
        lowguid = atoi((char*)guid_str);

        /* impossible without entry
        if(lowguid)
            pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_GUID(lowguid,HIGHGUID_UNIT));
        */

        // attempt check creature existence by DB data
        if(!pCreature)
        {
            CreatureData const* data = sObjectMgr->GetCreatureData(lowguid);
            if(!data)
            {
                PSendSysMessage(LANG_COMMAND_CREATGUIDNOTFOUND, lowguid);
                SetSentErrorMessage(true);
                return false;
            }
        }
        else
        {
            lowguid = pCreature->GetDBTableGUIDLow();
        }
    }

    // now lowguid is low guid really existed creature
    // and pCreature point (maybe) to this creature or NULL

    MovementGeneratorType move_type;

    std::string type = type_str;

    if(type == "stay")
        move_type = IDLE_MOTION_TYPE;
    else if(type == "random")
        move_type = RANDOM_MOTION_TYPE;
    else if(type == "way")
        move_type = WAYPOINT_MOTION_TYPE;
    else
        return false;

    if(pCreature)
    {
        // update movement type
        if(doNotDelete == false)
            pCreature->LoadPath(0);

        pCreature->SetDefaultMovementType(move_type);
        pCreature->GetMotionMaster()->Initialize();
        if(pCreature->IsAlive())                            // dead creature will reset movement generator at respawn
        {
            pCreature->SetDeathState(JUST_DIED);
            pCreature->Respawn();
        }
        pCreature->SaveToDB();
    }
    if( doNotDelete == false )
    {
        PSendSysMessage(LANG_MOVE_TYPE_SET,type_str);
    }
    else
    {
        PSendSysMessage(LANG_MOVE_TYPE_SET_NODEL,type_str);
    }

    return true;
}                                                           // HandleNpcSetMoveTypeCommand

//change level of creature or pet
bool ChatHandler::HandleChangeLevelCommand(const char* args)
{
    ARGS_CHECK

    uint8 lvl = (uint8) atoi((char*)args);
    if ( lvl < 1 || lvl > sWorld->getConfig(CONFIG_MAX_PLAYER_LEVEL) + 3)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Creature* pCreature = GetSelectedCreature();
    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if(pCreature->IsPet())
    {
        ((Pet*)pCreature)->GivePetLevel(lvl);
    }
    else
    {
        pCreature->SetMaxHealth( 100 + 30*lvl);
        pCreature->SetHealth( 100 + 30*lvl);
        pCreature->SetLevel( lvl);
        pCreature->SaveToDB();
    }

    return true;
}

//set npcflag of creature
bool ChatHandler::HandleNpcFlagCommand(const char* args)
{
    ARGS_CHECK

    uint32 npcFlags = (uint32) atoi((char*)args);

    Creature* pCreature = GetSelectedCreature();

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->SetUInt32Value(UNIT_NPC_FLAGS, npcFlags);

    //WorldDatabase.PExecute("UPDATE creature_template SET npcflag = '%u' WHERE entry = '%u'", npcFlags, pCreature->GetEntry());

    SendSysMessage(LANG_VALUE_SAVED_REJOIN);

    return true;
}

//set model of creature
bool ChatHandler::HandleNpcSetModelCommand(const char* args)
{
    ARGS_CHECK

    uint32 displayId = (uint32) atoi((char*)args);

    Creature *pCreature = GetSelectedCreature();

    if(!pCreature || pCreature->IsPet())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->SetDisplayId(displayId);
    pCreature->SetNativeDisplayId(displayId);

    pCreature->SaveToDB();

    return true;
}

//morph creature or player
bool ChatHandler::HandleMorphCommand(const char* args)
{
    ARGS_CHECK

    uint16 display_id = 0;

    if (strcmp("random", args) == 0)
    {
        display_id = urand(4,25958);
        PSendSysMessage("displayid: %u",display_id);
    } else
       display_id = (uint16)atoi((char*)args);

    if(!display_id)
        return false;

    Unit *target = GetSelectedUnit();
    if(!target)
        target = m_session->GetPlayer();

    target->SetDisplayId(display_id);

    return true;
}

//set faction of creature
bool ChatHandler::HandleNpcFactionIdCommand(const char* args)
{
    ARGS_CHECK

    uint32 factionId = (uint32) atoi((char*)args);

    if (!sFactionTemplateStore.LookupEntry(factionId))
    {
        PSendSysMessage(LANG_WRONG_FACTION, factionId);
        SetSentErrorMessage(true);
        return false;
    }

    Creature* pCreature = GetSelectedCreature();

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    pCreature->SetFaction(factionId);

    // faction is set in creature_template - not inside creature

    // update in memory
    if(CreatureTemplate const *cinfo = pCreature->GetCreatureTemplate())
    {
        const_cast<CreatureTemplate*>(cinfo)->faction = factionId;
    }

    // and DB
    //WorldDatabase.PExecute("UPDATE creature_template SET faction = '%u' WHERE entry = '%u'", factionId, pCreature->GetEntry());

    return true;
}

//kick player
bool ChatHandler::HandleKickPlayerCommand(const char *args)
{
    const char* kickName = strtok((char*)args, " ");
    char* kickReason = strtok(nullptr, "\n");
    std::string reason = "No Reason";
    std::string kicker = "Console";
    if(kickReason)
        reason = kickReason;
    if(m_session)
        kicker = m_session->GetPlayer()->GetName();

    if(!kickName)
     {
        Player* player = GetSelectedPlayerOrSelf();
        if(!player)
        {
            SendSysMessage(LANG_NO_CHAR_SELECTED);
            SetSentErrorMessage(true);
            return false;
        }

        if(player==m_session->GetPlayer())
        {
            SendSysMessage(LANG_COMMAND_KICKSELF);
            SetSentErrorMessage(true);
            return false;
        }

        if(sWorld->getConfig(CONFIG_SHOW_KICK_IN_WORLD) == 1)
        {

            sWorld->SendWorldText(LANG_COMMAND_KICKMESSAGE, player->GetName().c_str(), kicker.c_str(), reason.c_str());
        }
        else
        {

            PSendSysMessage(LANG_COMMAND_KICKMESSAGE, player->GetName().c_str(), kicker.c_str(), reason.c_str());
        }

        player->GetSession()->KickPlayer();
    }
    else
    {
        std::string name = kickName;
        if(!normalizePlayerName(name))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        if(m_session && name==m_session->GetPlayer()->GetName())
        {
            SendSysMessage(LANG_COMMAND_KICKSELF);
            SetSentErrorMessage(true);
            return false;
        }

        Player* player = sObjectAccessor->FindConnectedPlayerByName(kickName);
        if(!player)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        if(m_session && player->GetSession()->GetSecurity() > m_session->GetSecurity())
        {
            SendSysMessage(LANG_YOURS_SECURITY_IS_LOW); //maybe replacement string for this later on
            SetSentErrorMessage(true);
            return false;
        }

        if(sWorld->KickPlayer(name.c_str()))
        {
            if(sWorld->getConfig(CONFIG_SHOW_KICK_IN_WORLD) == 1)
            {

                sWorld->SendWorldText(LANG_COMMAND_KICKMESSAGE, name.c_str(), kicker.c_str(), reason.c_str());
            }
            else
            {
                PSendSysMessage(LANG_COMMAND_KICKMESSAGE, name.c_str(), kicker.c_str(), reason.c_str());
            }
        }
        else
        {
            PSendSysMessage(LANG_COMMAND_KICKNOTFOUNDPLAYER, name.c_str());
            return false;
        }
    }
    return true;
}

//show info of player
bool ChatHandler::HandlePInfoCommand(const char* args)
{
    Player* target = nullptr;
    uint64 targetGUID = 0;

    PreparedStatement* stmt = nullptr;

    char* px = strtok((char*)args, " ");
    char* py = nullptr;

    std::string name;

    if (px)
    {
        name = px;

        if(name.empty())
            return false;

        if(!normalizePlayerName(name))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        target = sObjectAccessor->FindConnectedPlayerByName(name.c_str());
        if (target)
            py = strtok(nullptr, " ");
        else
        {
            targetGUID = sCharacterCache->GetCharacterGuidByName(name);
            if(targetGUID)
                py = strtok(nullptr, " ");
            else
                py = px;
        }
    }

    if(!target && !targetGUID)
    {
        target = GetSelectedPlayer();
    }

    if(!target && !targetGUID)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 accId = 0;
    uint32 money = 0;
    uint32 total_player_time = 0;
    uint32 level = 0;
    uint32 latency = 0;

    // get additional information from Player object
    if(target)
    {
        targetGUID = target->GetGUID();
        name = target->GetName();                           // re-read for case GetSelectedPlayer() target
        accId = target->GetSession()->GetAccountId();
        money = target->GetMoney();
        total_player_time = target->GetTotalPlayedTime();
        level = target->GetLevel();
        latency = target->GetSession()->GetLatency();
    }
    // get additional information from DB
    else
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_PINFO);
        stmt->setUInt32(0, GUID_LOPART(targetGUID));
        PreparedQueryResult result = CharacterDatabase.Query(stmt);

        if (!result)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }
        
        Field *fields = result->Fetch();
        total_player_time = fields[0].GetUInt32();
        level = fields[1].GetUInt8();
        money = fields[2].GetUInt32();
        accId = fields[3].GetUInt32();
    }

    std::string username = GetTrinityString(LANG_ERROR);
    std::string last_ip = GetTrinityString(LANG_ERROR);
    uint8 security = 0;
    std::string last_login = GetTrinityString(LANG_ERROR);
    std::string current_mail = GetTrinityString(LANG_ERROR);
    std::string reg_mail = GetTrinityString(LANG_ERROR);

    // Query the prepared statement for login data
    stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_PINFO);
    stmt->setInt32(0, int32(realm.Id.Realm));
    stmt->setUInt32(1, accId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if(result)
    {
        Field* fields = result->Fetch();
        username = fields[0].GetString();
        security = fields[1].GetUInt8();

        if(!m_session || m_session->GetSecurity() >= security)
        {
            current_mail = fields[2].GetString();
            reg_mail = fields[3].GetString();
            last_ip = fields[4].GetString();
            last_login = fields[5].GetString();
        }
        else
        {
            current_mail = "-";
            reg_mail = "-";
            last_ip = "-";
            last_login = "-";
        }
    }

    PSendSysMessage(LANG_PINFO_ACCOUNT, (target?"":GetTrinityString(LANG_OFFLINE)), name.c_str(), GUID_LOPART(targetGUID), username.c_str(), accId, security, last_ip.c_str(), last_login.c_str(), latency);

    std::string timeStr = secsToTimeString(total_player_time,true,true);
    uint32 gold = money /GOLD;
    uint32 silv = (money % GOLD) / SILVER;
    uint32 copp = (money % GOLD) % SILVER;
    PSendSysMessage(LANG_PINFO_LEVEL,  timeStr.c_str(), level, gold,silv,copp );

    PSendSysMessage("Current mail: %s",current_mail.c_str());
    
    if ( py && strncmp(py, "rep", 3) == 0 )
    {
        if(!target)
        {
            // rep option not implemented for offline case
            SendSysMessage(LANG_PINFO_NO_REP);
            SetSentErrorMessage(true);
            return false;
        }

        char const* FactionName;
        for(FactionStateList::const_iterator itr = target->m_factions.begin(); itr != target->m_factions.end(); ++itr)
        {
            FactionEntry const *factionEntry = sFactionStore.LookupEntry(itr->second.ID);
            if (factionEntry)
                FactionName = factionEntry->name[GetSessionDbcLocale()];
            else
                FactionName = "#Not found#";
            ReputationRank rank = target->GetReputationRank(factionEntry);
            std::string rankName = GetTrinityString(ReputationRankStrIndex[rank]);
            std::ostringstream ss;
            ss << itr->second.ID << ": |cffffffff|Hfaction:" << itr->second.ID << "|h[" << FactionName << "]|h|r " << rankName << "|h|r (" << target->GetReputation(factionEntry) << ")";

            if(itr->second.Flags & FACTION_FLAG_VISIBLE)
                ss << GetTrinityString(LANG_FACTION_VISIBLE);
            if(itr->second.Flags & FACTION_FLAG_AT_WAR)
                ss << GetTrinityString(LANG_FACTION_ATWAR);
            if(itr->second.Flags & FACTION_FLAG_PEACE_FORCED)
                ss << GetTrinityString(LANG_FACTION_PEACE_FORCED);
            if(itr->second.Flags & FACTION_FLAG_HIDDEN)
                ss << GetTrinityString(LANG_FACTION_HIDDEN);
            if(itr->second.Flags & FACTION_FLAG_INVISIBLE_FORCED)
                ss << GetTrinityString(LANG_FACTION_INVISIBLE_FORCED);
            if(itr->second.Flags & FACTION_FLAG_INACTIVE)
                ss << GetTrinityString(LANG_FACTION_INACTIVE);

            SendSysMessage(ss.str().c_str());
        }
    }
    return true;
}

//set spawn dist of creature
bool ChatHandler::HandleNpcSpawnDistCommand(const char* args)
{
    ARGS_CHECK

    float option = atof((char*)args);
    if (option < 0.0f)
    {
        SendSysMessage(LANG_BAD_VALUE);
        return false;
    }

    MovementGeneratorType mtype = IDLE_MOTION_TYPE;
    if (option >0.0f)
        mtype = RANDOM_MOTION_TYPE;

    Creature *pCreature = GetSelectedCreature();
    uint32 u_guidlow = 0;

    if (pCreature)
        u_guidlow = pCreature->GetDBTableGUIDLow();
    else
        return false;

    pCreature->SetRespawnRadius((float)option);
    pCreature->SetDefaultMovementType(mtype);
    pCreature->GetMotionMaster()->Initialize();
    if(pCreature->IsAlive())                                // dead creature will reset movement generator at respawn
    {
        pCreature->SetDeathState(JUST_DIED);
        pCreature->Respawn();
    }

    WorldDatabase.PExecute("UPDATE creature SET spawndist=%f, MovementType=%i WHERE guid=%u",option,mtype,u_guidlow);
    PSendSysMessage(LANG_COMMAND_SPAWNDIST,option);
    return true;
}

bool ChatHandler::HandleNpcSpawnTimeCommand(const char* args)
{
    ARGS_CHECK

    char* stime = strtok((char*)args, " ");

    if (!stime)
        return false;

    int i_stime = atoi((char*)stime);

    if (i_stime < 0)
    {
        SendSysMessage(LANG_BAD_VALUE);
        SetSentErrorMessage(true);
        return false;
    }

    Creature *pCreature = GetSelectedCreature();
    uint32 u_guidlow = 0;

    if (pCreature)
        u_guidlow = pCreature->GetDBTableGUIDLow();
    else
        return false;

    WorldDatabase.PExecute("UPDATE creature SET spawntimesecs=%i WHERE guid=%u",i_stime,u_guidlow);
    pCreature->SetRespawnDelay((uint32)i_stime);
    PSendSysMessage(LANG_COMMAND_SPAWNTIME,i_stime);

    return true;
}

/////WAYPOINT COMMANDS

bool ChatHandler::HandleWpAddCommand(const char* args)
{
    
    // optional
    char* path_number = nullptr;
    uint32 pathid = 0;

    if(*args)
        path_number = strtok((char*)args, " ");

    uint32 point = 0;
    Creature* target = GetSelectedCreature();

    if (!path_number)
        {
        if(target)
            pathid = target->GetWaypointPathId();
                else
                {
                    QueryResult result = WorldDatabase.PQuery( "SELECT MAX(id) FROM waypoint_data");
                    if(result)
                    {
                        uint32 maxpathid = result->Fetch()->GetInt32();
                        pathid = maxpathid+1;
                        PSendSysMessage("%s%s|r", "|cff00ff00", "New path started.");
                    }
                }
        }
        else
            pathid = atoi(path_number);

    // path_id -> ID of the Path
    // point   -> number of the waypoint (if not 0)

    if(!pathid)
    {
        PSendSysMessage("%s%s|r", "|cffff33ff", "Current creature haven't loaded path.");
        return true;
    }

    QueryResult result = WorldDatabase.PQuery( "SELECT MAX(point) FROM waypoint_data WHERE id = '%u'",pathid);

    if( result )
    {
        point = (*result)[0].GetUInt32();
    }

    Player* player = m_session->GetPlayer();

    WorldDatabase.PExecute("INSERT INTO waypoint_data (id, point, position_x, position_y, position_z) VALUES ('%u','%u','%f', '%f', '%f')",
        pathid, point+1, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());

        PSendSysMessage("%s%s%u%s%u%s|r", "|cff00ff00", "PathID: |r|cff00ffff", pathid, "|r|cff00ff00: Waypoint |r|cff00ffff", point,"|r|cff00ff00 created. ");

    return true;
}                                                           // HandleWpAddCommand

bool ChatHandler::HandleWpLoadPathCommand(const char *args)
{
    ARGS_CHECK

    // optional
    char* path_number = nullptr;

    if(*args)
        path_number = strtok((char*)args, " ");


    uint32 pathid = 0;
    uint32 guidlow = 0;
    Creature* target = GetSelectedCreature();

    if(!target)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if(target->GetEntry() == 1)
    {
        PSendSysMessage("%s%s|r", "|cffff33ff", "You want to load path to a waypoint? Aren't you?");
        SetSentErrorMessage(true);
        return false;
    }

    pathid = atoi(path_number);

    if(!pathid)
    {
        PSendSysMessage("%s%s|r", "|cffff33ff", "No valid path number provided.");
        return true;
    }

    guidlow = target->GetDBTableGUIDLow();
    QueryResult result = WorldDatabase.PQuery( "SELECT guid FROM creature_addon WHERE guid = '%u'",guidlow);

    if( result )
    {
        WorldDatabase.PExecute("UPDATE creature_addon SET path_id = '%u' WHERE guid = '%u'", pathid, guidlow);
    }
    else
    {
        //if there's any creature_template_addon, let's base ourserlves on it
        result = WorldDatabase.PQuery( "SELECT mount,bytes0,bytes1,bytes2,emote,moveflags,auras FROM creature_template_addon WHERE entry = '%u'",target->GetEntry());
        if(result)
        {
            uint32 mount = (*result)[0].GetUInt32();
            uint32 bytes0 = (*result)[1].GetUInt32();
            uint32 bytes1 = (*result)[2].GetUInt32();
            uint32 bytes2 = (*result)[3].GetUInt32();
            uint32 emote = (*result)[4].GetUInt32();
            uint32 moveflags = (*result)[5].GetUInt32();
            const char* auras = (*result)[6].GetCString();
            WorldDatabase.PExecute("INSERT INTO creature_addon(guid,path_id,mount,bytes0,bytes1,bytes2,emote,moveflags,auras) VALUES \
                                   ('%u','%u','%u','%u','%u','%u','%u','%u','%s')", guidlow, pathid,mount,bytes0,bytes1,bytes2,emote,moveflags,auras);
        } else { //else just create a new entry
            WorldDatabase.PExecute("INSERT INTO creature_addon(guid,path_id) VALUES ('%u','%u')", guidlow, pathid);
        }
    }

    WorldDatabase.PExecute("UPDATE creature SET MovementType = '%u' WHERE guid = '%u'", WAYPOINT_MOTION_TYPE, guidlow);

    target->LoadPath(pathid);
    target->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
    target->GetMotionMaster()->Initialize();
    target->Say("Path loaded.",LANG_UNIVERSAL,nullptr);

    return true;
}


bool ChatHandler::HandleReloadAllPaths(const char* args)
{
    ARGS_CHECK

    uint32 id = atoi(args);

    if(!id)
        return false;

    PSendSysMessage("%s%s|r|cff00ffff%u|r", "|cff00ff00", "Loading Path: ", id);
    sWaypointMgr->ReloadPath(id);
    return true;
}

bool ChatHandler::HandleWpUnLoadPathCommand(const char *args)
{
    Creature* target = GetSelectedCreature();

    if(!target)
    {
        PSendSysMessage("%s%s|r", "|cff33ffff", "You must select target.");
        return true;
    }

    uint32 guidlow = target->GetDBTableGUIDLow();
    if(!guidlow)
    {
        PSendSysMessage("%s%s|r", "|cff33ffff", "Creature is not permanent.");
        return true;
    }

    if(target->GetCreatureAddon())
    {
        if(target->GetCreatureAddon()->path_id != 0)
        {
            WorldDatabase.PExecute("UPDATE creature_addon SET path_id = 0 WHERE guid = %u", guidlow);
            target->UpdateWaypointID(0);
            WorldDatabase.PExecute("UPDATE creature SET MovementType = '%u' WHERE guid = '%u'", IDLE_MOTION_TYPE, guidlow);
            target->LoadPath(0);
            target->SetDefaultMovementType(IDLE_MOTION_TYPE);
            target->GetMotionMaster()->MoveTargetedHome();
            target->GetMotionMaster()->Initialize();
            target->Say("Path unloaded.",LANG_UNIVERSAL,nullptr);
            return true;
        }
        PSendSysMessage("%s%s|r", "|cffff33ff", "Target have no loaded path. (1)");
        return true;
    }
    PSendSysMessage("%s%s|r", "|cffff33ff", "Target have no loaded path. (2)");
    return true;
}

bool ChatHandler::HandleWpEventCommand(const char* args)
{
    ARGS_CHECK

    char* show_str = strtok((char*)args, " ");

    std::string show = show_str;

    // Check
    if( (show != "add") && (show != "mod") && (show != "del") && (show != "listid")) return false;


    if(show == "add")
    {
    uint32 id = 0;
    char* arg_id = strtok(nullptr, " ");

    if(arg_id)
        id = atoi(arg_id);

    if(id)
    {
        QueryResult result = WorldDatabase.PQuery( "SELECT `id` FROM waypoint_scripts WHERE guid = %u", id);

        if( !result )
        {
            WorldDatabase.PExecute("INSERT INTO waypoint_scripts(guid)VALUES(%u)", id);
            PSendSysMessage("%s%s%u|r", "|cff00ff00", "Wp Event: New waypoint event added: ", id);
        }
        else
        {
            PSendSysMessage("|cff00ff00Wp Event: you have choosed an existing waypoint script guid: %u|r", id);
        }
    }
    else
    {
        if(QueryResult result = WorldDatabase.PQuery( "SELECT MAX(guid) FROM waypoint_scripts"))
        {
            id = result->Fetch()->GetUInt32();
            WorldDatabase.PExecute("INSERT INTO waypoint_scripts(guid)VALUES(%u)", id+1);
            PSendSysMessage("%s%s%u|r", "|cff00ff00","Wp Event: New waypoint event added: |r|cff00ffff", id+1);
        }
    }

    return true;
    }


    if(show == "listid")
    {
        uint32 id;
        char* arg_id = strtok(nullptr, " ");

        if(!arg_id)
        {
            PSendSysMessage("%s%s|r", "|cff33ffff","Wp Event: you must provide waypoint script id.");
            return true;
        }

        id = atoi(arg_id);

        uint32 a2, a3, a4, a5, a6;
        float a8, a9, a10, a11;
        char const* a7;

        QueryResult result = WorldDatabase.PQuery( "SELECT `guid`, `delay`, `command`, `datalong`, `datalong2`, `dataint`, `x`, `y`, `z`, `o` FROM waypoint_scripts WHERE id = %u", id);

        if( !result )
        {
            PSendSysMessage("%s%s%u|r", "|cff33ffff", "Wp Event: no waypoint scripts found on id: ", id);
            return true;
        }

        Field *fields;

        do
        {
            fields = result->Fetch();
            a2 = fields[0].GetUInt32();
            a3 = fields[1].GetUInt32();
            a4 = fields[2].GetUInt32();
            a5 = fields[3].GetUInt32();
            a6 = fields[4].GetUInt32();
            a7 = fields[5].GetCString();
            a8 = fields[6].GetFloat();
            a9 = fields[7].GetFloat();
            a10 = fields[8].GetFloat();
            a11 = fields[9].GetFloat();

            PSendSysMessage("|cffff33ffid:|r|cff00ffff %u|r|cff00ff00, guid: |r|cff00ffff%u|r|cff00ff00, delay: |r|cff00ffff%u|r|cff00ff00, command: |r|cff00ffff%u|r|cff00ff00, datalong: |r|cff00ffff%u|r|cff00ff00, datalong2: |r|cff00ffff%u|r|cff00ff00, datatext: |r|cff00ffff%s|r|cff00ff00, posx: |r|cff00ffff%f|r|cff00ff00, posy: |r|cff00ffff%f|r|cff00ff00, posz: |r|cff00ffff%f|r|cff00ff00, orientation: |r|cff00ffff%f|r", id, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
        } while(result->NextRow());
    }

    if(show == "del")
    {
        char* arg_id = strtok(nullptr, " ");
        uint32 id = atoi(arg_id);

        QueryResult result = WorldDatabase.PQuery( "SELECT `guid` FROM waypoint_scripts WHERE guid = %u", id);

        if( result )
        {

           WorldDatabase.PExecute("DELETE FROM waypoint_scripts WHERE guid = %u", id);
           PSendSysMessage("%s%s%u|r","|cff00ff00","Wp Event: waypoint script removed: ", id);
        }
        else
            PSendSysMessage("|cffff33ffWp Event: ERROR: you have selected a non existing script: %u|r", id);

        return true;
    }


    if(show == "mod")
    {
        char* arg_1 = strtok(nullptr," ");

        if(!arg_1)
        {
            SendSysMessage("|cffff33ffERROR: waypoint script guid not present.|r");
            return true;
        }

        uint32 id = atoi(arg_1);

        if(!id)
        {
            SendSysMessage("|cffff33ffERROR: no valid waypoint script id not present.|r");
            return true;
        }

        char* arg_2 = strtok(nullptr," ");

        if(!arg_2)
        {   
            SendSysMessage("|cffff33ffERROR: no argument present.|r");
            return true;
        }

        std::string arg_string  = arg_2;

        if( (arg_string != "setid") && (arg_string != "delay") && (arg_string != "command")
        && (arg_string != "datalong") && (arg_string != "datalong2") && (arg_string != "dataint") && (arg_string != "posx")
        && (arg_string != "posy") && (arg_string != "posz") && (arg_string != "orientation") )
        { 
            SendSysMessage("|cffff33ffERROR: no valid argument present.|r");
            return true;
        }

        char* arg_3;
        std::string arg_str_2 = arg_2;
        arg_3 = strtok(nullptr," ");

        if(!arg_3)
        {
            SendSysMessage("|cffff33ffERROR: no additional argument present.|r");
            return true;
        }

        float coord;

        if(arg_str_2 == "setid")
        {
            uint32 newid = atoi(arg_3);
            PSendSysMessage("%s%s|r|cff00ffff%u|r|cff00ff00%s|r|cff00ffff%u|r","|cff00ff00","Wp Event: waypoint script guid: ", newid," id changed: ", id);
            WorldDatabase.PExecute("UPDATE waypoint_scripts SET id='%u' WHERE guid='%u'",newid, id); 
            return true;
        }
        else
        {
            QueryResult result = WorldDatabase.PQuery("SELECT id FROM waypoint_scripts WHERE guid='%u'",id);

            if(!result)
            {
                SendSysMessage("|cffff33ffERROR: you have selected a nonexistent waypoint script guid.|r");
                return true;
            }

            if(arg_str_2 == "posx")
            {
                coord = atof(arg_3);
                WorldDatabase.PExecute("UPDATE waypoint_scripts SET x='%f' WHERE guid='%u'",
                    coord, id);
                PSendSysMessage("|cff00ff00Waypoint script:|r|cff00ffff %u|r|cff00ff00 position_x updated.|r", id);
                return true;
            }else if(arg_str_2 == "posy")
            {
                coord = atof(arg_3);
                WorldDatabase.PExecute("UPDATE waypoint_scripts SET y='%f' WHERE guid='%u'",
                    coord, id);
                PSendSysMessage("|cff00ff00Waypoint script: %u position_y updated.|r", id);
                return true;
            } else if(arg_str_2 == "posz")
            {
                coord = atof(arg_3);
                WorldDatabase.PExecute("UPDATE waypoint_scripts SET z='%f' WHERE guid='%u'",
                    coord, id);
                PSendSysMessage("|cff00ff00Waypoint script: |r|cff00ffff%u|r|cff00ff00 position_z updated.|r", id);
                return true;
            } else if(arg_str_2 == "orientation")
            {
                coord = atof(arg_3);
                WorldDatabase.PExecute("UPDATE waypoint_scripts SET o='%f' WHERE guid='%u'",
                    coord, id);
                PSendSysMessage("|cff00ff00Waypoint script: |r|cff00ffff%u|r|cff00ff00 orientation updated.|r", id);
                return true;
            } else if(arg_str_2 == "dataint")
            {
                    WorldDatabase.PExecute("UPDATE waypoint_scripts SET %s='%u' WHERE guid='%u'",
                    arg_2, atoi(arg_3), id);
                    PSendSysMessage("|cff00ff00Waypoint script: |r|cff00ffff%u|r|cff00ff00 dataint updated.|r", id);
                    return true;
            }else
            {
                    std::string arg_str_3 = arg_3;
                    WorldDatabase.EscapeString(arg_str_3);
                    WorldDatabase.PExecute("UPDATE waypoint_scripts SET %s='%s' WHERE guid='%u'",
                    arg_2, arg_str_3.c_str(), id);
            }
        }
        PSendSysMessage("%s%s|r|cff00ffff%u:|r|cff00ff00 %s %s|r","|cff00ff00","Waypoint script:", id, arg_2,"updated.");
    }
    return true;
}

bool ChatHandler::HandleWpModifyCommand(const char* args)
{
    ARGS_CHECK

    // first arg: add del text emote spell waittime move
    char* show_str = strtok((char*)args, " ");
    if (!show_str)
    {
        return false;
    }

    std::string show = show_str;
    // Check
    // Remember: "show" must also be the name of a column!
    if( (show != "delay") && (show != "action") && (show != "action_chance")
        && (show != "move_type") && (show != "del") && (show != "move") && (show != "wpadd")
       )
    {
        return false;
    }

    // Next arg is: <PATHID> <WPNUM> <ARGUMENT>
    char* arg_str = nullptr;

    // Did user provide a GUID
    // or did the user select a creature?
    // -> variable lowguid is filled with the GUID of the NPC
    uint32 pathid = 0;
    uint32 point = 0;
    uint32 wpGuid = 0;
    Creature* target = GetSelectedCreature();

    if(!target || target->GetEntry() != VISUAL_WAYPOINT)
    {
        SendSysMessage("|cffff33ffERROR: you must select a waypoint.|r");
        return false;
    }

    // The visual waypoint
    Creature* wpCreature = nullptr;
    wpGuid = target->GetGUIDLow();

    // Did the user select a visual spawnpoint?
    if(wpGuid)
        wpCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_NEW_GUID(wpGuid, VISUAL_WAYPOINT, HIGHGUID_UNIT));
    // attempt check creature existence by DB data
    else
    {
        PSendSysMessage(LANG_WAYPOINT_CREATNOTFOUND, wpGuid);
        return false;
    }
    // User did select a visual waypoint?
    // Check the creature
    if (wpCreature->GetEntry() == VISUAL_WAYPOINT )
    {
        QueryResult result =
        WorldDatabase.PQuery( "SELECT id, point FROM waypoint_data WHERE wpguid = %u", wpGuid);

        if(!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUNDSEARCH, target->GetGUIDLow());
            // Select waypoint number from database
            // Since we compare float values, we have to deal with
            // some difficulties.
            // Here we search for all waypoints that only differ in one from 1 thousand
            // (0.001) - There is no other way to compare C++ floats with mySQL floats
            // See also: http://dev.mysql.com/doc/refman/5.0/en/problems-with-float.html
            const char* maxDIFF = "0.01";
            result = WorldDatabase.PQuery( "SELECT id, point FROM waypoint_data WHERE (abs(position_x - %f) <= %s ) and (abs(position_y - %f) <= %s ) and (abs(position_z - %f) <= %s )",
            wpCreature->GetPositionX(), maxDIFF, wpCreature->GetPositionY(), maxDIFF, wpCreature->GetPositionZ(), maxDIFF);
            if(!result)
            {
                    PSendSysMessage(LANG_WAYPOINT_NOTFOUNDDBPROBLEM, wpGuid);
                    return true;
            }
        }

        do
        {
            Field *fields = result->Fetch();
            pathid = fields[0].GetUInt32();
            point  = fields[1].GetUInt32();
        }
        while( result->NextRow() );

        // We have the waypoint number and the GUID of the "master npc"
        // Text is enclosed in "<>", all other arguments not
        arg_str = strtok((char*)nullptr, " ");
    }

    // Check for argument
    if(show != "del" && show != "move" && arg_str == nullptr)
    {
        PSendSysMessage(LANG_WAYPOINT_ARGUMENTREQ, show_str);
        return false;
    }

    if(show == "del" && target)
    {
        PSendSysMessage("|cff00ff00DEBUG: wp modify del, PathID: |r|cff00ffff%u|r", pathid);

        // wpCreature
        Creature* wpCreature = nullptr;

        if( wpGuid != 0 )
        {
            wpCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_NEW_GUID(wpGuid, VISUAL_WAYPOINT, HIGHGUID_UNIT));
            wpCreature->CombatStop();
            wpCreature->DeleteFromDB();
            wpCreature->AddObjectToRemoveList();
        }

        SQLTransaction trans = WorldDatabase.BeginTransaction();
        trans->PAppend("DELETE FROM waypoint_data WHERE id='%u' AND point='%u'",
            pathid, point);
        trans->PAppend("UPDATE waypoint_data SET point=point-1 WHERE id='%u' AND point>'%u'",
            pathid, point);
        WorldDatabase.CommitTransaction(trans);

        PSendSysMessage(LANG_WAYPOINT_REMOVED);
        return true;
    }                                                       // del

    if(show == "move" && target)
    {
        PSendSysMessage("|cff00ff00DEBUG: wp move, PathID: |r|cff00ffff%u|r", pathid);

        Player *chr = m_session->GetPlayer();
        Map *map = chr->GetMap();
        {
            // wpCreature
            Creature* wpCreature = nullptr;
            // What to do:
            // Move the visual spawnpoint
            // Respawn the owner of the waypoints
            if( wpGuid != 0 )
            {
                wpCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_NEW_GUID(wpGuid, VISUAL_WAYPOINT, HIGHGUID_UNIT));
                wpCreature->CombatStop();
                wpCreature->DeleteFromDB();
                wpCreature->AddObjectToRemoveList();
                // re-create
                auto  wpCreature2 = new Creature;
                if (!wpCreature2->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_UNIT, true), map, VISUAL_WAYPOINT))
                {
                    PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, VISUAL_WAYPOINT);
                    delete wpCreature2;
                    return false;
                }
                wpCreature2->Relocate(chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ(), chr->GetOrientation());

                if(!wpCreature2->IsPositionValid())
                {
                    TC_LOG_ERROR("command","ERROR: creature (guidlow %d, entry %d) not created. Suggested coordinates aren't valid (X: %f Y: %f)",wpCreature2->GetGUIDLow(),wpCreature2->GetEntry(),wpCreature2->GetPositionX(),wpCreature2->GetPositionY());
                    delete wpCreature2;
                    return false;
                }

                wpCreature2->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()));
                // To call _LoadGoods(); _LoadQuests(); CreateTrainerSpells();
                wpCreature2->LoadFromDB(wpCreature2->GetDBTableGUIDLow(), map);
                map->Add(wpCreature2);
                //sMapMgr->CreateMap(npcCreature->GetMapId())->Add(wpCreature2);
            }

            WorldDatabase.PExecute("UPDATE waypoint_data SET position_x = '%f',position_y = '%f',position_z = '%f' where id = '%u' AND point='%u'",
                chr->GetPositionX(), chr->GetPositionY(), chr->GetPositionZ(), pathid, point );

            PSendSysMessage(LANG_WAYPOINT_CHANGED);
        }
        return true;
    }                                                       // move


    const char *text = arg_str;

    if( text == nullptr )
    {
        // show_str check for present in list of correct values, no sql injection possible
        WorldDatabase.PExecute("UPDATE waypoint_data SET %s=NULL WHERE id='%u' AND point='%u'",
            show_str, pathid, point);
    }
    else
    {
        // show_str check for present in list of correct values, no sql injection possible
        std::string text2 = text;
        WorldDatabase.EscapeString(text2);
        WorldDatabase.PExecute("UPDATE waypoint_data SET %s='%s' WHERE id='%u' AND point='%u'",
            show_str, text2.c_str(), pathid, point);
    }

    PSendSysMessage(LANG_WAYPOINT_CHANGED_NO, show_str);

    return true;
}


bool ChatHandler::HandleWpShowCommand(const char* args)
{
    ARGS_CHECK

    // first arg: on, off, first, last
    char* show_str = strtok((char*)args, " ");
    if (!show_str)
    {
        return false;
    }
    // second arg: GUID (optional, if a creature is selected)
    char* guid_str = strtok((char*)nullptr, " ");

    uint32 pathid = 0;
    Creature* target = GetSelectedCreature();

    // Did player provide a PathID?

    if (!guid_str)
    {
        // No PathID provided
        // -> Player must have selected a creature
        if(!target)
        {
            SendSysMessage(LANG_SELECT_CREATURE);
            SetSentErrorMessage(true);
            return false;
        }

        pathid = target->GetWaypointPathId();
    }

    else
    {
        // PathID provided
        // Warn if player also selected a creature
        // -> Creature selection is ignored <-
        if(target)
        {
            SendSysMessage(LANG_WAYPOINT_CREATSELECTED);
        }

        pathid = atoi((char*)guid_str);
    }

    std::string show = show_str;
    uint32 Maxpoint;

    //PSendSysMessage("wpshow - show: %s", show);

    // Show info for the selected waypoint
    if (show == "info") {
        // Check if the user did specify a visual waypoint
        if (target->GetEntry() != VISUAL_WAYPOINT) {
            PSendSysMessage(LANG_WAYPOINT_VP_SELECT);
            SetSentErrorMessage(true);
            return false;
        }
        QueryResult result = WorldDatabase.PQuery("SELECT id, point, delay, move_type, action, action_chance FROM waypoint_data WHERE wpguid = %u", target->GetGUIDLow());

        if (!result) {
            SendSysMessage(LANG_WAYPOINT_NOTFOUNDDBPROBLEM);
            return true;
        }

        SendSysMessage("|cff00ffffDEBUG: wp show info:|r");

        do {
            Field *fields = result->Fetch();
            pathid = fields[0].GetUInt32();
            uint32 point = fields[1].GetUInt32();
            uint32 delay = fields[2].GetUInt32();
            uint32 flag = fields[3].GetUInt32();
            uint32 ev_id = fields[4].GetUInt32();
            uint32 ev_chance = fields[5].GetUInt32();

            PSendSysMessage("|cff00ff00Show info: for current point: |r|cff00ffff%u|r|cff00ff00, Path ID: |r|cff00ffff%u|r", point, pathid);
            PSendSysMessage("|cff00ff00Show info: delay: |r|cff00ffff%u|r", delay);
            PSendSysMessage("|cff00ff00Show info: Move flag: |r|cff00ffff%u|r", flag);
            PSendSysMessage("|cff00ff00Show info: Waypoint event: |r|cff00ffff%u|r", ev_id);
            PSendSysMessage("|cff00ff00Show info: Event chance: |r|cff00ffff%u|r", ev_chance);
        } while (result->NextRow());

        // Cleanup memory
        return true;
    }

    if(show == "on")
    {
        QueryResult result = WorldDatabase.PQuery( "SELECT point, position_x,position_y,position_z FROM waypoint_data WHERE id = '%u'", pathid);

        if(!result)
        {
            SendSysMessage("|cffff33ffPath no found.|r");
            SetSentErrorMessage(true);
            return false;
        }

        PSendSysMessage("|cff00ff00DEBUG: wp on, PathID: |cff00ffff%u|r", pathid);

        // Delete all visuals for this NPC
        QueryResult result2 = WorldDatabase.PQuery( "SELECT wpguid FROM waypoint_data WHERE id = '%u' and wpguid <> 0", pathid);

        if(result2)
        {
            bool hasError = false;
            do
            {
                Field *fields = result2->Fetch();
                uint32 wpguid = fields[0].GetUInt32();
                Creature* pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_NEW_GUID(wpguid,VISUAL_WAYPOINT,HIGHGUID_UNIT));

                if(!pCreature)
                {
                    PSendSysMessage(LANG_WAYPOINT_NOTREMOVED, wpguid);
                    hasError = true;
                    WorldDatabase.PExecute("DELETE FROM creature WHERE guid = '%u'", wpguid);
                }
                else
                {
                    pCreature->CombatStop();
                    pCreature->DeleteFromDB();
                    pCreature->AddObjectToRemoveList();
                }

            }while( result2->NextRow() );

            if( hasError )
            {
                PSendSysMessage(LANG_WAYPOINT_TOOFAR1);
                PSendSysMessage(LANG_WAYPOINT_TOOFAR2);
                PSendSysMessage(LANG_WAYPOINT_TOOFAR3);
            }
        }

        do
        {
            Field *fields = result->Fetch();
           // uint32 point    = fields[0].GetUInt32();
            float x         = fields[1].GetFloat();
            float y         = fields[2].GetFloat();
            float z         = fields[3].GetFloat();

            uint32 id = VISUAL_WAYPOINT;

            Player *chr = m_session->GetPlayer();
            /*Map *map = chr->GetMap();
            float o = chr->GetOrientation();

            Creature* wpCreature = new Creature;
            if (!wpCreature->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_UNIT, true), map, id, 0))
            {
                PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, id);
                delete wpCreature;
                return false;
            }

            wpCreature->Relocate(x, y, z, o);

            if(!wpCreature->IsPositionValid())
            {
                TC_LOG_ERROR("command","ERROR: Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",wpCreature->GetGUIDLow(),wpCreature->GetEntry(),wpCreature->GetPositionX(),wpCreature->GetPositionY());
                delete wpCreature;
                return false;
            }

            // set "wpguid" column to the visual waypoint
            WorldDatabase.PExecute("UPDATE waypoint_data SET wpguid = '%u' WHERE id = '%u' and point = '%u'", wpCreature->GetGUIDLow(), pathid, point);

            wpCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()));
            wpCreature->LoadFromDB(wpCreature->GetDBTableGUIDLow(),map);
            map->Add(wpCreature);
            */
            chr->SummonCreature(id,x,y,z,0,TEMPSUMMON_CORPSE_TIMED_DESPAWN,60000);
        }
        while( result->NextRow() );

        SendSysMessage("|cff00ff00Showing the current creature's path.|r");

        return true;
    }

    if(show == "first")
    {
        PSendSysMessage("|cff00ff00DEBUG: wp first, GUID: %u|r", pathid);

        QueryResult result = WorldDatabase.PQuery( "SELECT position_x,position_y,position_z FROM waypoint_data WHERE point='1' AND id = '%u'",pathid);
        if(!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUND, pathid);
            SetSentErrorMessage(true);
            return false;
        }

        Field *fields = result->Fetch();
        float x         = fields[0].GetFloat();
        float y         = fields[1].GetFloat();
        float z         = fields[2].GetFloat();
        uint32 id = VISUAL_WAYPOINT;

        Player *chr = m_session->GetPlayer();
        //float o = chr->GetOrientation();
        /*Map *map = chr->GetMap();
        
        Creature* pCreature = new Creature;
        if (!pCreature->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_UNIT,true),map, id, 0))
        {
            PSendSysMessage(LANG_WAYPOINT_VP_NOTCREATED, id);
            delete pCreature;
            return false;
        }

        pCreature->Relocate(x, y, z, o);

        if(!pCreature->IsPositionValid())
        {
            TC_LOG_ERROR("command","ERROR: Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",pCreature->GetGUIDLow(),pCreature->GetEntry(),pCreature->GetPositionX(),pCreature->GetPositionY());
            delete pCreature;
            return false;
        }

        pCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()));
        pCreature->LoadFromDB(pCreature->GetDBTableGUIDLow(), map);
        map->Add(pCreature);

        if(target)
        {
            pCreature->SetDisplayId(target->GetDisplayId());
            pCreature->SetFloatValue(OBJECT_FIELD_SCALE_X, 0.5);
        }
        */
        chr->SummonCreature(id,x,y,z,0,TEMPSUMMON_CORPSE_DESPAWN,10);
        return true;
    }

    if(show == "last")
    {
        PSendSysMessage("|cff00ff00DEBUG: wp last, PathID: |r|cff00ffff%u|r", pathid);

        QueryResult result = WorldDatabase.PQuery( "SELECT MAX(point) FROM waypoint_data WHERE id = '%u'",pathid);
        if( result )
        {
            Maxpoint = (*result)[0].GetUInt32();
        }
        else
            Maxpoint = 0;

        result = WorldDatabase.PQuery( "SELECT position_x,position_y,position_z FROM waypoint_data WHERE point ='%u' AND id = '%u'",Maxpoint, pathid);
        if(!result)
        {
            PSendSysMessage(LANG_WAYPOINT_NOTFOUNDLAST, pathid);
            SetSentErrorMessage(true);
            return false;
        }
        Field *fields = result->Fetch();
        float x         = fields[0].GetFloat();
        float y         = fields[1].GetFloat();
        float z         = fields[2].GetFloat();
        uint32 id = VISUAL_WAYPOINT;

        Player *chr = m_session->GetPlayer();
        //float o = chr->GetOrientation();
        /*Map *map = chr->GetMap();
        
        Creature* pCreature = new Creature;
        if (!pCreature->Create(sObjectMgr->GenerateLowGuid(HIGHGUID_UNIT,true), map, id, 0))
        {
            PSendSysMessage(LANG_WAYPOINT_NOTCREATED, id);
            delete pCreature;
            return false;
        }

        pCreature->Relocate(x, y, z, o);

        if(!pCreature->IsPositionValid())
        {
            TC_LOG_ERROR("command","ERROR: Creature (guidlow %d, entry %d) not created. Suggested coordinates isn't valid (X: %f Y: %f)",pCreature->GetGUIDLow(),pCreature->GetEntry(),pCreature->GetPositionX(),pCreature->GetPositionY());
            delete pCreature;
            return false;
        }

        pCreature->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()));
        pCreature->LoadFromDB(pCreature->GetDBTableGUIDLow(), map);
        map->Add(pCreature);

        if(target)
        {
            pCreature->SetDisplayId(target->GetDisplayId());
            pCreature->SetFloatValue(OBJECT_FIELD_SCALE_X, 0.5);
        }
        */
        chr->SummonCreature(id,x,y,z,0,TEMPSUMMON_CORPSE_DESPAWN,10);
        return true;
    }

    if(show == "off")
    {
        QueryResult result = WorldDatabase.PQuery("SELECT guid FROM creature WHERE id = '%u'", 1);
        if(!result)
        {
            SendSysMessage(LANG_WAYPOINT_VP_NOTFOUND);
            SetSentErrorMessage(true);
            return false;
        }
        bool hasError = false;
        do
        {
            Field *fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            Creature* pCreature = ObjectAccessor::GetCreature(*m_session->GetPlayer(),MAKE_NEW_GUID(guid,VISUAL_WAYPOINT,HIGHGUID_UNIT));

            if(!pCreature)
            {
                PSendSysMessage(LANG_WAYPOINT_NOTREMOVED, guid);
                hasError = true;
                WorldDatabase.PExecute("DELETE FROM creature WHERE guid = '%u'", guid);
            }
            else
            {
                pCreature->CombatStop();
                pCreature->DeleteFromDB();
                pCreature->AddObjectToRemoveList();
            }
        }while(result->NextRow());
        // set "wpguid" column to "empty" - no visual waypoint spawned
        WorldDatabase.PExecute("UPDATE waypoint_data SET wpguid = '0'");
        //WorldDatabase.PExecute("UPDATE creature_movement SET wpguid = '0' WHERE wpguid <> '0'");

        if( hasError )
        {
            PSendSysMessage(LANG_WAYPOINT_TOOFAR1);
            PSendSysMessage(LANG_WAYPOINT_TOOFAR2);
            PSendSysMessage(LANG_WAYPOINT_TOOFAR3);
        }

        SendSysMessage(LANG_WAYPOINT_VP_ALLREMOVED);

        return true;
    }

    PSendSysMessage("|cffff33ffDEBUG: wpshow - no valid command found|r");

    return true;
}

//////////// WAYPOINT COMMANDS //

//rename characters
bool ChatHandler::HandleRenameCommand(const char* args)
{
    Player* target = nullptr;
    uint64 targetGUID = 0;
    std::string oldname;

    char* px = strtok((char*)args, " ");

    if(px)
    {
        oldname = px;

        if(!normalizePlayerName(oldname))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        target = sObjectAccessor->FindConnectedPlayerByName(oldname.c_str());

        if (!target)
            targetGUID = sCharacterCache->GetCharacterGuidByName(oldname);
    }

    if(!target && !targetGUID)
    {
        target = GetSelectedPlayer();
    }

    if(!target && !targetGUID)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    if(target)
    {
        PSendSysMessage(LANG_RENAME_PLAYER, target->GetName().c_str());
        target->SetAtLoginFlag(AT_LOGIN_RENAME);
    }
    else
    {
        PSendSysMessage(LANG_RENAME_PLAYER_GUID, oldname.c_str(), GUID_LOPART(targetGUID));
        CharacterDatabase.PExecute("UPDATE characters SET at_login = at_login | '1' WHERE guid = '%u'", GUID_LOPART(targetGUID));
    }

    return true;
}

/* Syntax : .arenarename <playername> <type> <newname> */
bool ChatHandler::HandleRenameArenaTeamCommand(const char* args)
{
    char* playerName = strtok((char*)args, " ");
    char* cType = strtok(nullptr, " ");
    char* newName = strtok(nullptr, "");
    if(!playerName || !cType || !newName)
        return false;

    uint8 type = atoi(cType);

    uint8 dataIndex = 0;
    switch (type)
    {
        case 2: dataIndex = 0; break;
        case 3: dataIndex = 1; break;
        case 5: dataIndex = 2; break;
        default:
            SendSysMessage("Invalid team type (must be 2, 3 or 5).");
            return true;
    }

    uint64 targetGUID = 0;
    std::string stringName = playerName;

    targetGUID = sCharacterCache->GetCharacterGuidByName(stringName);
    if(!targetGUID)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        return true;
    }

    CharacterCacheEntry const* playerData = sCharacterCache->GetCharacterCacheByGuid(GUID_LOPART(targetGUID));
    if (!playerData)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        return true;
    }

    uint32 arenateamid = playerData->arenaTeamId[dataIndex];
    if(!arenateamid)
    {
        PSendSysMessage("Team not found. Also double check your team type.");
        return true;
    }

    CharacterDatabase.PQuery("UPDATE arena_team SET name = '%s' WHERE arenateamid = '%u'", newName, arenateamid);
    // + Update within memory ?

    PSendSysMessage("Team (id %u) name changed to \"%s\"", arenateamid, newName);

    ArenaTeam* team = sObjectMgr->GetArenaTeamById(arenateamid);
    if (team)
    {
        team->SetName(newName);
    }
    else {
        PSendSysMessage("Could not change team name in current memory, the change will be effective only at next server start");
    }

    return true;
}

//spawn go
bool ChatHandler::HandleGameObjectCommand(const char* args)
{
    ARGS_CHECK

    char* pParam1 = strtok((char*)args, " ");
    if (!pParam1)
        return false;

    uint32 id = atoi((char*)pParam1);
    if(!id)
        return false;

    char* spawntimeSecs = strtok(nullptr, " ");

    const GameObjectTemplate *goI = sObjectMgr->GetGameObjectTemplate(id);

    if (!goI)
    {
        PSendSysMessage(LANG_GAMEOBJECT_NOT_EXIST,id);
        SetSentErrorMessage(true);
        return false;
    }

    Player *chr = m_session->GetPlayer();
    float x = float(chr->GetPositionX());
    float y = float(chr->GetPositionY());
    float z = float(chr->GetPositionZ());
    float o = float(chr->GetOrientation());
    Map *map = chr->GetMap();

    float rot2 = sin(o/2);
    float rot3 = cos(o/2);
    
    /* Gobjects on transports have been added on LK (or did not found correct packet structure */
    if (Transport* trans = chr->GetTransport())
    {
#ifdef LICH_KING
        uint32 guid = sObjectMgr->GenerateLowGuid(HIGHGUID_GAMEOBJECT);
        GameObjectData& data = sObjectMgr->NewGOData(guid);
        data.id = id;
        data.posX = chr->GetTransOffsetX();
        data.posY = chr->GetTransOffsetY();
        data.posZ = chr->GetTransOffsetZ();
        data.orientation = chr->GetTransOffsetO();
        data.rotation = G3D::Quat(0, 0, rot2, rot3);
        data.spawntimesecs = 30;
        data.animprogress = 0;
        data.go_state = 1;
        data.spawnMask = 1;
        data.ArtKit = 0;

        if(!trans->GetGOInfo())
        {
            SendSysMessage("Error : Cannot save gameobject on transport because trans->GetGOInfo() == NULL");
            return true;
        }
        if(GameObject* gob = trans->CreateGOPassenger(guid, &data))
        {
            gob->SaveToDB(trans->GetGOInfo()->moTransport.mapID, 1 << map->GetSpawnMode());
            map->Add(gob);
            sTransportMgr->CreatePassengerForTransportInDB(PASSENGER_GAMEOBJECT,guid,trans->GetEntry());
            sObjectMgr->AddGameobjectToGrid(guid, &data);
        } else {
            SendSysMessage("Error : Cannot create gameobject passenger.");
        }
        return true;
#else
        SendSysMessage("Error : Cannot create gameobject passenger.");
#endif
    }

    auto  pGameObj = new GameObject;
    uint32 db_lowGUID = sObjectMgr->GenerateLowGuid(HIGHGUID_GAMEOBJECT);

    if(!pGameObj->Create(db_lowGUID, goI->entry, map, Position(x, y, z, o), G3D::Quat(0, 0, rot2, rot3), 0, GO_STATE_READY))
    {
        delete pGameObj;
        return false;
    }

    if( spawntimeSecs )
    {
        uint32 value = atoi((char*)spawntimeSecs);
        pGameObj->SetRespawnTime(value);
        //TC_LOG_DEBUG("command","*** spawntimeSecs: %d", value);
    }
     
    // fill the gameobject data and save to the db
    pGameObj->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()));

    delete pGameObj;
    pGameObj = sObjectMgr->IsGameObjectStaticTransport(goI->entry) ? new StaticTransport() : new GameObject();

    // this will generate a new guid if the object is in an instance
    if(!pGameObj->LoadFromDB(db_lowGUID, map))
    {
        delete pGameObj;
        return false;
    }

    map->Add(pGameObj);

    // TODO: is it really necessary to add both the real and DB table guid here ?
    sObjectMgr->AddGameobjectToGrid(db_lowGUID, sObjectMgr->GetGOData(db_lowGUID));

    PSendSysMessage(LANG_GAMEOBJECT_ADD,id,goI->name.c_str(),db_lowGUID,x,y,z);
    return true;
}

//show animation
bool ChatHandler::HandleAnimCommand(const char* args)
{
    ARGS_CHECK

    uint32 anim_id = atoi((char*)args);
    m_session->GetPlayer()->HandleEmoteCommand(anim_id);
    return true;
}

bool ChatHandler::HandleAddHonorCommand(const char* args)
{
    ARGS_CHECK

    Player *target = GetSelectedPlayerOrSelf();
    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 amount = (uint32)atoi(args);
    target->RewardHonor(nullptr, 1, amount);
    return true;
}

bool ChatHandler::HandleHonorAddKillCommand(const char* /*args*/)
{
    

    Unit *target = GetSelectedUnit();
    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    m_session->GetPlayer()->RewardHonor(target, 1);
    return true;
}

bool ChatHandler::HandleUpdateHonorFieldsCommand(const char* /*args*/)
{
    Player *target = GetSelectedPlayerOrSelf();
    if(!target)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    target->UpdateHonorFields();
    return true;
}

bool ChatHandler::HandleLookupEventCommand(const char* args)
{
    ARGS_CHECK

    std::string namepart = args;
    std::wstring wnamepart;

    // converting string that we try to find to lower case
    if(!Utf8toWStr(namepart,wnamepart))
        return false;

    wstrToLower(wnamepart);

    uint32 counter = 0;

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

    for(uint32 id = 0; id < events.size(); ++id )
    {
        GameEventData const& eventData = events[id];

        std::string descr = eventData.description;
        if(descr.empty())
            continue;

        if (Utf8FitTo(descr, wnamepart))
        {
            char const* active = activeEvents.find(id) != activeEvents.end() ? GetTrinityString(LANG_ACTIVE) : "";

            if(m_session)
                PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT,id,id,eventData.description.c_str(),active );
            else
                PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE,id,eventData.description.c_str(),active );

            ++counter;
        }
    }

    if (counter==0)
        SendSysMessage(LANG_NOEVENTFOUND);

    return true;
}

bool ChatHandler::HandleEventActiveListCommand(const char* args)
{
    uint32 counter = 0;

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

    char const* active = GetTrinityString(LANG_ACTIVE);

    for(unsigned short event_id : activeEvents)
    {
        GameEventData const& eventData = events[event_id];

        if(m_session)
            PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT,event_id,event_id,eventData.description.c_str(),active );
        else
            PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE,event_id,eventData.description.c_str(),active );

        ++counter;
    }

    if (counter==0)
        SendSysMessage(LANG_NOEVENTFOUND);

    return true;
}

bool ChatHandler::HandleEventInfoCommand(const char* args)
{
    ARGS_CHECK

    // id or [name] Shift-click form |color|Hgameevent:id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameevent");
    if(!cId)
        return false;

    uint32 event_id = atoi(cId);

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

    if(event_id >=events.size())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventData const& eventData = events[event_id];
    if(!eventData.isValid())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();
    bool active = activeEvents.find(event_id) != activeEvents.end();
    char const* activeStr = active ? GetTrinityString(LANG_ACTIVE) : "";

    std::string startTimeStr = TimeToTimestampStr(eventData.start);
    std::string endTimeStr = TimeToTimestampStr(eventData.end);

    uint32 delay = sGameEventMgr->NextCheck(event_id);
    time_t nextTime = time(nullptr)+delay;
    std::string nextStr = nextTime >= eventData.start && nextTime < eventData.end ? TimeToTimestampStr(time(nullptr)+delay) : "-";

    std::string occurenceStr = secsToTimeString(eventData.occurence * MINUTE);
    std::string lengthStr = secsToTimeString(eventData.length * MINUTE);

    PSendSysMessage(LANG_EVENT_INFO,event_id,eventData.description.c_str(),activeStr,
        startTimeStr.c_str(),endTimeStr.c_str(),occurenceStr.c_str(),lengthStr.c_str(),
        nextStr.c_str());
    return true;
}

bool ChatHandler::HandleEventStartCommand(const char* args)
{
    ARGS_CHECK

    // id or [name] Shift-click form |color|Hgameevent:id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameevent");
    if(!cId)
        return false;

    int32 event_id = atoi(cId);

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

    if(event_id < 1 || event_id >=events.size())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventData const& eventData = events[event_id];
    if(!eventData.isValid())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    if(sGameEventMgr->IsActiveEvent(event_id))
    {
        PSendSysMessage(LANG_EVENT_ALREADY_ACTIVE,event_id);
        SetSentErrorMessage(true);
        return false;
    }

    sGameEventMgr->StartEvent(event_id,true);
    return true;
}

bool ChatHandler::HandleEventStopCommand(const char* args)
{
    ARGS_CHECK

    // id or [name] Shift-click form |color|Hgameevent:id|h[name]|h|r
    char* cId = extractKeyFromLink((char*)args,"Hgameevent");
    if(!cId)
        return false;

    int32 event_id = atoi(cId);

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

    if(event_id < 1 || event_id >=events.size())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventData const& eventData = events[event_id];
    if(!eventData.isValid())
    {
        SendSysMessage(LANG_EVENT_NOT_EXIST);
        SetSentErrorMessage(true);
        return false;
    }

    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

    if(activeEvents.find(event_id) == activeEvents.end())
    {
        PSendSysMessage(LANG_EVENT_NOT_ACTIVE,event_id);
        SetSentErrorMessage(true);
        return false;
    }

    sGameEventMgr->StopEvent(event_id,true);
    return true;
}

bool ChatHandler::HandleCombatStopCommand(const char* args)
{
    Player *player;

    if(*args)
    {
        std::string playername = args;

        if(!normalizePlayerName(playername))
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }

        player = sObjectAccessor->FindConnectedPlayerByName(playername.c_str());

        if(!player)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }
    }
    else
    {
        player = GetSelectedPlayerOrSelf();
    }

    player->CombatStop();
    player->GetHostileRefManager().deleteReferences();
    return true;
}

bool ChatHandler::HandleLearnAllCraftsCommand(const char* /*args*/)
{
    

    uint32 classmask = m_session->GetPlayer()->GetClassMask();

    for (uint32 i = 0; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const *skillInfo = sSkillLineStore.LookupEntry(i);
        if( !skillInfo )
            continue;

        if( skillInfo->categoryId == SKILL_CATEGORY_PROFESSION || skillInfo->categoryId == SKILL_CATEGORY_SECONDARY )
        {
            for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
            {
                SkillLineAbilityEntry const *skillLine = sSkillLineAbilityStore.LookupEntry(j);
                if( !skillLine )
                    continue;

                // skip racial skills
                if( skillLine->racemask != 0 )
                    continue;

                // skip wrong class skills
                if( skillLine->classmask && (skillLine->classmask & classmask) == 0)
                    continue;

                if( skillLine->skillId != i || skillLine->forward_spellid )
                    continue;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(skillLine->spellId);
                if(!spellInfo || !SpellMgr::IsSpellValid(spellInfo,m_session->GetPlayer(),false))
                    continue;

                m_session->GetPlayer()->LearnSpell(skillLine->spellId, false);
            }
        }
    }

    SendSysMessage(LANG_COMMAND_LEARN_ALL_CRAFT);
    return true;
}

bool ChatHandler::HandleLearnAllRecipesCommand(const char* args)
{
    ARGS_CHECK

    //  Learns all recipes of specified profession and sets skill to max
    //  Example: .learn all_recipes enchanting

    Player* target = GetSelectedPlayerOrSelf();
    if( !target )
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        return false;
    }

    std::wstring wnamepart;

    if(!Utf8toWStr(args,wnamepart))
        return false;

    // converting string that we try to find to lower case
    wstrToLower( wnamepart );

    uint32 classmask = m_session->GetPlayer()->GetClassMask();

    for (uint32 i = 0; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const *skillInfo = sSkillLineStore.LookupEntry(i);
        if( !skillInfo )
            continue;

        if( skillInfo->categoryId != SKILL_CATEGORY_PROFESSION &&
            skillInfo->categoryId != SKILL_CATEGORY_SECONDARY )
            continue;

        int loc = GetSessionDbcLocale();
        std::string name = skillInfo->name[loc];

        if(Utf8FitTo(name, wnamepart))
        {
            for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
            {
                SkillLineAbilityEntry const *skillLine = sSkillLineAbilityStore.LookupEntry(j);
                if( !skillLine )
                    continue;

                if( skillLine->skillId != i || skillLine->forward_spellid )
                    continue;

                // skip racial skills
                if( skillLine->racemask != 0 )
                    continue;

                // skip wrong class skills
                if( skillLine->classmask && (skillLine->classmask & classmask) == 0)
                    continue;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(skillLine->spellId);
                if(!spellInfo || !SpellMgr::IsSpellValid(spellInfo,m_session->GetPlayer(),false))
                    continue;

                if( !target->HasSpell(spellInfo->Id) )
                    m_session->GetPlayer()->LearnSpell(skillLine->spellId, false);
            }

            uint16 MaxLevel = target->GetPureMaxSkillValue(skillInfo->id);

            uint16 step = (MaxLevel-1) / 75;
            target->SetSkill(skillInfo->id, step, MaxLevel, MaxLevel);
            PSendSysMessage(LANG_COMMAND_LEARN_ALL_RECIPES, name.c_str());
            return true;
        }
    }

    return false;
}

bool ChatHandler::HandleLookupPlayerIpCommand(const char* args)
{
    ARGS_CHECK

    std::string ip = strtok ((char*)args, " ");
    char* limit_str = strtok (nullptr, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    LoginDatabase.EscapeString(ip);

    QueryResult result = LoginDatabase.PQuery ("SELECT id,username FROM account WHERE last_ip = '%s'", ip.c_str ());

    return LookupPlayerSearchCommand (result,limit);
}

bool ChatHandler::HandleLookupPlayerAccountCommand(const char* args)
{
    ARGS_CHECK

    std::string account = strtok ((char*)args, " ");
    char* limit_str = strtok (nullptr, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    if (!AccountMgr::normalizeString (account))
        return false;

    LoginDatabase.EscapeString(account);

    QueryResult result = LoginDatabase.PQuery ("SELECT id,username FROM account WHERE username = '%s'", account.c_str ());

    return LookupPlayerSearchCommand (result,limit);
}

bool ChatHandler::HandleLookupPlayerEmailCommand(const char* args)
{
    ARGS_CHECK

    std::string email = strtok ((char*)args, " ");
    char* limit_str = strtok (nullptr, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    LoginDatabase.EscapeString(email);

    QueryResult result = LoginDatabase.PQuery ("SELECT id,username FROM account WHERE email = '%s'", email.c_str ());

    return LookupPlayerSearchCommand (result,limit);
}

bool ChatHandler::LookupPlayerSearchCommand(QueryResult result, int32 limit)
{
    if(!result)
    {
        PSendSysMessage(LANG_NO_PLAYERS_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    int i =0;
    do
    {
        Field* fields = result->Fetch();
        uint32 acc_id = fields[0].GetUInt32();
        std::string acc_name = fields[1].GetString();

        QueryResult chars = CharacterDatabase.PQuery("SELECT guid,name FROM characters WHERE account = '%u'", acc_id);
        if(chars)
        {
            PSendSysMessage(LANG_LOOKUP_PLAYER_ACCOUNT,acc_name.c_str(),acc_id);

            uint64 guid = 0;
            std::string name;

            do
            {
                Field* charfields = chars->Fetch();
                guid = charfields[0].GetUInt64();
                name = charfields[1].GetString();

                PSendSysMessage(LANG_LOOKUP_PLAYER_CHARACTER,name.c_str(),guid);
                ++i;

            } while( chars->NextRow() && ( limit == -1 || i < limit ) );
        }
    } while(result->NextRow());

    return true;
}

/// Triggering corpses expire check in world
bool ChatHandler::HandleServerCorpsesCommand(const char* /*args*/)
{
    SendSysMessage("Erasing corpses...");
    CorpsesErase();
    return true;
}

bool ChatHandler::HandleRepairitemsCommand(const char* /*args*/)
{
    Player *target = GetSelectedPlayerOrSelf();

    if(!target)
    {
        PSendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // Repair items
    target->DurabilityRepairAll(false, 0, false);

    PSendSysMessage(LANG_YOU_REPAIR_ITEMS, target->GetName().c_str());
    if(needReportToTarget(target))
        ChatHandler(target).PSendSysMessage(LANG_YOUR_ITEMS_REPAIRED, GetName().c_str());
    return true;
}

bool ChatHandler::HandleNpcFollowCommand(const char* /*args*/)
{
    

    Player *player = m_session->GetPlayer();
    Creature *creature = GetSelectedCreature();

    if(!creature)
    {
        PSendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    // Follow player - Using pet's default dist and angle
    creature->GetMotionMaster()->MoveFollow(player, PET_FOLLOW_DIST, creature->GetFollowAngle());

    PSendSysMessage(LANG_CREATURE_FOLLOW_YOU_NOW, creature->GetName().c_str());
    return true;
}

bool ChatHandler::HandleNpcUnFollowCommand(const char* /*args*/)
{
    

    Player *player = m_session->GetPlayer();
    Creature *creature = GetSelectedCreature();

    if(!creature)
    {
        PSendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if (/*creature->GetMotionMaster()->empty() ||*/
        creature->GetMotionMaster()->GetCurrentMovementGeneratorType ()!=FOLLOW_MOTION_TYPE)
    {
        PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU, creature->GetName().c_str());
        SetSentErrorMessage(true);
        return false;
    }

    FollowMovementGenerator<Creature> const* mgen
        = static_cast<FollowMovementGenerator<Creature> const*>((creature->GetMotionMaster()->top()));

    if(mgen->GetTarget()!=player)
    {
        PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU, creature->GetName().c_str());
        SetSentErrorMessage(true);
        return false;
    }

    // reset movement
    creature->GetMotionMaster()->MovementExpired(true);

    PSendSysMessage(LANG_CREATURE_NOT_FOLLOW_YOU_NOW, creature->GetName().c_str());
    return true;
}

bool ChatHandler::HandleCreatePetCommand(const char* args)
{
    Player *player = m_session->GetPlayer();
    Creature *creatureTarget = GetSelectedCreature();

    if(!creatureTarget || creatureTarget->IsPet() || creatureTarget->GetTypeId() == TYPEID_PLAYER)
    {
        PSendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(creatureTarget->GetEntry());
    // Creatures with family 0 crashes the server
    if(cInfo->family == 0)
    {
        PSendSysMessage("This creature cannot be tamed. (family id: 0).");
        SetSentErrorMessage(true);
        return false;
    }

    if(player->GetMinionGUID())
    {
        PSendSysMessage("You already have a pet.");
        SetSentErrorMessage(true);
        return false;
    }

    // Everything looks OK, create new pet
    auto  pet = new Pet(HUNTER_PET);

    if(!pet)
      return false;
    
    if(!pet->CreateBaseAtCreature(creatureTarget))
    {
        delete pet;
        PSendSysMessage("Error 1");
        return false;
    }

    creatureTarget->SetDeathState(JUST_DIED);
    creatureTarget->RemoveCorpse();
    creatureTarget->SetHealth(0); // just for nice GM-mode view

    pet->SetUInt64Value(UNIT_FIELD_SUMMONEDBY, player->GetGUID());
    pet->SetCreatorGUID(player->GetGUID());
    pet->SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE, player->GetFaction());

    if(!pet->InitStatsForLevel(creatureTarget->GetLevel()))
    {
        TC_LOG_ERROR("command","ERROR: InitStatsForLevel() in EffectTameCreature failed! Pet deleted.");
        PSendSysMessage("Error 2");
        delete pet;
        return false;
    }

    // prepare visual effect for levelup
    pet->SetUInt32Value(UNIT_FIELD_LEVEL,creatureTarget->GetLevel()-1);

     pet->GetCharmInfo()->SetPetNumber(sObjectMgr->GeneratePetNumber(), true);
     // this enables pet details window (Shift+P)
     pet->AIM_Initialize();
     pet->InitPetCreateSpells();
     pet->SetHealth(pet->GetMaxHealth());

     Map* m = sMapMgr->CreateMap(pet->GetMapId(), pet);
     m->Add(pet->ToCreature());

     // visual effect for levelup
     pet->SetUInt32Value(UNIT_FIELD_LEVEL,creatureTarget->GetLevel());

     player->SetPet(pet);
     pet->SavePetToDB(PET_SAVE_AS_CURRENT);
     player->PetSpellInitialize();

    return true;
}

bool ChatHandler::HandlePetLearnCommand(const char* args)
{
    ARGS_CHECK

    Player *plr = m_session->GetPlayer();
    Pet *pet = plr->GetPet();

    if(!pet)
    {
        PSendSysMessage("You have no pet");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 spellId = extractSpellIdFromLink((char*)args);

    if(!spellId || !sSpellMgr->GetSpellInfo(spellId))
        return false;

    // Check if pet already has it
    if(pet->HasSpell(spellId))
    {
        PSendSysMessage("Pet already has spell: %u.", spellId);
        SetSentErrorMessage(true);
        return false;
    }

    // Check if spell is valid
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if(!spellInfo || !SpellMgr::IsSpellValid(spellInfo))
    {
        PSendSysMessage(LANG_COMMAND_SPELL_BROKEN,spellId);
        SetSentErrorMessage(true);
        return false;
    }

    pet->LearnSpell(spellId);

    PSendSysMessage("Pet has learned spell %u.", spellId);
    return true;
}

bool ChatHandler::HandlePetUnlearnCommand(const char *args)
{
    ARGS_CHECK

    Player *plr = m_session->GetPlayer();
    Pet *pet = plr->GetPet();

    if(!pet)
    {
        PSendSysMessage("You have no pet.");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 spellId = extractSpellIdFromLink((char*)args);

    if(pet->HasSpell(spellId))
        pet->RemoveSpell(spellId);
    else
        PSendSysMessage("Pet doesn't have that spell.");

    return true;
}

bool ChatHandler::HandlePetTpCommand(const char *args)
{
    ARGS_CHECK

    Player *plr = m_session->GetPlayer();
    Pet *pet = plr->GetPet();

    if(!pet)
    {
        PSendSysMessage("You have no pet.");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 tp = atol(args);

    pet->SetTP(tp);

    PSendSysMessage("Pet's tp changed to %u.", tp);
    return true;
}

bool ChatHandler::HandleActivateObjectCommand(const char *args)
{
    ARGS_CHECK

    char* cId = extractKeyFromLink((char*)args,"Hgameobject");
    if(!cId)
        return false;

    uint32 lowguid = atoi(cId);
    if(!lowguid)
        return false;

    GameObject* obj = nullptr;

    // by DB guid
    if (GameObjectData const* go_data = sObjectMgr->GetGOData(lowguid))
        obj = GetObjectGlobalyWithGuidOrNearWithDbGuid(lowguid,go_data->id);

    if(!obj)
    {
        PSendSysMessage(LANG_COMMAND_OBJNOTFOUND, lowguid);
        SetSentErrorMessage(true);
        return false;
    }

    // Activate
    obj->SetLootState(GO_READY);
    obj->UseDoorOrButton(10000);

    PSendSysMessage("Object activated!");

    return true;
}

// add creature, temp only
bool ChatHandler::HandleTempAddSpwCommand(const char* args)
{
    ARGS_CHECK
    char* charID = strtok((char*)args, " ");
    if (!charID)
        return false;

    Player *chr = m_session->GetPlayer();

    float x = chr->GetPositionX();
    float y = chr->GetPositionY();
    float z = chr->GetPositionZ();
    float ang = chr->GetOrientation();

    uint32 id = atoi(charID);

    chr->SummonCreature(id,x,y,z,ang, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1 * MINUTE * IN_MILLISECONDS);

    return true;
}

// add go, temp only
bool ChatHandler::HandleTempGameObjectCommand(const char* args)
{
    ARGS_CHECK
    char* charID = strtok((char*)args, " ");
    if (!charID)
        return false;

    Player *chr = m_session->GetPlayer();

    char* spawntime = strtok(nullptr, " ");
    uint32 spawntm = 0;

    if( spawntime )
        spawntm = atoi((char*)spawntime);

    float x = chr->GetPositionX();
    float y = chr->GetPositionY();
    float z = chr->GetPositionZ();
    float ang = chr->GetOrientation();

    float rot2 = sin(ang/2);
    float rot3 = cos(ang/2);

    uint32 id = atoi(charID);

    chr->SummonGameObject(id,Position(x,y,z,ang), G3D::Quat(0,0,rot2,rot3),spawntm);

    return true;
}

bool ChatHandler::HandleNpcAddFormationCommand(const char* args)
{
    ARGS_CHECK

    uint32 leaderGUID = (uint32) atoi((char*)args);

    Creature *pCreature = GetSelectedCreature();

    if(!pCreature || !pCreature->GetDBTableGUIDLow())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return true;
    }
    
    if(pCreature->GetFormation())
    {
        PSendSysMessage("Selected creature is already member of group %u.", pCreature->GetFormation()->GetId());
        return true;
    }

    uint32 lowguid = pCreature->GetDBTableGUIDLow();
    if (!lowguid)
    {
        PSendSysMessage("Creature may not be a summon", leaderGUID);
        return true;
    }

    CreatureData const* data = sObjectMgr->GetCreatureData(leaderGUID);
    if (!data)
    {
        PSendSysMessage("Could not find creature data for guid %u", leaderGUID);
        SetSentErrorMessage(true);
        return false;
    }

    Creature* leader;
    if (pCreature->GetMap()->Instanceable())
        leader = pCreature->GetMap()->GetCreatureWithTableGUID(leaderGUID);
    else
        leader = pCreature->GetMap()->GetCreature(MAKE_NEW_GUID(leaderGUID, data->id, HIGHGUID_UNIT));

    if (!leader)
    {
        PSendSysMessage("Could not find leader (guid %u) in map.", leaderGUID);
        return true;
    }

    if (!leader->GetFormation())
    {
        FormationInfo* group_member;
        group_member = new FormationInfo;
        group_member->leaderGUID = leaderGUID;
        sCreatureGroupMgr->AddGroupMember(leaderGUID, group_member);
        pCreature->SearchFormation();

        WorldDatabase.PExecute("REPLACE INTO `creature_formations` (`leaderGUID`, `memberGUID`, `dist`, `angle`, `groupAI`) VALUES ('%u', '%u', 0, 0, '%u')",
            leaderGUID, leaderGUID, uint32(group_member->groupAI));

        PSendSysMessage("Created formation with leader %u", leaderGUID);
    }

    Player *chr = m_session->GetPlayer();
    FormationInfo* group_member;

    group_member                  = new FormationInfo;
    group_member->follow_angle    = pCreature->GetAngle(chr) - chr->GetOrientation();
    group_member->follow_dist     = sqrtf(pow(chr->GetPositionX() - pCreature->GetPositionX(),int(2))+pow(chr->GetPositionY()-pCreature->GetPositionY(),int(2)));
    group_member->leaderGUID      = leaderGUID;

    sCreatureGroupMgr->AddGroupMember(lowguid, group_member);
    pCreature->SearchFormation();

    WorldDatabase.PExecute("REPLACE INTO `creature_formations` (`leaderGUID`, `memberGUID`, `dist`, `angle`, `groupAI`) VALUES ('%u', '%u','%f', '%f', '%u')",
        leaderGUID, lowguid, group_member->follow_dist, group_member->follow_angle, uint32(group_member->groupAI));

    PSendSysMessage("Creature %u added to formation with leader %u.", lowguid, leaderGUID);

    return true;
 }

bool ChatHandler::HandleNpcRemoveFormationCommand(const char* args)
{
    ARGS_CHECK

    Creature *pCreature = GetSelectedCreature();

    if(!pCreature || !pCreature->GetDBTableGUIDLow())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return true;
    }

    CreatureGroup* formation = pCreature->GetFormation();
    if(!formation)
    {
        PSendSysMessage("Selected creature (%u) is not in a formation.", pCreature->GetGUIDLow());
        return true;
    }

    formation->RemoveMember(pCreature);
    pCreature->SetFormation(nullptr);
    WorldDatabase.PExecute("DELETE ROM `creature_formations` WHERE memberGUID = %u",pCreature->GetGUIDLow());

    PSendSysMessage("Creature removed from formation.");

    return true;
}

bool ChatHandler::HandleNpcSetLinkCommand(const char* args)
{
    ARGS_CHECK

    uint32 linkguid = (uint32) atoi((char*)args);

    Creature* pCreature = GetSelectedCreature();

    if(!pCreature)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    if(!pCreature->GetDBTableGUIDLow())
    {
        PSendSysMessage("Selected creature [guidlow:%u] isn't in `creature` table", pCreature->GetGUIDLow());
        SetSentErrorMessage(true);
        return false;
    }

    if(!sObjectMgr->SetCreatureLinkedRespawn(pCreature->GetDBTableGUIDLow(), linkguid))
    {
        PSendSysMessage("Selected creature can't link with guid '%u'.", linkguid);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage("LinkGUID '%u' added to creature with DBTableGUID: '%u'.", linkguid, pCreature->GetDBTableGUIDLow());
    return true;
}

bool ChatHandler::HandleChanBan(const char* args)
{
    ARGS_CHECK
        
    std::string channelNamestr = "world";
    
    char* charname = strtok((char*)args, " ");
    if (!charname)
        return false;
        
    std::string charNamestr = charname;
    
    if (!normalizePlayerName(charNamestr)) {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }
    
    uint32 accountid = sCharacterCache->GetCharacterAccountIdByName(charNamestr.c_str());
    if (!accountid)
    {
        PSendSysMessage("No account found for player name: %s.", charNamestr.c_str());
        return true;
    }
    
    char* duration = strtok (nullptr," ");
    if (!duration || !atoi(duration))
        return false;

    char* reason = strtok (nullptr,"");
    std::string reasonstr;
    if (!reason)
    {
        SendSysMessage("You must specify a reason.");
        return false;
    }
    
    reasonstr = reason;
        
    LogsDatabase.EscapeString(reasonstr);
        
    uint32 durationSecs = TimeStringToSecs(duration);
    
    CharacterDatabase.PExecute("INSERT INTO channel_ban VALUES (%u, %lu, \"%s\", \"%s\")", accountid, time(nullptr)+durationSecs, channelNamestr.c_str(), reasonstr.c_str());
    LogsDatabaseAccessor::Sanction(m_session, accountid, 0, SANCTION_CHAN_BAN, durationSecs, reasonstr);
 
    PSendSysMessage("You banned %s from World channed with the reason: %s.", charNamestr.c_str(), reasonstr.c_str());

    Player *player = sObjectAccessor->FindConnectedPlayerByName(charNamestr.c_str());
    if (!player)
        return true;

    if (ChannelMgr* cMgr = channelMgr(player->GetTeam())) {
        if (Channel *chn = cMgr->GetChannel(channelNamestr.c_str(), player)) {
            chn->Kick(m_session ? m_session->GetPlayer()->GetGUID() : 0, player->GetName());
            chn->AddNewGMBan(accountid, time(nullptr)+durationSecs);
            //TODO translate
            //ChatHandler(player).PSendSysMessage("You have been banned from World channel with this reason: %s", reasonstr.c_str());
            ChatHandler(player).PSendSysMessage("Vous avez été banni du channel world avec la raison suivante : %s", reasonstr.c_str());
        }
    }
    
    return true;
}

bool ChatHandler::HandleChanUnban(const char* args)
{
    ARGS_CHECK
        
    std::string channelNamestr = "world";
    
    char* charname = strtok((char*)args, "");
    if (!charname)
        return false;
        
    std::string charNamestr = charname;
    
    if (!normalizePlayerName(charNamestr)) {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }
    
    uint32 accountid = sCharacterCache->GetCharacterAccountIdByName(charNamestr.c_str());
    if (!accountid)
    {
        PSendSysMessage("No account found for player %s.", charNamestr.c_str());
        SetSentErrorMessage(true);
        return false;
    }
        
    CharacterDatabase.PExecute("UPDATE channel_ban SET expire = %lu WHERE accountid = %u AND expire > %lu", time(nullptr), accountid, time(nullptr));
    
    if(m_session) 
    {
        if (ChannelMgr* cMgr = channelMgr(m_session->GetPlayer()->GetTeam())) {
            if (Channel *chn = cMgr->GetChannel(channelNamestr.c_str(), m_session->GetPlayer()))
                chn->RemoveGMBan(accountid);
        }
    }

    LogsDatabaseAccessor::RemoveSanction(m_session, accountid, 0, "", SANCTION_CHAN_BAN);
 
    PSendSysMessage("Player %s is unbanned.", charNamestr.c_str());
    if (Player *player = sObjectAccessor->FindConnectedPlayerByName(charNamestr.c_str()))
    {
        //TODO translate
        //ChatHandler(player).PSendSysMessage("You are now unbanned from the World channel.");
        ChatHandler(player).PSendSysMessage("Vous êtes maintenant débanni du channel world.");
    }
    
    return true;
}

bool ChatHandler::HandleChanInfoBan(const char* args)
{
    ARGS_CHECK
        
    std::string channelNamestr = "world";
    
    char* charname = strtok((char*)args, "");
    if (!charname)
        return false;
        
    std::string charNamestr = charname;
    
    if (!normalizePlayerName(charNamestr)) {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }
    
    uint32 accountid = sCharacterCache->GetCharacterAccountIdByName(charNamestr.c_str());
    if (!accountid)
    {
        PSendSysMessage("No account found for player %s", charNamestr.c_str());
        SetSentErrorMessage(true);
        return false;
    }
        
    QueryResult result = CharacterDatabase.PQuery("SELECT reason, FROM_UNIXTIME(expire), expire FROM channel_ban WHERE accountid = %u AND channel = '%s'", accountid, channelNamestr.c_str());
    if (result) {
        do {
            Field *fields = result->Fetch();
            std::string reason = fields[0].GetString();
            std::string expiredate = fields[1].GetString();
            time_t expiretimestamp = time_t(fields[2].GetUInt32());

            PSendSysMessage("Reason: \"%s\" - Expires in: %s %s", reason.c_str(), expiredate.c_str(), (expiretimestamp > time(nullptr)) ? "(actif)" : "");
        } while (result->NextRow());
    }
    else {
        PSendSysMessage("No ban on this player.");
        SetSentErrorMessage(true);
        return false;
    }
    
    return true;
}

bool ChatHandler::HandleMmapPathCommand(const char* args)
{
    if (!MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(GetSession()->GetPlayer()->GetMapId()))
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    PSendSysMessage("mmap path:");

    // units
    Player* player = GetSession()->GetPlayer();
    Unit* target = GetSelectedUnit();
    if (!player || !target)
    {
       PSendSysMessage("Invalid target/source selection.");
       SetSentErrorMessage(true);
       return false;
    }

    char* para = strtok((char*)args, " ");

    bool useStraightPath = false;
    if (para && strcmp(para, "true") == 0)
        useStraightPath = true;

    // unit locations
    float x, y, z;
    player->GetPosition(x, y, z);

    // path
    PathGenerator path(target);
    path.SetUseStraightPath(useStraightPath);
    bool result = path.CalculatePath(x, y, z);

    Movement::PointsArray const& pointPath = path.GetPath();
    PSendSysMessage("%s's path to %s:", target->GetName().c_str(), player->GetName().c_str());
    PSendSysMessage("Building: %s", useStraightPath ? "StraightPath" : "SmoothPath");
    PSendSysMessage("Result: %s - Length: %zu - Type: %u", (result ? "true" : "false"), pointPath.size(), path.GetPathType());

    G3D::Vector3 const &start = path.GetStartPosition();
    G3D::Vector3 const &end = path.GetEndPosition();
    G3D::Vector3 const &actualEnd = path.GetActualEndPosition();

    PSendSysMessage("StartPosition     (%.3f, %.3f, %.3f)", start.x, start.y, start.z);
    PSendSysMessage("EndPosition       (%.3f, %.3f, %.3f)", end.x, end.y, end.z);
    PSendSysMessage("ActualEndPosition (%.3f, %.3f, %.3f)", actualEnd.x, actualEnd.y, actualEnd.z);

    if (!player->IsGameMaster())
        PSendSysMessage("Enable GM mode to see the path points.");

    for (const auto & i : pointPath)
        player->SummonCreature(VISUAL_WAYPOINT, i.x, i.y, i.z, 0, TEMPSUMMON_TIMED_DESPAWN, 9000);

    return true;
}

bool ChatHandler::HandleMmapLocCommand(const char* /*args*/)
{
    

    PSendSysMessage("mmap tileloc:");

    // grid tile location
    Player* player = m_session->GetPlayer();

    int32 gx = 32 - player->GetPositionX() / SIZE_OF_GRIDS;
    int32 gy = 32 - player->GetPositionY() / SIZE_OF_GRIDS;

    PSendSysMessage("%03u%02i%02i.mmtile", player->GetMapId(), gy, gx);
    PSendSysMessage("gridloc [%i,%i]", gx, gy);

    // calculate navmesh tile location
    const dtNavMesh* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(player->GetMapId());
    const dtNavMeshQuery* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(player->GetMapId(), player->GetInstanceId());
    if (!navmesh || !navmeshquery)
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    const float* min = navmesh->getParams()->orig;

    float x, y, z;
    player->GetPosition(x, y, z);
    float location[VERTEX_SIZE] = {y, z, x};
    float extents[VERTEX_SIZE] = {2.f,4.f,2.f};

    int32 tilex = int32((y - min[0]) / SIZE_OF_GRIDS);
    int32 tiley = int32((x - min[2]) / SIZE_OF_GRIDS);

    PSendSysMessage("Calc   [%02i,%02i]", tilex, tiley);

    // navmesh poly -> navmesh tile location
    dtQueryFilter filter = dtQueryFilter();
    dtPolyRef polyRef = INVALID_POLYREF;
    navmeshquery->findNearestPoly(location, extents, &filter, &polyRef, nullptr);

    if (polyRef == INVALID_POLYREF)
        PSendSysMessage("Dt     [??,??] (invalid poly, probably no tile loaded)");
    else
    {
        const dtMeshTile* tile;
        const dtPoly* poly;
        navmesh->getTileAndPolyByRef(polyRef, &tile, &poly);
        if (tile)
            PSendSysMessage("Dt     [%02i,%02i]", tile->header->x, tile->header->y);
        else
            PSendSysMessage("Dt     [??,??] (no tile loaded)");
    }

    return true;
}

bool ChatHandler::HandleMmapLoadedTilesCommand(const char* /*args*/)
{
    

    uint32 mapid = m_session->GetPlayer()->GetMapId();

    const dtNavMesh* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapid);
    const dtNavMeshQuery* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(mapid, m_session->GetPlayer()->GetInstanceId());
    if (!navmesh || !navmeshquery)
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    PSendSysMessage("mmap loadedtiles:");

    for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
    {
        const dtMeshTile* tile = navmesh->getTile(i);
        if (!tile || !tile->header)
            continue;

        PSendSysMessage("[%02i,%02i]", tile->header->x, tile->header->y);
    }

    return true;
}

bool ChatHandler::HandleMmapStatsCommand(const char* /*args*/)
{
    PSendSysMessage("mmap stats:");

    MMAP::MMapManager *manager = MMAP::MMapFactory::createOrGetMMapManager();
    PSendSysMessage(" %u maps loaded with %u tiles overall", manager->getLoadedMapsCount(), manager->getLoadedTilesCount());

    const dtNavMesh* navmesh = manager->GetNavMesh(m_session->GetPlayer()->GetMapId());
    if (!navmesh)
    {
        PSendSysMessage("NavMesh not loaded for current map.");
        return true;
    }

    uint32 tileCount = 0;
    uint32 nodeCount = 0;
    uint32 polyCount = 0;
    uint32 vertCount = 0;
    uint32 triCount = 0;
    uint32 triVertCount = 0;
    uint32 dataSize = 0;
    for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
    {
        const dtMeshTile* tile = navmesh->getTile(i);
        if (!tile || !tile->header)
            continue;

        tileCount ++;
        nodeCount += tile->header->bvNodeCount;
        polyCount += tile->header->polyCount;
        vertCount += tile->header->vertCount;
        triCount += tile->header->detailTriCount;
        triVertCount += tile->header->detailVertCount;
        dataSize += tile->dataSize;
    }

    PSendSysMessage("Navmesh stats on current map:");
    PSendSysMessage(" %u tiles loaded", tileCount);
    PSendSysMessage(" %u BVTree nodes", nodeCount);
    PSendSysMessage(" %u polygons (%u vertices)", polyCount, vertCount);
    PSendSysMessage(" %u triangles (%u vertices)", triCount, triVertCount);
    PSendSysMessage(" %.2f MB of data (not including pointers)", ((float)dataSize / sizeof(unsigned char)) / 1048576);

    return true;
}

bool ChatHandler::HandlePetRenameCommand(const char* args)
{
    ARGS_CHECK
    Creature* target = GetSelectedCreature();
    
    if (!target || !target->IsPet())
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }
        
    Pet* targetPet = target->ToPet();
        
    if (targetPet->getPetType() != HUNTER_PET)
    {
        SendSysMessage("Must select a hunter pet");
        SetSentErrorMessage(true);
        return false;
    }
    
    Unit *owner = targetPet->GetOwner();
    if (owner && (owner->GetTypeId() == TYPEID_PLAYER) && (owner->ToPlayer())->GetGroup())
        (owner->ToPlayer())->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_NAME);
        
    targetPet->SetName(args);
    targetPet->SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, time(nullptr));
    
    return true;
}

//Visually copy stuff from player given to target player (fade off at disconnect like a normal morph)
bool ChatHandler::HandleCopyStuffCommand(char const * args)
{
    ARGS_CHECK

    std::string fromPlayerName = args;
    Player* fromPlayer = nullptr;
    Player* toPlayer = GetSelectedPlayerOrSelf();

    if(normalizePlayerName(fromPlayerName))
        fromPlayer = sObjectAccessor->FindConnectedPlayerByName(fromPlayerName.c_str());

    if(!fromPlayer || !toPlayer)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return true;
    }

    //4 lasts EQUIPMENT_SLOT = weapons (16-17-18?) + ammunition (19?)
    for (uint8 slot = 0; slot < (EQUIPMENT_SLOT_END - 4); slot++)
    {
        uint32 visualbase = PLAYER_VISIBLE_ITEM_1_0 + (slot * MAX_VISIBLE_ITEM_OFFSET);
        toPlayer->SetUInt32Value(visualbase,fromPlayer->GetUInt32Value(visualbase));
    }

    //copy helm/cloak settings
     if(fromPlayer->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
        toPlayer->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
     else
        toPlayer->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);

    if(fromPlayer->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
        toPlayer->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
    else
        toPlayer->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);

    return true;
}

/* Syntax : .npc path type [PathType] 
No arguments means print current type. This DOES NOT update values in db, use .path type to do so.

Possible types :
0 - WP_PATH_TYPE_LOOP
1 - WP_PATH_TYPE_ONCE
2 - WP_PATH_TYPE_ROUND_TRIP
*/
bool ChatHandler::HandleNpcPathTypeCommand(const char* args)
{
    Creature* target = GetSelectedCreature();
    if(!target)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    if(target->GetMotionMaster()->GetCurrentMovementGeneratorType() != WAYPOINT_MOTION_TYPE)
    {
        SendSysMessage("Creature is not using waypoint movement generator.");
        return true;
    }

    auto movGenerator = dynamic_cast<WaypointMovementGenerator<Creature>*>(target->GetMotionMaster()->top());
    if(!movGenerator)
    {
        SendSysMessage("Could not get movement generator.");
        return true;
    }

    if(!*args)
    { //getter
        WaypointPathType type = movGenerator->GetPathType();
        std::string pathTypeStr = GetWaypointPathTypeName(type);
        PSendSysMessage("Creature waypoint movement type : %s (%u).", pathTypeStr.c_str(), type);
    } else 
    { //setter
        uint32 type = (uint32)atoi(args);
        bool ok = movGenerator->SetPathType(WaypointPathType(type));
        if(!ok)
        {
            PSendSysMessage("Wrong type given : %u.", type);
            return false;
        }
        std::string pathTypeStr = GetWaypointPathTypeName(WaypointPathType(type));
        PSendSysMessage("Target creature path type set to %s (%u).", pathTypeStr.c_str(), type);
    }
    return true;
}

/* Syntax : .npc path direction [PathDirection]
No arguments means print current direction. This DOES NOT update values in db, use .path direction to do so.

Possible directions :
0 - WP_PATH_DIRECTION_NORMAL
1 - WP_PATH_DIRECTION_REVERSE
*/
bool ChatHandler::HandleNpcPathDirectionCommand(const char* args)
{
    Creature* target = GetSelectedCreature();
    if(!target)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    if(target->GetMotionMaster()->GetCurrentMovementGeneratorType() != WAYPOINT_MOTION_TYPE)
    {
        SendSysMessage("Creature is not using waypoint movement generator.");
        return true;
    }

    auto movGenerator = dynamic_cast<WaypointMovementGenerator<Creature>*>(target->GetMotionMaster()->top());
    if(!movGenerator)
    {
        SendSysMessage("Could not get movement generator.");
        return true;
    }

    if(!*args)
    { //getter
        WaypointPathDirection dir = movGenerator->GetPathDirection();
        std::string pathDirStr = GetWaypointPathDirectionName(WaypointPathDirection(dir));
        PSendSysMessage("Creature waypoint movement direction : %s (%u).", pathDirStr.c_str(), dir);
    } else 
    { //setter
        uint32 dir = (uint32)atoi(args);
        bool ok = movGenerator->SetDirection(WaypointPathDirection(dir));
        if(!ok)
        {
            PSendSysMessage("Wrong direction given : %u.", dir);
            return false;
        }
        std::string pathDirStr = GetWaypointPathDirectionName(WaypointPathDirection(dir));
        PSendSysMessage("Target creature path direction set to %s (%u).", pathDirStr.c_str(), dir);
    }
    return true;
}

/* Syntax : .npc path currentid */
bool ChatHandler::HandleNpcPathCurrentIdCommand(const char* args)
{
    Creature* target = GetSelectedCreature();
    if(!target)
    {
        SendSysMessage(LANG_SELECT_CREATURE);
        return true;
    }

    uint32 pathId = target->GetWaypointPathId();
    PSendSysMessage("Target creature current path id : %u.", pathId);

    return true;
}