/*
 * Copyright (C) 2008-2015 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildFinderMgr.h"
#include "ScriptMgr.h"
#include "Chat.h"
#include "Config.h"
#include "SocialMgr.h"
#include "Log.h"
#include "AccountMgr.h"
#include "CalendarMgr.h"

#define MAX_GUILD_BANK_TAB_TEXT_LEN 500
#define EMBLEM_PRICE 10 * GOLD

inline uint32 _GetGuildBankTabPrice(uint8 tabId)
{
    switch (tabId)
    {
        case 0: return 100;
        case 1: return 250;
        case 2: return 500;
        case 3: return 1000;
        case 4: return 2500;
        case 5: return 5000;
        default: return 0;
    }
}

void Guild::SendCommandResult(WorldSession* session, GuildCommandType type, GuildCommandError errCode, const std::string& param)
{
    WorldPacket data(SMSG_GUILD_COMMAND_RESULT, 8 + param.size() + 1);
    data << uint32(type);
    data << uint32(errCode);
    data << uint8(param.size());
    data.append(param.c_str(), param.size());
    session->SendPacket(&data);
}

void Guild::SendSaveEmblemResult(WorldSession* session, GuildEmblemError errCode)
{
    WorldPacket data(MSG_SAVE_GUILD_EMBLEM, 4);
    data << uint32(errCode);
    session->SendPacket(&data);
}

// LogHolder
Guild::LogHolder::~LogHolder()
{
    // Cleanup
    for (GuildLog::iterator itr = m_log.begin(); itr != m_log.end(); ++itr)
        delete (*itr);
}

// Adds event loaded from database to collection
inline void Guild::LogHolder::LoadEvent(LogEntry* entry)
{
    if (m_nextGUID == uint32(GUILD_EVENT_LOG_GUID_UNDEFINED))
        m_nextGUID = entry->GetGUID();
    m_log.push_front(entry);
}

// Adds new event happened in game.
// If maximum number of events is reached, oldest event is removed from collection.
inline void Guild::LogHolder::AddEvent(SQLTransaction& trans, LogEntry* entry)
{
    // Check max records limit
    if (m_log.size() >= m_maxRecords)
    {
        LogEntry* oldEntry = m_log.front();
        delete oldEntry;
        m_log.pop_front();
    }
    // Add event to list
    m_log.push_back(entry);
    // Save to DB
    entry->SaveToDB(trans);
}

// Writes information about all events into packet.
inline void Guild::LogHolder::WritePacket(WorldPacket& data) const
{
    ByteBuffer buffer;
    data.WriteBits(m_log.size(), 23);
    for (GuildLog::const_iterator itr = m_log.begin(); itr != m_log.end(); ++itr)
        (*itr)->WritePacket(data, buffer);

    data.append(buffer);
}

inline uint32 Guild::LogHolder::GetNextGUID()
{
    // Next guid was not initialized. It means there are no records for this holder in DB yet.
    // Start from the beginning.
    if (m_nextGUID == uint32(GUILD_EVENT_LOG_GUID_UNDEFINED))
        m_nextGUID = 0;
    else
        m_nextGUID = (m_nextGUID + 1) % m_maxRecords;
    return m_nextGUID;
}

// EventLogEntry
void Guild::EventLogEntry::SaveToDB(SQLTransaction& trans) const
{
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_EVENTLOG);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt32(1, m_guid);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);

    uint8 index = 0;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_EVENTLOG);
    stmt->setUInt32(  index, m_guildId);
    stmt->setUInt32(++index, m_guid);
    stmt->setUInt8 (++index, uint8(m_eventType));
    stmt->setUInt32(++index, m_playerGuid1);
    stmt->setUInt32(++index, m_playerGuid2);
    stmt->setUInt8 (++index, m_newRank);
    stmt->setUInt64(++index, m_timestamp);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

void Guild::EventLogEntry::WritePacket(WorldPacket& data, ByteBuffer& content) const
{
    ObjectGuid guid1 = MAKE_NEW_GUID(m_playerGuid1, 0, HIGHGUID_PLAYER);
    ObjectGuid guid2 = MAKE_NEW_GUID(m_playerGuid2, 0, HIGHGUID_PLAYER);

    data.WriteByteMask(guid1[2]);
    data.WriteByteMask(guid1[4]);
    data.WriteByteMask(guid2[7]);
    data.WriteByteMask(guid2[6]);
    data.WriteByteMask(guid1[3]);
    data.WriteByteMask(guid2[3]);
    data.WriteByteMask(guid2[5]);
    data.WriteByteMask(guid1[7]);
    data.WriteByteMask(guid1[5]);
    data.WriteByteMask(guid1[0]);
    data.WriteByteMask(guid2[4]);
    data.WriteByteMask(guid2[2]);
    data.WriteByteMask(guid2[0]);
    data.WriteByteMask(guid2[1]);
    data.WriteByteMask(guid1[1]);
    data.WriteByteMask(guid1[6]);

    content.WriteByteSeq(guid2[3]);
    content.WriteByteSeq(guid2[2]);
    content.WriteByteSeq(guid2[5]);

    // New Rank
    content << uint8(m_newRank);

    content.WriteByteSeq(guid2[4]);
    content.WriteByteSeq(guid1[0]);
    content.WriteByteSeq(guid1[4]);

    // Event timestamp
    content << uint32(::time(NULL) - m_timestamp);

    content.WriteByteSeq(guid1[7]);
    content.WriteByteSeq(guid1[3]);
    content.WriteByteSeq(guid2[0]);
    content.WriteByteSeq(guid2[6]);
    content.WriteByteSeq(guid2[7]);
    content.WriteByteSeq(guid1[5]);

    // Event type
    content << uint8(m_eventType);

    content.WriteByteSeq(guid2[1]);
    content.WriteByteSeq(guid1[2]);
    content.WriteByteSeq(guid1[6]);
    content.WriteByteSeq(guid1[1]);
}

// BankEventLogEntry
void Guild::BankEventLogEntry::SaveToDB(SQLTransaction& trans) const
{
    uint8 index = 0;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_EVENTLOG);
    stmt->setUInt32(  index, m_guildId);
    stmt->setUInt32(++index, m_guid);
    stmt->setUInt8 (++index, m_bankTabId);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);

    index = 0;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_EVENTLOG);
    stmt->setUInt32(  index, m_guildId);
    stmt->setUInt32(++index, m_guid);
    stmt->setUInt8 (++index, m_bankTabId);
    stmt->setUInt8 (++index, uint8(m_eventType));
    stmt->setUInt32(++index, m_playerGuid);
    stmt->setUInt32(++index, m_itemOrMoney);
    stmt->setUInt16(++index, m_itemStackCount);
    stmt->setUInt8 (++index, m_destTabId);
    stmt->setUInt64(++index, m_timestamp);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

void Guild::BankEventLogEntry::WritePacket(WorldPacket& data, ByteBuffer& content) const
{
    ObjectGuid logGuid = MAKE_NEW_GUID(m_playerGuid, 0, HIGHGUID_PLAYER);

    bool hasItem = m_eventType == GUILD_BANK_LOG_DEPOSIT_ITEM || m_eventType == GUILD_BANK_LOG_WITHDRAW_ITEM ||
                   m_eventType == GUILD_BANK_LOG_MOVE_ITEM || m_eventType == GUILD_BANK_LOG_MOVE_ITEM2;

    bool itemMoved = (m_eventType == GUILD_BANK_LOG_MOVE_ITEM || m_eventType == GUILD_BANK_LOG_MOVE_ITEM2);

    bool hasStack = (hasItem && m_itemStackCount > 1) || itemMoved;

    data.WriteBit(IsMoneyEvent());
    data.WriteByteMask(logGuid[4]);
    data.WriteByteMask(logGuid[1]);
    data.WriteBit(hasItem);
    data.WriteBit(hasStack);
    data.WriteByteMask(logGuid[2]);
    data.WriteByteMask(logGuid[5]);
    data.WriteByteMask(logGuid[3]);
    data.WriteByteMask(logGuid[6]);
    data.WriteByteMask(logGuid[0]);
    data.WriteBit(itemMoved);
    data.WriteByteMask(logGuid[7]);

    content.WriteByteSeq(logGuid[6]);
    content.WriteByteSeq(logGuid[1]);
    content.WriteByteSeq(logGuid[5]);
    if (hasStack)
        content << uint32(m_itemStackCount);

    content << uint8(m_eventType);
    content.WriteByteSeq(logGuid[2]);
    content.WriteByteSeq(logGuid[4]);
    content.WriteByteSeq(logGuid[0]);
    content.WriteByteSeq(logGuid[7]);
    content.WriteByteSeq(logGuid[3]);
    if (hasItem)
        content << uint32(m_itemOrMoney);

    content << uint32(time(NULL) - m_timestamp);

    if (IsMoneyEvent())
        content << uint64(m_itemOrMoney);

    if (itemMoved)
        content << uint8(m_destTabId);
}

// RankInfo
void Guild::RankInfo::LoadFromDB(Field* fields)
{
    m_rankId            = fields[1].GetUInt8();
    m_name              = fields[2].GetString();
    m_rights            = fields[3].GetUInt32();
    m_bankMoneyPerDay   = fields[4].GetUInt32();
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        m_rights |= GR_RIGHT_ALL;
}

void Guild::RankInfo::SaveToDB(SQLTransaction& trans) const
{
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_RANK);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt8 (1, m_rankId);
    stmt->setString(2, m_name);
    stmt->setUInt32(3, m_rights);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

void Guild::RankInfo::SetName(const std::string& name)
{
    if (m_name == name)
        return;

    m_name = name;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_NAME);
    stmt->setString(0, m_name);
    stmt->setUInt8 (1, m_rankId);
    stmt->setUInt32(2, m_guildId);
    CharacterDatabase.Execute(stmt);
}

void Guild::RankInfo::SetRights(uint32 rights)
{
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        rights = GR_RIGHT_ALL;

    if (m_rights == rights)
        return;

    m_rights = rights;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_RIGHTS);
    stmt->setUInt32(0, m_rights);
    stmt->setUInt8 (1, m_rankId);
    stmt->setUInt32(2, m_guildId);
    CharacterDatabase.Execute(stmt);
}

void Guild::RankInfo::SetBankMoneyPerDay(uint32 money)
{
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        money = uint32(GUILD_WITHDRAW_MONEY_UNLIMITED);

    if (m_bankMoneyPerDay == money)
        return;

    m_bankMoneyPerDay = money;

    PreparedStatement* stmt = NULL;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_BANK_MONEY);
    stmt->setUInt32(0, money);
    stmt->setUInt8 (1, m_rankId);
    stmt->setUInt32(2, m_guildId);
    CharacterDatabase.Execute(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_BANK_RESET_TIME);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt8 (1, m_rankId);
    CharacterDatabase.Execute(stmt);
}

void Guild::RankInfo::SetBankTabSlotsAndRights(uint8 tabId, GuildBankRightsAndSlots rightsAndSlots, bool saveToDB)
{
    if (m_rankId == GR_GUILDMASTER)                     // Prevent loss of leader rights
        rightsAndSlots.SetGuildMasterValues();

    GuildBankRightsAndSlots& guildBR = m_bankTabRightsAndSlots[tabId];
    if (guildBR.IsEqual(rightsAndSlots))
        return;

    guildBR = rightsAndSlots;

    if (saveToDB)
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_RIGHT);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8 (1, tabId);
        stmt->setUInt8 (2, m_rankId);
        CharacterDatabase.Execute(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_RIGHT);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8 (1, tabId);
        stmt->setUInt8 (2, m_rankId);
        stmt->setUInt8 (3, guildBR.rights);
        stmt->setUInt32(4, guildBR.slots);
        CharacterDatabase.Execute(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_RANK_BANK_TIME0 + tabId);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8 (1, m_rankId);
        CharacterDatabase.Execute(stmt);
    }
}

// BankTab
bool Guild::BankTab::LoadFromDB(Field* fields)
{
    m_name = fields[2].GetString();
    m_icon = fields[3].GetString();
    m_text = fields[4].GetString();
    return true;
}

bool Guild::BankTab::LoadItemFromDB(Field* fields)
{
    uint8 slotId = fields[13].GetUInt8();
    uint32 itemGuid = fields[14].GetUInt32();
    uint32 itemEntry = fields[15].GetUInt32();
    if (slotId >= GUILD_BANK_MAX_SLOTS)
        return false;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
    if (!proto)
        return false;

    Item* pItem = NewItemOrBag(proto);
    if (!pItem->LoadFromDB(itemGuid, 0, fields, itemEntry))
    {
        CharacterDatabase.PExecute("DELETE FROM guild_bank_item WHERE guildid = '%u' AND TabId = '%u' AND SlotId = '%u'", m_guildId, m_tabId, slotId);

        delete pItem;
        return false;
    }

    pItem->AddToWorld();
    m_items[slotId] = pItem;
    return true;
}

// Deletes contents of the tab from the world (and from DB if necessary)
void Guild::BankTab::Delete(SQLTransaction& trans, bool removeItemsFromDB)
{
    for (uint8 slotId = 0; slotId < GUILD_BANK_MAX_SLOTS; ++slotId)
        if (Item* pItem = m_items[slotId])
        {
            pItem->RemoveFromWorld();
            if (removeItemsFromDB)
                pItem->DeleteFromDB(trans);
            delete pItem;
            pItem = NULL;
        }
}

void Guild::BankTab::SetInfo(const std::string& name, const std::string& icon)
{
    if (m_name == name && m_icon == icon)
        return;

    m_name = name;
    m_icon = icon;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_BANK_TAB_INFO);
    stmt->setString(0, m_name);
    stmt->setString(1, m_icon);
    stmt->setUInt32(2, m_guildId);
    stmt->setUInt8 (3, m_tabId);
    CharacterDatabase.Execute(stmt);
}

void Guild::BankTab::SetText(const std::string& text)
{
    if (m_text == text)
        return;

    m_text = text;
    utf8truncate(m_text, MAX_GUILD_BANK_TAB_TEXT_LEN);          // DB and client size limitation

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_BANK_TAB_TEXT);
    stmt->setString(0, m_text);
    stmt->setUInt32(1, m_guildId);
    stmt->setUInt8 (2, m_tabId);
    CharacterDatabase.Execute(stmt);
}

// Sets/removes contents of specified slot.
// If pItem == NULL contents are removed.
bool Guild::BankTab::SetItem(SQLTransaction& trans, uint8 slotId, Item* item)
{
    if (slotId >= GUILD_BANK_MAX_SLOTS)
        return false;

    m_items[slotId] = item;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_ITEM);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt8 (1, m_tabId);
    stmt->setUInt8 (2, slotId);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);

    if (item)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_ITEM);
        stmt->setUInt32(0, m_guildId);
        stmt->setUInt8 (1, m_tabId);
        stmt->setUInt8 (2, slotId);
        stmt->setUInt32(3, item->GetGUIDLow());
        CharacterDatabase.ExecuteOrAppend(trans, stmt);

        item->SetUInt64Value(ITEM_FIELD_CONTAINED, 0);
        item->SetUInt64Value(ITEM_FIELD_OWNER, 0);
        item->FSetState(ITEM_NEW);
        item->SaveToDB(trans);                                 // Not in inventory and can be saved standalone
    }
    return true;
}

void Guild::BankTab::SendText(Guild const* guild, WorldSession* session) const
{
    uint32 size = uint32(m_text.size());
    WorldPacket data(SMSG_GUILD_BANK_QUERY_TEXT_RESULT, 1 + size + 1);

    data
        << WriteAsUnaligned<14>(size)
        << uint32(m_tabId)
        << WriteBuffer(m_text.c_str(), size);

    if (session)
        session->SendPacket(&data);
    else
        guild->BroadcastPacket(&data);
}

// Member
void Guild::Member::SetStats(Player* player)
{
    m_name      = player->GetName();
    m_level     = player->getLevel();
    m_class     = player->getClass();
    m_zoneId    = player->GetZoneId();
    m_accountId = player->GetSession()->GetAccountId();

    m_achievementPoints = player->GetAchievementMgr().GetAchievementPoints();

    uint8 maxProf = 2;
    uint32 prev_skill = 0;
    for (PlayerSpellMap::const_iterator spellIter = player->GetSpellMap().begin(); spellIter != player->GetSpellMap().end(); ++spellIter)
    {
        if (maxProf == 0)
            break;

        uint32 spellId = spellIter->first;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            continue;

        if (!spellInfo->IsPrimaryProfession())
            continue;

        uint32 skillId = 0;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_SKILL)
            {
                skillId = uint32(spellInfo->Effects[i].MiscValue);
                break;
            }
        }

        if (prev_skill == skillId)
            continue;

        prev_skill = skillId;

        uint16 value = player->GetSkillValue(skillId);
        uint8 rank = sSpellMgr->GetSpellRank(spellId);

        SetProfession(maxProf--, value, skillId, rank);
    }

    if (prev_skill > 0)
    {
        for (uint8 i = prev_skill; i < 2; ++i)
            SetProfession(i, 0, 0, 0);
    }

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_PROFESSIONS);
    stmt->setUInt32(0, _professions[0].value);
    stmt->setUInt32(1, _professions[0].skillId);
    stmt->setUInt32(2, _professions[0].rank);
    stmt->setUInt32(3, _professions[1].value);
    stmt->setUInt32(4, _professions[1].skillId);
    stmt->setUInt32(5, _professions[1].rank);
    stmt->setUInt32(6, m_guildId);
    stmt->setUInt32(7, GUID_LOPART(m_guid));
    CharacterDatabase.Execute(stmt);
}

void Guild::Member::SetStats(const std::string& name, uint8 level, uint8 _class, uint32 zoneId, uint32 accountId)
{
    m_name      = name;
    m_level     = level;
    m_class     = _class;
    m_zoneId    = zoneId;
    m_accountId = accountId;

    for (uint8 i = 0; i < 2; ++i)
        SetProfession(i, 0, 0, 0);
}

void Guild::Member::SetPublicNote(const std::string& publicNote)
{
    if (m_publicNote == publicNote)
        return;

    m_publicNote = publicNote;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_PNOTE);
    stmt->setString(0, publicNote);
    stmt->setUInt32(1, GUID_LOPART(m_guid));
    CharacterDatabase.Execute(stmt);
}

void Guild::Member::SetOfficerNote(const std::string& officerNote)
{
    if (m_officerNote == officerNote)
        return;

    m_officerNote = officerNote;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_OFFNOTE);
    stmt->setString(0, officerNote);
    stmt->setUInt32(1, GUID_LOPART(m_guid));
    CharacterDatabase.Execute(stmt);
}

void Guild::Member::ChangeRank(uint8 newRank)
{
    m_rankId = newRank;

    // Update rank information in player's field, if he is online.
    if (Player* player = FindPlayer())
        player->SetRank(newRank);

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_RANK);
    stmt->setUInt8 (0, newRank);
    stmt->setUInt32(1, GUID_LOPART(m_guid));
    CharacterDatabase.Execute(stmt);
}

void Guild::Member::SaveToDB(SQLTransaction& trans) const
{
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_MEMBER);
    stmt->setUInt32(0, m_guildId);
    stmt->setUInt32(1, GUID_LOPART(m_guid));
    stmt->setUInt8 (2, m_rankId);
    stmt->setString(3, m_publicNote);
    stmt->setString(4, m_officerNote);
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

// Loads member's data from database.
// If member has broken fields (level, class) returns false.
// In this case member has to be removed from guild.
bool Guild::Member::LoadFromDB(Field* fields, uint32 lowGuid)
{
    m_publicNote    = fields[3].GetString();
    m_officerNote   = fields[4].GetString();
    m_bankRemaining[GUILD_BANK_MAX_TABS].resetTime  = fields[5].GetUInt32();
    m_bankRemaining[GUILD_BANK_MAX_TABS].value      = fields[6].GetUInt32();
    for (uint8 i = 0; i < GUILD_BANK_MAX_TABS; ++i)
    {
        m_bankRemaining[i].resetTime                = fields[7 + i * 2].GetUInt32();
        m_bankRemaining[i].value                    = fields[8 + i * 2].GetUInt32();
    }

    SetStats(fields[23].GetString(),
             fields[24].GetUInt8(),                         // characters.level
             fields[25].GetUInt8(),                         // characters.class
             fields[26].GetUInt16(),                        // characters.zone
             fields[27].GetUInt32());                       // characters.account
    m_logoutTime    = fields[28].GetUInt32();               // characters.logout_time
    
    SetWeeklyReputation(fields[29].GetUInt32());
    SetAchievementPoints(fields[30].GetUInt32(), lowGuid);
 
    SetProfession(0, uint16(fields[31].GetUInt32()), fields[32].GetUInt32(), uint8(fields[33].GetUInt32()));
    SetProfession(1, uint16(fields[34].GetUInt32()), fields[35].GetUInt32(), uint8(fields[36].GetUInt32()));

    m_xpContrib = fields[37].GetUInt64();
    m_xpContribWeek = fields[38].GetUInt64();
   
    if (!CheckStats())
        return false;

    if (!m_zoneId)
        m_zoneId = Player::GetZoneIdFromDB(m_guid);

    return true;
}

// Validate player fields. Returns false if corrupted fields are found.
bool Guild::Member::CheckStats() const
{
    if (m_level < 1)
        return false;

    if (m_class < CLASS_WARRIOR || m_class >= MAX_CLASSES)
        return false;

    return true;
}

// Decreases amount of money/slots left for today.
// If (tabId == GUILD_BANK_MAX_TABS) decrease money amount.
// Otherwise decrease remaining items amount for specified tab.
void Guild::Member::DecreaseBankRemainingValue(SQLTransaction& trans, uint8 tabId, uint32 amount)
{
    m_bankRemaining[tabId].value -= amount;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(
        tabId == GUILD_BANK_MAX_TABS ?
        CHAR_UPD_GUILD_MEMBER_BANK_REM_MONEY :
        CHAR_UPD_GUILD_MEMBER_BANK_REM_SLOTS0 + tabId);
    stmt->setUInt32(0, m_bankRemaining[tabId].value);
    stmt->setUInt32(1, m_guildId);
    stmt->setUInt32(2, GUID_LOPART(m_guid));
    CharacterDatabase.ExecuteOrAppend(trans, stmt);
}

// Get amount of money/slots left for today.
// If (tabId == GUILD_BANK_MAX_TABS) return money amount.
// Otherwise return remaining items amount for specified tab.
// If reset time was more than 24 hours ago, renew reset time and reset amount to maximum value.
uint32 Guild::Member::GetBankRemainingValue(uint8 tabId, const Guild* guild) const
{
    // Guild master has unlimited amount.
    if (IsRank(GR_GUILDMASTER))
        return tabId == GUILD_BANK_MAX_TABS ? GUILD_WITHDRAW_MONEY_UNLIMITED : GUILD_WITHDRAW_SLOT_UNLIMITED;

    // Check rights for non-money tab.
    if (tabId != GUILD_BANK_MAX_TABS)
        if ((guild->_GetRankBankTabRights(m_rankId, tabId) & GUILD_BANK_RIGHT_VIEW_TAB) != GUILD_BANK_RIGHT_VIEW_TAB)
            return 0;

    uint32 curTime = uint32(::time(NULL) / MINUTE); // minutes
    if (curTime > m_bankRemaining[tabId].resetTime + 24 * HOUR / MINUTE)
    {
        RemainingValue& rv = const_cast <RemainingValue&> (m_bankRemaining[tabId]);
        rv.resetTime = curTime;
        rv.value = tabId == GUILD_BANK_MAX_TABS ?
            guild->_GetRankBankMoneyPerDay(m_rankId) :
            guild->_GetRankBankTabSlotsPerDay(m_rankId, tabId);

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(
            tabId == GUILD_BANK_MAX_TABS ?
            CHAR_UPD_GUILD_MEMBER_BANK_TIME_MONEY :
            CHAR_UPD_GUILD_MEMBER_BANK_TIME_REM_SLOTS0 + tabId);
        stmt->setUInt32(0, m_bankRemaining[tabId].resetTime);
        stmt->setUInt32(1, m_bankRemaining[tabId].value);
        stmt->setUInt32(2, m_guildId);
        stmt->setUInt32(3, GUID_LOPART(m_guid));
        CharacterDatabase.Execute(stmt);
    }
    return m_bankRemaining[tabId].value;
}

void Guild::Member::SetAchievementPoints(uint32 val, uint32 lowGuid, bool saveData)
{
    m_achievementPoints = val;
    if (saveData)
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_ACHIEVEMENTS);
        stmt->setUInt64(0, m_achievementPoints);
        stmt->setUInt32(1, m_guildId);
        stmt->setUInt32(2, lowGuid);
        CharacterDatabase.Execute(stmt);
    }
}

inline void Guild::Member::ResetTabTimes()
{
    for (uint8 tabId = 0; tabId < GUILD_BANK_MAX_TABS; ++tabId)
        m_bankRemaining[tabId].resetTime = 0;
}

inline void Guild::Member::ResetMoneyTime()
{
    m_bankRemaining[GUILD_BANK_MAX_TABS].resetTime = 0;
}

// EmblemInfo
void EmblemInfo::ReadPacket(WorldPacket& recv)
{
    recv >> m_style >> m_color >> m_borderStyle >> m_borderColor >> m_backgroundColor;
}

void EmblemInfo::LoadFromDB(Field* fields)
{
    m_style             = fields[3].GetUInt8();
    m_color             = fields[4].GetUInt8();
    m_borderStyle       = fields[5].GetUInt8();
    m_borderColor       = fields[6].GetUInt8();
    m_backgroundColor   = fields[7].GetUInt8();
}

void EmblemInfo::WritePacket(WorldPacket& data) const
{
    data << uint32(m_style);
    data << uint32(m_color);
    data << uint32(m_borderStyle);
    data << uint32(m_borderColor);
    data << uint32(m_backgroundColor);
}

void EmblemInfo::SaveToDB(uint32 guildId) const
{
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_EMBLEM_INFO);
    stmt->setUInt32(0, m_style);
    stmt->setUInt32(1, m_color);
    stmt->setUInt32(2, m_borderStyle);
    stmt->setUInt32(3, m_borderColor);
    stmt->setUInt32(4, m_backgroundColor);
    stmt->setUInt32(5, guildId);
    CharacterDatabase.Execute(stmt);
}

// MoveItemData
bool Guild::MoveItemData::CheckItem(uint32& splitedAmount)
{
    ASSERT(m_pItem);
    if (splitedAmount > m_pItem->GetCount())
        return false;
    if (splitedAmount == m_pItem->GetCount())
        splitedAmount = 0;
    return true;
}

bool Guild::MoveItemData::CanStore(Item* pItem, bool swap, bool sendError)
{
    m_vec.clear();
    InventoryResult msg = CanStore(pItem, swap);
    if (sendError && msg != EQUIP_ERR_OK)
        m_pPlayer->SendEquipError(msg, pItem);
    return (msg == EQUIP_ERR_OK);
}

bool Guild::MoveItemData::CloneItem(uint32 count)
{
    ASSERT(m_pItem);
    m_pClonedItem = m_pItem->CloneItem(count);
    if (!m_pClonedItem)
    {
        m_pPlayer->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, m_pItem);
        return false;
    }
    return true;
}

void Guild::MoveItemData::LogAction(MoveItemData* pFrom) const
{
    ASSERT(pFrom->GetItem());
}

inline void Guild::MoveItemData::CopySlots(SlotIds& ids) const
{
    for (ItemPosCountVec::const_iterator itr = m_vec.begin(); itr != m_vec.end(); ++itr)
        ids.insert(uint8(itr->pos));
}

// PlayerMoveItemData
bool Guild::PlayerMoveItemData::InitItem()
{
    m_pItem = m_pPlayer->GetItemByPos(m_container, m_slotId);
    if (m_pItem)
    {
        // Anti-WPE protection. Do not move non-empty bags to bank.
        if (m_pItem->IsNotEmptyBag())
        {
            m_pPlayer->SendEquipError(EQUIP_ERR_DESTROY_NONEMPTY_BAG, m_pItem);
            m_pItem = NULL;
        }
        // Bound items cannot be put into bank.
        else if (!m_pItem->CanBeTraded())
        {
            m_pPlayer->SendEquipError(EQUIP_ERR_CANT_SWAP, m_pItem);
            m_pItem = NULL;
        }
    }
    return (m_pItem != NULL);
}

void Guild::PlayerMoveItemData::RemoveItem(SQLTransaction& trans, MoveItemData* /*pOther*/, uint32 splitedAmount)
{
    if (splitedAmount)
    {
        m_pItem->SetCount(m_pItem->GetCount() - splitedAmount);
        m_pItem->SetState(ITEM_CHANGED, m_pPlayer);
        m_pPlayer->SaveInventoryAndGoldToDB(trans);
    }
    else
    {
        m_pPlayer->MoveItemFromInventory(m_container, m_slotId, true);
        m_pItem->DeleteFromInventoryDB(trans);
        m_pItem = NULL;
    }
}

Item* Guild::PlayerMoveItemData::StoreItem(SQLTransaction& trans, Item* pItem)
{
    ASSERT(pItem);
    m_pPlayer->MoveItemToInventory(m_vec, pItem, true);
    m_pPlayer->SaveInventoryAndGoldToDB(trans);
    return pItem;
}

void Guild::PlayerMoveItemData::LogBankEvent(SQLTransaction& trans, MoveItemData* pFrom, uint32 count) const
{
    ASSERT(pFrom);
    // Bank -> Char
    m_pGuild->_LogBankEvent(trans, GUILD_BANK_LOG_WITHDRAW_ITEM, pFrom->GetContainer(), m_pPlayer->GetGUIDLow(),
        pFrom->GetItem()->GetEntry(), count);
}

inline InventoryResult Guild::PlayerMoveItemData::CanStore(Item* pItem, bool swap)
{
    return m_pPlayer->CanStoreItem(m_container, m_slotId, m_vec, pItem, swap);
}

// BankMoveItemData
bool Guild::BankMoveItemData::InitItem()
{
    m_pItem = m_pGuild->_GetItem(m_container, m_slotId);
    return (m_pItem != NULL);
}

bool Guild::BankMoveItemData::HasStoreRights(MoveItemData* pOther) const
{
    ASSERT(pOther);
    // Do not check rights if item is being swapped within the same bank tab
    if (pOther->IsBank() && pOther->GetContainer() == m_container)
        return true;
    return m_pGuild->_MemberHasTabRights(m_pPlayer->GetGUID(), m_container, GUILD_BANK_RIGHT_DEPOSIT_ITEM);
}

bool Guild::BankMoveItemData::HasWithdrawRights(MoveItemData* pOther) const
{
    ASSERT(pOther);
    // Do not check rights if item is being swapped within the same bank tab
    if (pOther->IsBank() && pOther->GetContainer() == m_container)
        return true;
    return (m_pGuild->_GetMemberRemainingSlots(m_pPlayer->GetGUID(), m_container) != 0);
}

void Guild::BankMoveItemData::RemoveItem(SQLTransaction& trans, MoveItemData* pOther, uint32 splitedAmount)
{
    ASSERT(m_pItem);
    if (splitedAmount)
    {
        m_pItem->SetCount(m_pItem->GetCount() - splitedAmount);
        m_pItem->FSetState(ITEM_CHANGED);
        m_pItem->SaveToDB(trans);
    }
    else
    {
        m_pGuild->_RemoveItem(trans, m_container, m_slotId);
        m_pItem = NULL;
    }
    // Decrease amount of player's remaining items (if item is moved to different tab or to player)
    if (!pOther->IsBank() || pOther->GetContainer() != m_container)
        m_pGuild->_DecreaseMemberRemainingSlots(trans, m_pPlayer->GetGUID(), m_container);
}

Item* Guild::BankMoveItemData::StoreItem(SQLTransaction& trans, Item* pItem)
{
    if (!pItem)
        return NULL;

    BankTab* pTab = m_pGuild->GetBankTab(m_container);
    if (!pTab)
        return NULL;

    Item* pLastItem = pItem;

    for (ItemPosCountVec::const_iterator itr = m_vec.begin(); itr != m_vec.end(); )
    {
        ItemPosCount pos(*itr);
        ++itr;

        pLastItem = _StoreItem(trans, pTab, pItem, pos, itr != m_vec.end());
    }
    return pLastItem;
}

void Guild::BankMoveItemData::LogBankEvent(SQLTransaction& trans, MoveItemData* pFrom, uint32 count) const
{
    ASSERT(pFrom->GetItem());
    if (pFrom->IsBank())
        // Bank -> Bank
        m_pGuild->_LogBankEvent(trans, GUILD_BANK_LOG_MOVE_ITEM, pFrom->GetContainer(), m_pPlayer->GetGUIDLow(),
            pFrom->GetItem()->GetEntry(), count, m_container);
    else
        // Char -> Bank
        m_pGuild->_LogBankEvent(trans, GUILD_BANK_LOG_DEPOSIT_ITEM, m_container, m_pPlayer->GetGUIDLow(),
            pFrom->GetItem()->GetEntry(), count);
}

void Guild::BankMoveItemData::LogAction(MoveItemData* pFrom) const
{
    MoveItemData::LogAction(pFrom);
    if (!pFrom->IsBank() && sWorld->getBoolConfig(CONFIG_GM_LOG_TRADE) && !AccountMgr::IsPlayerAccount(m_pPlayer->GetSession()->GetSecurity()))       // TODO: move to scripts
        sLog->outCommand(m_pPlayer->GetSession()->GetAccountId(),
            "GM %s (Account: %u) deposit item: %s (Entry: %d Count: %u) to guild bank (Guild ID: %u)",
            m_pPlayer->GetName(), m_pPlayer->GetSession()->GetAccountId(),
            pFrom->GetItem()->GetTemplate()->Name1.c_str(), pFrom->GetItem()->GetEntry(), pFrom->GetItem()->GetCount(),
            m_pGuild->GetId());
}

Item* Guild::BankMoveItemData::_StoreItem(SQLTransaction& trans, BankTab* pTab, Item* pItem, ItemPosCount& pos, bool clone) const
{
    uint8 slotId = uint8(pos.pos);
    uint32 count = pos.count;
    if (Item* pItemDest = pTab->GetItem(slotId))
    {
        pItemDest->SetCount(pItemDest->GetCount() + count);
        pItemDest->FSetState(ITEM_CHANGED);
        pItemDest->SaveToDB(trans);
        if (!clone)
        {
            pItem->RemoveFromWorld();
            pItem->DeleteFromDB(trans);
            delete pItem;
        }
        return pItemDest;
    }

    if (clone)
        pItem = pItem->CloneItem(count);
    else
        pItem->SetCount(count);

    if (pItem && pTab->SetItem(trans, slotId, pItem))
        return pItem;

    return NULL;
}

// Tries to reserve space for source item.
// If item in destination slot exists it must be the item of the same entry
// and stack must have enough space to take at least one item.
// Returns false if destination item specified and it cannot be used to reserve space.
bool Guild::BankMoveItemData::_ReserveSpace(uint8 slotId, Item* pItem, Item* pItemDest, uint32& count)
{
    uint32 requiredSpace = pItem->GetMaxStackCount();
    if (pItemDest)
    {
        // Make sure source and destination items match and destination item has space for more stacks.
        if (pItemDest->GetEntry() != pItem->GetEntry() || pItemDest->GetCount() >= pItem->GetMaxStackCount())
            return false;
        requiredSpace -= pItemDest->GetCount();
    }
    // Let's not be greedy, reserve only required space
    requiredSpace = std::min(requiredSpace, count);

    // Reserve space
    ItemPosCount pos(slotId, requiredSpace);
    if (!pos.isContainedIn(m_vec))
    {
        m_vec.push_back(pos);
        count -= requiredSpace;
    }
    return true;
}

void Guild::BankMoveItemData::CanStoreItemInTab(Item* pItem, uint8 skipSlotId, bool merge, uint32& count)
{
    for (uint8 slotId = 0; (slotId < GUILD_BANK_MAX_SLOTS) && (count > 0); ++slotId)
    {
        // Skip slot already processed in CanStore (when destination slot was specified)
        if (slotId == skipSlotId)
            continue;

        Item* pItemDest = m_pGuild->_GetItem(m_container, slotId);
        if (pItemDest == pItem)
            pItemDest = NULL;

        // If merge skip empty, if not merge skip non-empty
        if ((pItemDest != NULL) != merge)
            continue;

        _ReserveSpace(slotId, pItem, pItemDest, count);
    }
}

InventoryResult Guild::BankMoveItemData::CanStore(Item* pItem, bool swap)
{
    uint32 count = pItem->GetCount();
    // Soulbound items cannot be moved
    if (pItem->IsSoulBound())
        return EQUIP_ERR_DROP_BOUND_ITEM;

    // Make sure destination bank tab exists
    if (m_container >= m_pGuild->_GetPurchasedTabsSize())
        return EQUIP_ERR_WRONG_BAG_TYPE;

    // Slot explicitely specified. Check it.
    if (m_slotId != NULL_SLOT)
    {
        Item* pItemDest = m_pGuild->_GetItem(m_container, m_slotId);
        // Ignore swapped item (this slot will be empty after move)
        if ((pItemDest == pItem) || swap)
            pItemDest = NULL;

        if (!_ReserveSpace(m_slotId, pItem, pItemDest, count))
            return EQUIP_ERR_CANT_STACK;

        if (count == 0)
            return EQUIP_ERR_OK;
    }

    // Slot was not specified or it has not enough space for all the items in stack
    // Search for stacks to merge with
    if (pItem->GetMaxStackCount() > 1)
    {
        CanStoreItemInTab(pItem, m_slotId, true, count);
        if (count == 0)
            return EQUIP_ERR_OK;
    }

    // Search free slot for item
    CanStoreItemInTab(pItem, m_slotId, false, count);
    if (count == 0)
        return EQUIP_ERR_OK;

    return EQUIP_ERR_BANK_FULL;
}

// Guild
Guild::Guild() : m_id(0), m_leaderGuid(0), m_createdDate(0), m_accountsNumber(0), m_bankMoney(0), m_eventLog(NULL),
    m_achievementMgr(this), _newsLog(this), _level(1), _experience(0), _todayExperience(0)
{
    memset(&m_bankEventLog, 0, (GUILD_BANK_MAX_TABS + 1) * sizeof(LogHolder*));
    memset(m_guildChallenges, 0, sizeof(m_guildChallenges));
}

Guild::~Guild()
{
    SQLTransaction temp(NULL);
    _DeleteBankItems(temp);

    // Cleanup
    if (m_eventLog)
        delete m_eventLog;
    for (uint8 tabId = 0; tabId <= GUILD_BANK_MAX_TABS; ++tabId)
        if (m_bankEventLog[tabId])
            delete m_bankEventLog[tabId];
    for (Members::iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        delete itr->second;
}

// Creates new guild with default data and saves it to database.
bool Guild::Create(Player* pLeader, const std::string& name)
{
    // Check if guild with such name already exists
    if (sGuildMgr->GetGuildByName(name))
        return false;

    WorldSession* pLeaderSession = pLeader->GetSession();
    if (!pLeaderSession)
        return false;

    m_id = sGuildMgr->GenerateGuildId();
    m_leaderGuid = pLeader->GetGUID();
    m_name = name;
    m_info = "";
    m_motd = "Сообщение не установлено.";
    m_bankMoney = 0;
    m_createdDate = ::time(NULL);
    _level = 1;
    _CreateLogHolders();

    PreparedStatement* stmt = NULL;
    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_MEMBERS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    uint8 index = 0;
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD);
    stmt->setUInt32(  index, m_id);
    stmt->setString(++index, name);
    stmt->setUInt32(++index, GUID_LOPART(m_leaderGuid));
    stmt->setString(++index, m_info);
    stmt->setString(++index, m_motd);
    stmt->setUInt64(++index, uint32(m_createdDate));
    stmt->setUInt32(++index, m_emblemInfo.GetStyle());
    stmt->setUInt32(++index, m_emblemInfo.GetColor());
    stmt->setUInt32(++index, m_emblemInfo.GetBorderStyle());
    stmt->setUInt32(++index, m_emblemInfo.GetBorderColor());
    stmt->setUInt32(++index, m_emblemInfo.GetBackgroundColor());
    stmt->setUInt64(++index, m_bankMoney);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
    
    pLeader->SetReputation(1168, 1);
    
    // Create default ranks
    _CreateDefaultGuildRanks(pLeaderSession->GetSessionDbLocaleIndex());
    // Add guildmaster
    bool ret = AddMember(m_leaderGuid, GR_GUILDMASTER);

    _BroadcastEvent(GE_FOUNDER, m_leaderGuid);

    return ret;
}

// Disbands guild and deletes all related data from database
void Guild::Disband()
{
    _BroadcastEvent(GE_DISBANDED, 0);
    // Remove all members
    while (!m_members.empty())
    {
        Members::const_iterator itr = m_members.begin();
        DeleteMember(itr->second->GetGUID(), true);
    }

    PreparedStatement* stmt = NULL;
    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_RANKS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_TABS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    // Free bank tab used memory and delete items stored in them
    _DeleteBankItems(trans, true);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_ITEMS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_RIGHTS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_EVENTLOGS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_EVENTLOGS);
    stmt->setUInt32(0, m_id);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    sGuildFinderMgr->DeleteGuild(m_id);

    sGuildMgr->RemoveGuild(m_id);
}

void Guild::SaveToDB()
{
    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_EXPERIENCE);
    stmt->setUInt32(0, GetLevel());
    stmt->setUInt64(1, GetExperience());
    stmt->setUInt64(2, GetTodayExperience());
    stmt->setUInt32(3, GetId());
    trans->Append(stmt);

    m_achievementMgr.SaveToDB(trans);

    CharacterDatabase.CommitTransaction(trans);
}

void Guild::UpdateMemberData(Player* pPlayer, uint8 dataid, uint32 value)
{
    if (Member* pMember = GetMember(pPlayer->GetGUID()))
    {
        switch(dataid)
        {
            case GUILD_MEMBER_DATA_ZONEID:
                pMember->SetZoneID(value);
                break;
            case GUILD_MEMBER_DATA_ACHIEVEMENT_POINTS:
                pMember->SetAchievementPoints(value, pPlayer->GetGUIDLow(), true);
                break;
            case GUILD_MEMBER_DATA_LEVEL:
                pMember->SetLevel(value);
                break;
        }
    }
}

void Guild::HandleRoster(WorldSession* session /*= NULL*/)
{
    uint32 motdSize = uint32(m_motd.length());
    uint32 infoLength = uint32(m_info.length());
    uint32 membersSize = uint32(m_members.size());
    uint32 weeklyRepCap = uint32(sWorld->getIntConfig(CONFIG_GUILD_WEEKLY_REP_CAP));

    ByteBuffer memberData((membersSize * 100));
    WorldPacket data(SMSG_GUILD_ROSTER, motdSize + infoLength + 16 + ((41 + (33 * membersSize)) / 8) + (membersSize * 100));

    data 
        << WriteAsUnaligned<11>(motdSize)
        << WriteAsUnaligned<18>(membersSize);

    time_t now = ::time(NULL);
    for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
    {
        Member* member = itr->second;
        Player* player = member->FindPlayer();

        uint32 nameSize = uint32(member->GetName().length());
        uint32 pubNoteLength = uint32(member->GetPublicNote().length());
        uint32 offNoteLength = uint32(member->GetOfficerNote().length());

        ObjectGuid guid = member->GetGUID();
        data.WriteByteMask(guid[3]);
        data.WriteByteMask(guid[4]);
        data.WriteBit(false); // Has Authenticator
        data.WriteBit(false); // Can Scroll of Ressurect
        data.WriteBits(pubNoteLength, 8);
        data.WriteBits(offNoteLength, 8);
        data.WriteByteMask(guid[0]);
        data.WriteBits(nameSize, 7);
        data.WriteByteMask(guid[1]);
        data.WriteByteMask(guid[2]);
        data.WriteByteMask(guid[6]);
        data.WriteByteMask(guid[5]);
        data.WriteByteMask(guid[7]);

        uint8 flags = GUILDMEMBER_STATUS_NONE;
        if (player)
        {
            flags |= GUILDMEMBER_STATUS_ONLINE;
            if (player->isAFK())
                flags |= GUILDMEMBER_STATUS_AFK;
            if (player->isDND())
                flags |= GUILDMEMBER_STATUS_DND;
        }

        memberData << uint8(member->GetClass());
        memberData << int32(player ? player->GetReputation(1168) : 0);
        memberData.WriteByteSeq(guid[0]);
        memberData << uint64(member->GetXPContribWeek());
        memberData << uint32(member->GetRankId());
        memberData << uint32(member->GetAchievementPoints());                                    // player->GetAchievementMgr().GetCompletedAchievementsAmount()

        for (uint8 i = 0; i < 2; ++i)
        {
            memberData << uint32(member->GetProfession(i).rank);
            memberData << uint32(member->GetProfession(i).value);
            memberData << uint32(member->GetProfession(i).skillId);
        }

        memberData.WriteByteSeq(guid[2]);
        memberData << uint8(flags);
        memberData << uint32(player ? player->GetZoneId() : member->GetZone());
        memberData << uint64(member->GetXPContrib());
        memberData.WriteByteSeq(guid[7]);
        memberData << uint32(weeklyRepCap - member->GetWeeklyReputation()); // Remaining guild week Rep

        memberData << WriteBuffer(member->GetPublicNote().c_str(), pubNoteLength);

        memberData.WriteByteSeq(guid[3]);
        memberData << uint8(player ? player->getLevel() : member->GetLevel());
        memberData << int32(0);                                     // unk
        memberData.WriteByteSeq(guid[5]);
        memberData.WriteByteSeq(guid[4]);
        memberData << uint8(player ? player->getGender() : 0);
        memberData.WriteByteSeq(guid[1]);
        memberData << float(player ? 0.0f : float(float(now - member->GetLogoutTime()) / DAY));

        memberData << WriteBuffer(member->GetOfficerNote().c_str(), offNoteLength);
        memberData.WriteByteSeq(guid[6]);
        memberData << WriteBuffer(member->GetName().c_str(), nameSize);
    }

    data.WriteBits(infoLength, 12);

    data.append(memberData);

    data << WriteBuffer(m_info.c_str(), infoLength);
    data << WriteBuffer(m_motd.c_str(), motdSize);
    data << uint32(m_accountsNumber);
    data << uint32(weeklyRepCap);
    data.AppendPackedTime(m_createdDate);
    data << uint32(0);

    if (session)
        session->SendPacket(&data);
    else
        BroadcastPacket(&data);
}

void Guild::HandleQuery(WorldSession* session)
{
    WorldPacket data(SMSG_GUILD_QUERY_RESPONSE, 8 * 32 + 200);      // Guess size

    data << uint64(GetGUID());
    data << m_name;

    // Rank name
    for (uint8 i = 0; i < GUILD_RANKS_MAX_COUNT; ++i)               // Always show 10 ranks
    {
        if (i < _GetRanksSize())
            data << m_ranks[i].GetName();
        else
            data << uint8(0);                                       // Empty string
    }

    // Rank order of creation
    for (uint8 i = 0; i < GUILD_RANKS_MAX_COUNT; ++i)
    {
        if (i < _GetRanksSize())
            data << uint32(i);
        else
            data << uint32(0);
    }

    // Rank order of "importance" (sorting by rights)
    for (uint8 i = 0; i < GUILD_RANKS_MAX_COUNT; ++i)
    {
        if (i < _GetRanksSize())
            data << uint32(m_ranks[i].GetId());
        else
            data << uint32(0);
    }

    m_emblemInfo.WritePacket(data);

    data << uint32(_GetRanksSize());                                // Number of ranks used

    session->SendPacket(&data);
}

void Guild::HandleGuildRanks(WorldSession* session) const
{
    // perhaps move to guild.cpp.....
    ByteBuffer rankData(100);
    WorldPacket data(SMSG_GUILD_RANK, 100);

    data.WriteBits(_GetRanksSize(), 18);

    for (uint8 i = 0; i < _GetRanksSize(); i++)
    {
        RankInfo const* rankInfo = GetRankInfo(i);
        if (!rankInfo)
            continue;

        uint32 nameSize = uint32(rankInfo->GetName().length());

        data << WriteAsUnaligned<7>(nameSize);
        rankData << uint32(i);

        for (uint8 j = 0; j < GUILD_BANK_MAX_TABS; ++j)
        {
            rankData << uint32(rankInfo->GetBankTabSlotsPerDay(j));
            rankData << uint32(rankInfo->GetBankTabRights(j));
        }

        rankData << uint32(rankInfo->GetBankMoneyPerDay() / 10000);
        rankData << uint32(rankInfo->GetRights());
        rankData << WriteBuffer(rankInfo->GetName().c_str(), nameSize);

        rankData << uint32(rankInfo->GetId());
    }

    data.append(rankData);
    session->SendPacket(&data);
}

void Guild::HandleSetMOTD(WorldSession* session, const std::string& motd)
{
    if (m_motd == motd)
        return;

    // Player must have rights to set MOTD
    if (!_HasRankRight(session->GetPlayer(), GR_RIGHT_SETMOTD))
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    else
    {
        m_motd = motd;

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MOTD);
        stmt->setString(0, motd);
        stmt->setUInt32(1, m_id);
        CharacterDatabase.Execute(stmt);

        _BroadcastEvent(GE_MOTD, 0, motd.c_str());
    }
}

void Guild::HandleSetInfo(WorldSession* session, const std::string& info)
{
    if (m_info == info)
        return;

    // Player must have rights to set guild's info
    if (!_HasRankRight(session->GetPlayer(), GR_RIGHT_MODIFY_GUILD_INFO))
        SendCommandResult(session, GUILD_CREATE_S, ERR_GUILD_PERMISSIONS);
    else
    {
        m_info = info;

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_INFO);
        stmt->setString(0, info);
        stmt->setUInt32(1, m_id);
        CharacterDatabase.Execute(stmt);
    }
}

void Guild::HandleSetEmblem(WorldSession* session, const EmblemInfo& emblemInfo)
{
    Player* player = session->GetPlayer();
    if (!_IsLeader(player))
        // "Only guild leaders can create emblems."
        SendSaveEmblemResult(session, ERR_GUILDEMBLEM_NOTGUILDMASTER);
    else if (!player->HasEnoughMoney(uint64(EMBLEM_PRICE)))
        // "You can't afford to do that."
        SendSaveEmblemResult(session, ERR_GUILDEMBLEM_NOTENOUGHMONEY);
    else
    {
        player->ModifyMoney(-int64(EMBLEM_PRICE));

        m_emblemInfo = emblemInfo;
        m_emblemInfo.SaveToDB(m_id);

        // "Guild Emblem saved."
        SendSaveEmblemResult(session, ERR_GUILDEMBLEM_SUCCESS );

        HandleQuery(session);

        m_achievementMgr.UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BUY_GUILD_TABARD, 1, 0, 0, NULL, player);
    }
}

void Guild::HandleSetNewGuildMaster(WorldSession* session, std::string& name)
{
    Player* player = session->GetPlayer();
    // Only the guild master can throne a new guild master
    if (!_IsLeader(player))
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    // Old GM must be a guild member	
    else if (Member* oldGuildMaster = GetMember(player->GetGUID()))
    {
        // Same for the new one
        if (Member* newGuildMaster = GetMember(name))
        {
            _SetLeaderGUID(newGuildMaster);
            oldGuildMaster->ChangeRank(GR_INITIATE);
            _BroadcastEvent(GE_LEADER_CHANGED, 0, player->GetName(), name.c_str());
        }
    }
    else
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
}

void Guild::HandleSetBankTabInfo(WorldSession* session, uint8 tabId, const std::string& name, const std::string& icon)
{
    if (BankTab* pTab = GetBankTab(tabId))
    {
        pTab->SetInfo(name, icon);
        SendBankList(session, tabId, true, true);
    }
}

void Guild::HandleSetMemberNote(WorldSession* session, std::string const& note, uint64 guid, bool isPublic)
{
    // Player must have rights to set public/officer note
    if (!_HasRankRight(session->GetPlayer(), isPublic ? GR_RIGHT_EPNOTE : GR_RIGHT_EOFFNOTE))
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    else if (Member* member = GetMember(guid))
    {
        ObjectGuid playerGuid;

        if (isPublic)
            member->SetPublicNote(note);
        else
            member->SetOfficerNote(note);
        HandleRoster(session);

        WorldPacket data(SMSG_GUILD_MEMBER_UPDATE_NOTE);

        data.WriteBit(playerGuid[7]);
        data.WriteBit(playerGuid[2]);
        data.WriteBit(playerGuid[3]);

        data.WriteBits(note.size(),8);

        data.WriteBit(playerGuid[5]);
        data.WriteBit(playerGuid[0]);
        data.WriteBit(playerGuid[6]);
        data.WriteBit(playerGuid[4]);
        data.WriteBit(isPublic);
        data.WriteBit(playerGuid[1]);

        data.WriteByteSeq(playerGuid[3]);
        data.WriteByteSeq(playerGuid[0]);
        data.WriteByteSeq(playerGuid[5]);
        data.WriteByteSeq(playerGuid[2]);

        data.WriteString(note);

        data.WriteByteSeq(playerGuid[7]);
        data.WriteByteSeq(playerGuid[6]);
        data.WriteByteSeq(playerGuid[1]);
        data.WriteByteSeq(playerGuid[4]);

        session->SendPacket(&data);
    }
}

void Guild::HandleSetRankInfo(WorldSession* session, uint32 rankId, const std::string& name, uint32 rights, uint32 moneyPerDay, GuildBankRightsAndSlotsVec rightsAndSlots)
{
    // Only leader can modify ranks
    if (!_IsLeader(session->GetPlayer()))
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    else if (RankInfo* rankInfo = GetRankInfo(rankId))
    {
        rankInfo->SetName(name);
        rankInfo->SetRights(rights);
        _SetRankBankMoneyPerDay(rankId, moneyPerDay);

        uint8 tabId = 0;
        for (GuildBankRightsAndSlotsVec::const_iterator itr = rightsAndSlots.begin(); itr != rightsAndSlots.end(); ++itr)
            _SetRankBankTabRightsAndSlots(rankId, tabId++, *itr);

        HandleQuery(session);
        HandleRoster();                                     // Broadcast for tab rights update
        HandleGuildRanks(session);
    }
}

void Guild::HandleBuyBankTab(WorldSession* session, uint8 tabId)
{
    if (tabId != _GetPurchasedTabsSize())
        return;

    uint32 tabCost = _GetGuildBankTabPrice(tabId) * GOLD;
    if (!tabCost && tabId != 6 && tabId != 7)
        return;

    Player* player = session->GetPlayer();
    if (!player->HasEnoughMoney(uint64(tabCost)))                   // Should not happen, this is checked by client
        return;

    if (!_CreateNewBankTab())
        return;

    player->ModifyMoney(-int64(tabCost));
    _SetRankBankMoneyPerDay(player->GetRank(), uint32(GUILD_WITHDRAW_MONEY_UNLIMITED));
    _SetRankBankTabRightsAndSlots(player->GetRank(), tabId, GuildBankRightsAndSlots(GUILD_BANK_RIGHT_FULL, uint32(GUILD_WITHDRAW_SLOT_UNLIMITED)));
    HandleRoster();                                         // Broadcast for tab rights update
    SendBankList(session, tabId, false, true);
    m_achievementMgr.UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BUY_GUILD_BANK_SLOTS, _GetPurchasedTabsSize(), 0, 0, NULL, player);
}

void Guild::HandleInviteMember(WorldSession* session, const std::string& name)
{
    Player* pInvitee = sObjectAccessor->FindPlayerByName(name);
    if (!pInvitee)
    {
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PLAYER_NOT_FOUND_S, name);
        return;
    }

    Player* player = session->GetPlayer();
    // Do not show invitations from ignored players
    if (pInvitee->GetSocial()->HasIgnore(player->GetGUIDLow()))
        return;

    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) && pInvitee->GetTeam() != player->GetTeam())
    {
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_NOT_ALLIED, name);
        return;
    }
    // Invited player cannot be in another guild
    if (pInvitee->GetGuildId())
    {
        SendCommandResult(session, GUILD_INVITE_S, ERR_ALREADY_IN_GUILD_S, name);
        return;
    }
    // Invited player cannot be invited
    if (pInvitee->GetGuildIdInvited())
    {
        SendCommandResult(session, GUILD_INVITE_S, ERR_ALREADY_INVITED_TO_GUILD_S, name);
        return;
    }
    // Inviting player must have rights to invite
    if (!_HasRankRight(player, GR_RIGHT_INVITE))
    {
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
        return;
    }

    pInvitee->SetGuildIdInvited(m_id);
    _LogEvent(GUILD_EVENT_LOG_INVITE_PLAYER, player->GetGUIDLow(), pInvitee->GetGUIDLow());

    WorldPacket data(SMSG_GUILD_INVITE, 100);
    data << uint32(GetLevel());
    data << uint32(m_emblemInfo.GetBorderStyle());
    data << uint32(m_emblemInfo.GetBorderColor());
    data << uint32(m_emblemInfo.GetStyle());
    data << uint32(m_emblemInfo.GetBackgroundColor());
    data << uint32(m_emblemInfo.GetColor());

    ObjectGuid oldGuildGuid = MAKE_NEW_GUID(pInvitee->GetGuildId(), 0, pInvitee->GetGuildId() ? uint32(HIGHGUID_GUILD) : 0);
    ObjectGuid newGuildGuid = GetGUID();

    uint32 guildNameSize = uint32(pInvitee->GetGuildName().length());
    uint32 m_nameSize = uint32(m_name.length());
    uint32 playerNameSize = uint32(player->GetNameLength());
    
    data.WriteByteMask(newGuildGuid[3]);
    data.WriteByteMask(newGuildGuid[2]);
    data.WriteBits(guildNameSize, 8);
    data.WriteByteMask(newGuildGuid[1]);
    data.WriteByteMask(oldGuildGuid[6]);
    data.WriteByteMask(oldGuildGuid[4]);
    data.WriteByteMask(oldGuildGuid[1]);
    data.WriteByteMask(oldGuildGuid[5]);
    data.WriteByteMask(oldGuildGuid[7]);
    data.WriteByteMask(oldGuildGuid[2]);
    data.WriteByteMask(newGuildGuid[7]);
    data.WriteByteMask(newGuildGuid[0]);
    data.WriteByteMask(newGuildGuid[6]);
    data.WriteBits(m_nameSize, 8);
    data.WriteByteMask(oldGuildGuid[3]);
    data.WriteByteMask(oldGuildGuid[0]);
    data.WriteByteMask(newGuildGuid[5]);
    data.WriteBits(playerNameSize, 7);
    data.WriteByteMask(newGuildGuid[4]);

    data.WriteByteSeq(newGuildGuid[1]);
    data.WriteByteSeq(oldGuildGuid[3]);
    data.WriteByteSeq(newGuildGuid[6]);
    data.WriteByteSeq(oldGuildGuid[2]);
    data.WriteByteSeq(oldGuildGuid[1]);
    data.WriteByteSeq(newGuildGuid[0]);

    data << WriteBuffer(pInvitee->GetGuildName().c_str(), guildNameSize);

    data.WriteByteSeq(newGuildGuid[7]);
    data.WriteByteSeq(newGuildGuid[2]);

    data << WriteBuffer(player->GetName(), playerNameSize);

    data.WriteByteSeq(oldGuildGuid[7]);
    data.WriteByteSeq(oldGuildGuid[6]);
    data.WriteByteSeq(oldGuildGuid[5]);
    data.WriteByteSeq(oldGuildGuid[0]);
    data.WriteByteSeq(newGuildGuid[4]);

    data << WriteBuffer(m_name.c_str(), m_nameSize);

    data.WriteByteSeq(newGuildGuid[5]);
    data.WriteByteSeq(newGuildGuid[3]);
    data.WriteByteSeq(oldGuildGuid[4]);
    pInvitee->GetSession()->SendPacket(&data);
}

void Guild::HandleAcceptMember(WorldSession* session)
{
    Player* player = session->GetPlayer();
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD) &&
        player->GetTeam() != sObjectMgr->GetPlayerTeamByGUID(GetLeaderGUID()))
        return;

    if (AddMember(player->GetGUID()))
    {
        _LogEvent(GUILD_EVENT_LOG_JOIN_GUILD, player->GetGUIDLow());
        _BroadcastEvent(GE_JOINED, player->GetGUID(), player->GetName());
        player->SetReputation(1168, 1);
        sGuildFinderMgr->RemoveMembershipRequest(player->GetGUIDLow(), GUID_LOPART(this->GetGUID()));
    }
}

void Guild::HandleLeaveMember(WorldSession* session)
{
    Player* player = session->GetPlayer();
    // If leader is leaving
    if (_IsLeader(player))
    {
        if (m_members.size() > 1)
            // Leader cannot leave if he is not the last member
            SendCommandResult(session, GUILD_QUIT_S, ERR_GUILD_LEADER_LEAVE);
        else if (GetLevel() >= sWorld->getIntConfig(CONFIG_GUILD_UNDELETABLE_LEVEL))
            SendCommandResult(session, GUILD_QUIT_S, ERR_GUILD_UNDELETABLE_DUE_TO_LEVEL);
        else
            // Guild is disbanded if leader leaves.
            Disband();
    }
    else
    {
        DeleteMember(player->GetGUID(), false, false);

        _LogEvent(GUILD_EVENT_LOG_LEAVE_GUILD, player->GetGUIDLow());
        _BroadcastEvent(GE_LEFT, player->GetGUID(), player->GetName());

        SendCommandResult(session, GUILD_QUIT_S, ERR_GUILD_COMMAND_SUCCESS, m_name);
    }

    sCalendarMgr->RemovePlayerGuildEventsAndSignups(player->GetGUID(), GetId());
}

void Guild::HandleRemoveMember(WorldSession* session, uint64 guid)
{
    Player* player = session->GetPlayer();

    // Player must have rights to remove members
    if (!_HasRankRight(player, GR_RIGHT_REMOVE))
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    // Removed player must be a member of the guild
    else if (Member* member = GetMember(guid))
    {
        // Guild masters cannot be removed
        if (member->IsRank(GR_GUILDMASTER))
            SendCommandResult(session, GUILD_QUIT_S, ERR_GUILD_LEADER_LEAVE);
        // Do not allow to remove player with the same rank or higher
        else if (member->IsRankNotLower(player->GetRank()))
            SendCommandResult(session, GUILD_QUIT_S, ERR_GUILD_RANK_TOO_HIGH_S, member->GetName());
        else
        {
            // After call to DeleteMember pointer to member becomes invalid
            _LogEvent(GUILD_EVENT_LOG_UNINVITE_PLAYER, player->GetGUIDLow(), GUID_LOPART(guid));
            _BroadcastEvent(GE_REMOVED, 0, member->GetName().c_str(), player->GetName());
            DeleteMember(guid, false, true);
        }
    }
    //else if (removedPlayer)
        //SendCommandResult(session, GUILD_QUIT_S, ERR_GUILD_COMMAND_SUCCESS, removedPlayer->GetName());
}

void Guild::HandleUpdateMemberRank(WorldSession* session, uint64 targetGuid, bool demote)
{
    Player* player = session->GetPlayer();

    // Promoted player must be a member of guild
    if (Member* member = GetMember(targetGuid))
    {
        if (!_HasRankRight(player, demote ? GR_RIGHT_DEMOTE : GR_RIGHT_PROMOTE))
        {
            SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
            return;
        }

        // Player cannot promote himself
        if (member->IsSamePlayer(player->GetGUID()))
        {
            SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_NAME_INVALID);
            return;
        }

        if (demote)
        {
            // Player can demote only lower rank members
            if (member->IsRankNotLower(player->GetRank()))
            {
                SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_RANK_TOO_HIGH_S, member->GetName());
                return;
            }
            // Lowest rank cannot be demoted
            if (member->GetRankId() >= _GetLowestRankId())
            {
                SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_RANK_TOO_LOW_S, member->GetName());
                return;
            }
        }
        else
        {
            // Allow to promote only to lower rank than member's rank
            // member->GetRank() + 1 is the highest rank that current player can promote to
            if (member->IsRankNotLower(player->GetRank() + 1))
            {
                SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_RANK_TOO_HIGH_S, member->GetName());
                return;
            }
        }

        uint32 newRankId = member->GetRankId() + (demote ? 1 : -1);
        member->ChangeRank(newRankId);
        _LogEvent(demote ? GUILD_EVENT_LOG_DEMOTE_PLAYER : GUILD_EVENT_LOG_PROMOTE_PLAYER, player->GetGUIDLow(), GUID_LOPART(member->GetGUID()), newRankId);
        _BroadcastEvent(demote ? GE_DEMOTION : GE_PROMOTION, 0, player->GetName(), member->GetName().c_str(), _GetRankName(newRankId).c_str());
    }
}

void Guild::HandleSetMemberRank(WorldSession* session, uint64 targetGuid, uint64 setterGuid, uint32 rank)
{
    Player* player = session->GetPlayer();

    // Promoted player must be a member of guild
    if (Member* member = GetMember(targetGuid))
    {
        if (!_HasRankRight(player, rank > member->GetRankId() ? GR_RIGHT_DEMOTE : GR_RIGHT_PROMOTE))
        {
            SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
            return;
        }

        // Player cannot promote himself
        if (member->IsSamePlayer(player->GetGUID()))
        {
            SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_NAME_INVALID);
            return;
        }

        SendGuildRanksUpdate(setterGuid, targetGuid, rank);
    }
}

void Guild::HandleAddNewRank(WorldSession* session, std::string const& name) //, uint32 rankId)
{
    if (_GetRanksSize() >= GUILD_RANKS_MAX_COUNT)
        return;

    // Only leader can add new rank
    if (!_IsLeader(session->GetPlayer()))
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    else
    {
        _CreateRank(name, GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);
        HandleQuery(session);
        HandleRoster();                                             // Broadcast for tab rights update
        HandleGuildRanks(session);
    }
}

void Guild::HandleRemoveRank(WorldSession* session, uint32 rankId)
{
    // Cannot remove rank if total count is minimum allowed by the client
    if (_GetRanksSize() <= GUILD_RANKS_MIN_COUNT)
        return;

    // Only leader can delete ranks
    if (!_IsLeader(session->GetPlayer()))
        SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    else
    {
        // Delete bank rights for rank
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_RIGHTS_FOR_RANK);
        stmt->setUInt32(0, m_id);
        stmt->setUInt8(1, rankId);
        CharacterDatabase.Execute(stmt);
        // Delete rank
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_RANK);
        stmt->setUInt32(0, m_id);
        stmt->setUInt8(1, rankId);
        CharacterDatabase.Execute(stmt);

        m_ranks.erase(m_ranks.begin() + rankId);

        HandleQuery(session);
        HandleRoster();                                             // Broadcast for tab rights update
        HandleGuildRanks(session);
    }
}

void Guild::HandleMemberDepositMoney(WorldSession* session, uint32 amount, bool cashFlow /*=false*/)
{
    Player* player = session->GetPlayer();

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    // Add money to bank
    _ModifyBankMoney(trans, amount, true);
    // Remove money from player
    player->ModifyMoney(-int64(amount));
    player->SaveGoldToDB(trans);
    // Log GM action (TODO: move to scripts)
    if (!AccountMgr::IsPlayerAccount(player->GetSession()->GetSecurity()) && sWorld->getBoolConfig(CONFIG_GM_LOG_TRADE))
    {
        sLog->outCommand(player->GetSession()->GetAccountId(),
            "GM %s (Account: %u) deposit money (Amount: %u) to guild bank (Guild ID %u)",
            player->GetName(), player->GetSession()->GetAccountId(), amount, m_id);
    }
    // Log guild bank event
    _LogBankEvent(trans, cashFlow ? GUILD_BANK_LOG_CASH_FLOW_DEPOSIT : GUILD_BANK_LOG_DEPOSIT_MONEY, uint8(0), player->GetGUIDLow(), amount);

    CharacterDatabase.CommitTransaction(trans);

    if (!cashFlow)
        SendBankList(session, 0, false, false);
}

bool Guild::HandleMemberWithdrawMoney(WorldSession* session, uint32 amount, bool repair)
{
    if (m_bankMoney < amount)                               // Not enough money in bank
        return false;

    Player* player = session->GetPlayer();
    if (!_HasRankRight(player, repair ? GR_RIGHT_WITHDRAW_REPAIR : GR_RIGHT_WITHDRAW_GOLD))
        return false;

    uint32 remainingMoney = _GetMemberRemainingMoney(player->GetGUID());
    if (!remainingMoney)
        return false;

    if (remainingMoney < amount)
        return false;

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    // Update remaining money amount
    if (remainingMoney < uint32(GUILD_WITHDRAW_MONEY_UNLIMITED))
        if (Member* member = GetMember(player->GetGUID()))
            member->DecreaseBankRemainingValue(trans, GUILD_BANK_MAX_TABS, amount);
    // Remove money from bank
    _ModifyBankMoney(trans, amount, false);
    // Add money to player (if required)
    if (!repair)
    {
        player->ModifyMoney(amount);
        player->SaveGoldToDB(trans);
    }
    // Log guild bank event
    _LogBankEvent(trans, repair ? GUILD_BANK_LOG_REPAIR_MONEY : GUILD_BANK_LOG_WITHDRAW_MONEY, uint8(0), player->GetGUIDLow(), amount);
    CharacterDatabase.CommitTransaction(trans);

    SendMoneyInfo(session);
    if (!repair)
        SendBankList(session, 0, false, false);

    if (repair)
        m_achievementMgr.UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_SPENT_GOLD_GUILD_REPAIRS, amount, 0, 0, NULL, player);
    
    return true;
}

void Guild::HandleMemberLogout(WorldSession* session)
{
    Player* player = session->GetPlayer();
    if (Member* member = GetMember(player->GetGUID()))
    {
        member->SetStats(player);
        member->UpdateLogoutTime();
    }
    _BroadcastEvent(GE_SIGNED_OFF, player->GetGUID(), player->GetName());

    SaveToDB();
}

void Guild::HandleDisband(WorldSession* session)
{
    // Only leader can disband guild
    if (!_IsLeader(session->GetPlayer()))
        Guild::SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_PERMISSIONS);
    else if (GetLevel() >= sWorld->getIntConfig(CONFIG_GUILD_UNDELETABLE_LEVEL))
        Guild::SendCommandResult(session, GUILD_INVITE_S, ERR_GUILD_UNDELETABLE_DUE_TO_LEVEL);
    else
        Disband();
}

void Guild::HandleGuildPartyRequest(WorldSession* session)
{
    Player* player = session->GetPlayer();
    Group* group = player->GetGroup();

    // Make sure player is a member of the guild and that he is in a group.
    if (!IsMember(player->GetGUID()) || !group)
        return;

    WorldPacket data(SMSG_GUILD_PARTY_STATE_RESPONSE, 13);
    data.WriteBit(player->GetMap()->GetOwnerGuildId(player->GetTeam()) == GetId()); // Is guild group
    data << float(0.f);                                                             // Guild XP multiplier
    data << uint32(0);                                                              // Current guild members
    data << uint32(0);                                                              // Needed guild members

    session->SendPacket(&data);
}

void Guild::SendEventLog(WorldSession* session) const
{
    WorldPacket data(SMSG_GUILD_EVENT_LOG_QUERY_RESULT, 1 + m_eventLog->GetSize() * (1 + 8 + 4));
    m_eventLog->WritePacket(data);
    session->SendPacket(&data);
}

void Guild::SendBankLog(WorldSession* session, uint8 tabId) const
{
    // GUILD_BANK_MAX_TABS send by client for money log
    if (tabId < _GetPurchasedTabsSize() || tabId == GUILD_BANK_MAX_TABS)
    {
        LogHolder const* log = m_bankEventLog[tabId];
        WorldPacket data(SMSG_GUILD_BANK_LOG_QUERY_RESULT, log->GetSize() * (4 * 4 + 1) + 1 + 1);
        data.WriteBit(GetLevel() >= 5 && tabId == GUILD_BANK_MAX_TABS);     // has Cash Flow perk
        log->WritePacket(data);
        data << uint32(tabId);
        //if (tabId == GUILD_BANK_MAX_TABS && hasCashFlow)
        //    data << uint64(cashFlowContribution);
        session->SendPacket(&data);
    }
}

void Guild::SendBankList(WorldSession* session, uint8 tabId, bool withContent, bool withTabInfo) const
{
    ByteBuffer tabData;
    WorldPacket data(SMSG_GUILD_BANK_LIST, 500);
    data.WriteBit(false);
    uint32 itemCount = 0;
    if (withContent && _MemberHasTabRights(session->GetPlayer()->GetGUID(), tabId, GUILD_BANK_RIGHT_VIEW_TAB))
        if (BankTab const* tab = GetBankTab(tabId))
            for (uint8 slotId = 0; slotId < GUILD_BANK_MAX_SLOTS; ++slotId)
                if (tab->GetItem(slotId))
                    ++itemCount;

    data.WriteBits(itemCount, 20);
    data.WriteBits(withTabInfo ? _GetPurchasedTabsSize() : 0, 22);
    if (withContent && _MemberHasTabRights(session->GetPlayer()->GetGUID(), tabId, GUILD_BANK_RIGHT_VIEW_TAB))
    {
        if (BankTab const* tab = GetBankTab(tabId))
        {
            for (uint8 slotId = 0; slotId < GUILD_BANK_MAX_SLOTS; ++slotId)
            {
                if (Item* tabItem = tab->GetItem(slotId))
                {
                    data.WriteBit(false);

                    uint32 enchants = 0;
                    for (uint32 i = 0; i < MAX_GEM_SOCKETS; ++i)
                    {
                        if (uint32 enchantId = tabItem->GetEnchantmentId(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + i)))
                        {
                            tabData << uint32(enchantId) << uint32(i);
                            ++enchants;
                        }
                    }

                    data.WriteBits(enchants, 23);

                    tabData << uint32(0);
                    tabData << uint32(0);
                    tabData << uint32(0);
                    tabData << uint32(tabItem->GetCount());                 // ITEM_FIELD_STACK_COUNT
                    tabData << uint32(slotId);
                    tabData << uint32(0);
                    tabData << uint32(tabItem->GetEntry());
                    tabData << uint32(tabItem->GetItemRandomPropertyId());
                    tabData << uint32(abs(tabItem->GetSpellCharges()));     // Spell charges
                    tabData << uint32(tabItem->GetItemSuffixFactor());      // SuffixFactor
                }
            }
        }
    }

    if (withTabInfo)
    {
        for (uint8 i = 0; i < _GetPurchasedTabsSize(); ++i)
        {
            data.WriteBits(m_bankTabs[i]->GetIcon().length(), 9);
            data.WriteBits(m_bankTabs[i]->GetName().length(), 7);
        }

        for (uint8 i = 0; i < _GetPurchasedTabsSize(); ++i)
        {
            data
                << WriteBuffer(m_bankTabs[i]->GetIcon().c_str(), m_bankTabs[i]->GetIcon().length())
                << uint32(i)
                << WriteBuffer(m_bankTabs[i]->GetName().c_str(), m_bankTabs[i]->GetName().length());
        }
    }

    data << uint64(m_bankMoney);
    if (!tabData.empty())
        data.append(tabData);

    data << uint32(tabId);
    data << uint32(_GetMemberRemainingSlots(session->GetPlayer()->GetGUID(), 0));

    session->SendPacket(&data);
}

void Guild::SendBankTabText(WorldSession* session, uint8 tabId) const
{
    if (BankTab const* tab = GetBankTab(tabId))
        tab->SendText(this, session);
}

void Guild::SendPermissions(WorldSession* session) const
{
    uint64 guid = session->GetPlayer()->GetGUID();
    uint32 rankId = session->GetPlayer()->GetRank();
    WorldPacket data(SMSG_GUILD_PERMISSIONS_QUERY_RESULTS, 4 * 15 + 1);
    data << uint32(rankId);
    data << uint32(_GetPurchasedTabsSize());
    data << uint32(_GetRankRights(rankId));
    data << uint32(_GetMemberRemainingMoney(guid));
    data.WriteBits(GUILD_BANK_MAX_TABS, 23);
    for (uint8 tabId = 0; tabId < GUILD_BANK_MAX_TABS; ++tabId)
    {
        data << uint32(_GetRankBankTabRights(rankId, tabId));
        data << uint32(_GetMemberRemainingSlots(guid, tabId));
    }

    session->SendPacket(&data);
}

void Guild::SendMoneyInfo(WorldSession* session) const
{
    WorldPacket data(SMSG_GUILD_BANK_MONEY_WITHDRAWN, 4);
    data << uint64(_GetMemberRemainingMoney(session->GetPlayer()->GetGUID()));
    session->SendPacket(&data);
}

void Guild::SendLoginInfo(WorldSession* session) const
{
    /*
        Login sequence:
          SMSG_GUILD_EVENT - GE_MOTD
          SMSG_GUILD_RANK
          SMSG_GUILD_EVENT - GE_SIGNED_ON
          -- learn perks
          SMSG_GUILD_REPUTATION_WEEKLY_CAP
          SMSG_GUILD_ACHIEVEMENT_DATA
          SMSG_GUILD_MEMBER_DAILY_RESET // bank withdrawal reset
    */

    WorldPacket data(SMSG_GUILD_EVENT, 1 + 1 + m_motd.size() + 1);
    data << uint8(GE_MOTD);
    data << uint8(1);
    data << m_motd;
    session->SendPacket(&data);

    HandleGuildRanks(session);

    _BroadcastEvent(GE_SIGNED_ON, session->GetPlayer()->GetGUID(), session->GetPlayer()->GetName());

    // Send to self separately, player is not in world yet and is not found by _BroadcastEvent
    data.Initialize(SMSG_GUILD_EVENT, 1 + 1 + strlen(session->GetPlayer()->GetName()) + 8);
    data << uint8(GE_SIGNED_ON);
    data << uint8(1);
    data << session->GetPlayer()->GetName();
    data << uint64(session->GetPlayer()->GetGUID());
    session->SendPacket(&data);

    data.Initialize(SMSG_GUILD_MEMBER_DAILY_RESET, 0);  // tells the client to request bank withdrawal limit
    session->SendPacket(&data);

    if (!sWorld->getBoolConfig(CONFIG_GUILD_LEVELING_ENABLED))
        return;

    for (uint32 i = 0; i < sGuildPerkSpellsStore.GetNumRows(); ++i)
        if (GuildPerkSpellsEntry const* entry = sGuildPerkSpellsStore.LookupEntry(i))
            if (entry->Level <= GetLevel())
                session->GetPlayer()->learnSpell(entry->SpellId, true);

    SendGuildReputationWeeklyCap(session);

    GetAchievementMgr().SendAllAchievementData(session->GetPlayer());
}

void Guild::SendGuildReputationWeeklyCap(WorldSession* session) const
{
    if (Member const* member = GetMember(session->GetPlayer()->GetGUID()))
    {
        WorldPacket data(SMSG_GUILD_REPUTATION_WEEKLY_CAP, 4);
        data << uint32(sWorld->getIntConfig(CONFIG_GUILD_WEEKLY_REP_CAP) - member->GetWeeklyReputation());
        session->SendPacket(&data);
    }
}

// Loading methods
bool Guild::LoadFromDB(Field* fields)
{
    m_id            = fields[0].GetUInt32();
    m_name          = fields[1].GetString();
    m_leaderGuid    = MAKE_NEW_GUID(fields[2].GetUInt32(), 0, HIGHGUID_PLAYER);
    m_emblemInfo.LoadFromDB(fields);
    m_info          = fields[8].GetString();
    m_motd          = fields[9].GetString();
    m_createdDate   = time_t(fields[10].GetUInt32());
    m_bankMoney     = fields[11].GetUInt64();
    _level          = fields[12].GetUInt32();
    _experience     = fields[13].GetUInt64();
    _todayExperience = fields[14].GetUInt64();

    uint8 purchasedTabs = uint8(fields[15].GetUInt64());
    if (purchasedTabs > GUILD_BANK_MAX_TABS)
        purchasedTabs = GUILD_BANK_MAX_TABS;

    GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_REACH_GUILD_LEVEL, _level);
    GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BUY_GUILD_BANK_SLOTS, purchasedTabs);



    m_bankTabs.resize(purchasedTabs);
    for (uint8 i = 0; i < purchasedTabs; ++i)
        m_bankTabs[i] = new BankTab(m_id, i);

    _CreateLogHolders();
    return true;
}

void Guild::LoadRankFromDB(Field* fields)
{
    RankInfo rankInfo(m_id);

    rankInfo.LoadFromDB(fields);

    m_ranks.push_back(rankInfo);
}

bool Guild::LoadMemberFromDB(Field* fields)
{
    uint32 lowguid = fields[1].GetUInt32();
    Member *member = new Member(m_id, MAKE_NEW_GUID(lowguid, 0, HIGHGUID_PLAYER), fields[2].GetUInt8());
    if (!member->LoadFromDB(fields, lowguid))
    {
        _DeleteMemberFromDB(lowguid);
        delete member;
        return false;
    }
    m_members[lowguid] = member;
    return true;
}

void Guild::LoadBankRightFromDB(Field* fields)
{
                                           // rights             slots
    GuildBankRightsAndSlots rightsAndSlots(fields[3].GetUInt8(), fields[4].GetUInt32());
                                  // rankId             tabId
    _SetRankBankTabRightsAndSlots(fields[2].GetUInt8(), fields[1].GetUInt8(), rightsAndSlots, false);
}

bool Guild::LoadEventLogFromDB(Field* fields)
{
    if (m_eventLog->CanInsert())
    {
        m_eventLog->LoadEvent(new EventLogEntry(
            m_id,                                       // guild id
            fields[1].GetUInt32(),                      // guid
            time_t(fields[6].GetUInt32()),              // timestamp
            GuildEventLogTypes(fields[2].GetUInt8()),   // event type
            fields[3].GetUInt32(),                      // player guid 1
            fields[4].GetUInt32(),                      // player guid 2
            fields[5].GetUInt8()));                     // rank
        return true;
    }
    return false;
}

bool Guild::LoadBankEventLogFromDB(Field* fields)
{
    uint8 dbTabId = fields[1].GetUInt8();
    bool isMoneyTab = (dbTabId == GUILD_BANK_MONEY_LOGS_TAB);
    if (dbTabId < _GetPurchasedTabsSize() || isMoneyTab)
    {
        uint8 tabId = isMoneyTab ? uint8(GUILD_BANK_MAX_TABS) : dbTabId;
        LogHolder* pLog = m_bankEventLog[tabId];
        if (pLog->CanInsert())
        {
            uint32 guid = fields[2].GetUInt32();
            GuildBankEventLogTypes eventType = GuildBankEventLogTypes(fields[3].GetUInt8());
            if (BankEventLogEntry::IsMoneyEvent(eventType))
            {
                if (!isMoneyTab)
                    return false;
            }
            else if (isMoneyTab)
            {
                return false;
            }
            pLog->LoadEvent(new BankEventLogEntry(
                m_id,                                   // guild id
                guid,                                   // guid
                time_t(fields[8].GetUInt32()),          // timestamp
                dbTabId,                                // tab id
                eventType,                              // event type
                fields[4].GetUInt32(),                  // player guid
                fields[5].GetUInt32(),                  // item or money
                fields[6].GetUInt16(),                  // itam stack count
                fields[7].GetUInt8()));                 // dest tab id
        }
    }
    return true;
}

bool Guild::LoadBankTabFromDB(Field* fields)
{
    uint8 tabId = fields[1].GetUInt8();
    if (tabId >= _GetPurchasedTabsSize())
        return false;

    return m_bankTabs[tabId]->LoadFromDB(fields);
}

bool Guild::LoadBankItemFromDB(Field* fields)
{
    uint8 tabId = fields[12].GetUInt8();
    if (tabId >= _GetPurchasedTabsSize())
        return false;

    return m_bankTabs[tabId]->LoadItemFromDB(fields);
}

// Validates guild data loaded from database. Returns false if guild should be deleted.
bool Guild::Validate()
{
    // Validate ranks data
    // GUILD RANKS represent a sequence starting from 0 = GUILD_MASTER (ALL PRIVILEGES) to max 9 (lowest privileges).
    // The lower rank id is considered higher rank - so promotion does rank-- and demotion does rank++
    // Between ranks in sequence cannot be gaps - so 0, 1, 2, 4 is impossible
    // Min ranks count is 5 and max is 10.
    bool broken_ranks = false;
    if (_GetRanksSize() < GUILD_RANKS_MIN_COUNT || _GetRanksSize() > GUILD_RANKS_MAX_COUNT)
    {
        broken_ranks = true;
    }
    else
    {
        for (uint8 rankId = 0; rankId < _GetRanksSize(); ++rankId)
        {
            RankInfo* rankInfo = GetRankInfo(rankId);
            if (rankInfo->GetId() != rankId)
                broken_ranks = true;
        }
    }

    if (broken_ranks)
    {
        m_ranks.clear();
        _CreateDefaultGuildRanks(DEFAULT_LOCALE);
    }

    // Validate members' data
    for (Members::iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        if (itr->second->GetRankId() > _GetRanksSize())
            itr->second->ChangeRank(_GetLowestRankId());

    // Repair the structure of the guild.
    // If the guildmaster doesn't exist or isn't member of the guild
    // attempt to promote another member.
    Member* pLeader = GetMember(m_leaderGuid);
    if (!pLeader)
    {
        DeleteMember(m_leaderGuid);
        // If no more members left, disband guild
        if (m_members.empty())
        {
            Disband();
            return false;
        }
    }
    else if (!pLeader->IsRank(GR_GUILDMASTER))
        _SetLeaderGUID(pLeader);

    // Check config if multiple guildmasters are allowed
    if (!ConfigMgr::GetBoolDefault("Guild.AllowMultipleGuildMaster", 0))
        for (Members::iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
            if (itr->second->GetRankId() == GR_GUILDMASTER && !itr->second->IsSamePlayer(m_leaderGuid))
                itr->second->ChangeRank(GR_OFFICER);

    _UpdateAccountsNumber();
    return true;
}

// Broadcasts
void Guild::BroadcastToGuild(WorldSession* session, bool officerOnly, const std::string& msg, uint32 language) const
{
    if (session && session->GetPlayer() && _HasRankRight(session->GetPlayer(), officerOnly ? GR_RIGHT_OFFCHATSPEAK : GR_RIGHT_GCHATSPEAK))
    {
        WorldPacket data;
        ChatHandler::FillMessageData(&data, session, officerOnly ? CHAT_MSG_OFFICER : CHAT_MSG_GUILD, language, NULL, 0, msg.c_str(), NULL);
        for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
            if (Player* player = itr->second->FindPlayer())
                if (player->GetSession() && _HasRankRight(player, officerOnly ? GR_RIGHT_OFFCHATLISTEN : GR_RIGHT_GCHATLISTEN) &&
                    !player->GetSocial()->HasIgnore(session->GetPlayer()->GetGUIDLow()))
                    player->GetSession()->SendPacket(&data);
    }
}

void Guild::BroadcastAddonToGuild(WorldSession* session, bool officerOnly, const std::string& msg, const std::string& prefix) const
{
    if (session && session->GetPlayer() && _HasRankRight(session->GetPlayer(), officerOnly ? GR_RIGHT_OFFCHATSPEAK : GR_RIGHT_GCHATSPEAK))
    {
        WorldPacket data;
        ChatHandler::FillMessageData(&data, session, officerOnly ? CHAT_MSG_OFFICER : CHAT_MSG_GUILD, CHAT_MSG_ADDON, NULL, 0, msg.c_str(), NULL, prefix.c_str());
        for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
            if (Player* player = itr->second->FindPlayer())
                if (player->GetSession() && _HasRankRight(player, officerOnly ? GR_RIGHT_OFFCHATLISTEN : GR_RIGHT_GCHATLISTEN) &&
                    !player->GetSocial()->HasIgnore(session->GetPlayer()->GetGUIDLow()) &&
                    player->GetSession()->IsAddonRegistered(prefix))
                        player->GetSession()->SendPacket(&data);
    }
}

void Guild::BroadcastPacketToRank(WorldPacket* packet, uint8 rankId) const
{
    for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        if (itr->second->IsRank(rankId))
            if (Player* player = itr->second->FindPlayer())
                player->GetSession()->SendPacket(packet);
}

void Guild::BroadcastPacket(WorldPacket* packet) const
{
    for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        if (Player* player = itr->second->FindPlayer())
            player->GetSession()->SendPacket(packet);
}

void Guild::MassInviteToEvent(WorldSession* session, uint32 minLevel, uint32 maxLevel, uint32 minRank)
{
    uint32 count = 0;

    WorldPacket data(SMSG_CALENDAR_FILTER_GUILD);
    data << uint32(count); // count placeholder

    for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
    {
        // not sure if needed, maybe client checks it as well
        if (count >= CALENDAR_MAX_INVITES)
        {
            if (Player* player = session->GetPlayer())
                sCalendarMgr->SendCalendarCommandResult(player->GetGUID(), CALENDAR_ERROR_INVITES_EXCEEDED);
            return;
        }

        Member* member = itr->second;
        uint32 level = Player::GetLevelFromDB(member->GetGUID());

        if (member->GetGUID() != session->GetPlayer()->GetGUID() && level >= minLevel && level <= maxLevel && member->IsRankNotLower(minRank))
        {
            data.appendPackGUID(member->GetGUID());
            data << uint8(0); // unk
            ++count;
        }
    }

    data.put<uint32>(0, count);

    session->SendPacket(&data);
}

// Members handling
bool Guild::AddMember(uint64 guid, uint8 rankId)
{
    Player* player = ObjectAccessor::FindPlayer(guid);
    // Player cannot be in guild
    if (player)
    {
        if (player->GetGuildId() != 0)
            return false;
    }
    else if (Player::GetGuildIdFromDB(guid) != 0)
        return false;

    // Remove all player signs from another petitions
    // This will be prevent attempt to join many guilds and corrupt guild data integrity
    Player::RemovePetitionsAndSigns(guid);

    uint32 lowguid = GUID_LOPART(guid);

    // If rank was not passed, assign lowest possible rank
    if (rankId == GUILD_RANK_NONE)
        rankId = _GetLowestRankId();

    Member* member = new Member(m_id, guid, rankId);
    if (player)
        member->SetStats(player);
    else
    {
        bool ok = false;
        // Player must exist
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_DATA_FOR_GUILD);
        stmt->setUInt32(0, lowguid);
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        {
            Field* fields = result->Fetch();
            member->SetStats(
                fields[0].GetString(),
                fields[1].GetUInt8(),
                fields[2].GetUInt8(),
                fields[3].GetUInt16(),
                fields[4].GetUInt32());

            ok = member->CheckStats();
        }
        if (!ok)
        {
            delete member;
            return false;
        }
    }
    m_members[lowguid] = member;

    SQLTransaction trans(NULL);
    member->SaveToDB(trans);
    // If player not in game data in will be loaded from guild tables, so no need to update it!
    if (player)
    {
        player->SetInGuild(m_id);
        player->SetRank(rankId);
        player->SetGuildLevel(GetLevel());
        player->SetGuildIdInvited(0);

        if (sWorld->getBoolConfig(CONFIG_GUILD_LEVELING_ENABLED))
        {
            for (uint32 i = 0; i < sGuildPerkSpellsStore.GetNumRows(); ++i)
                if (GuildPerkSpellsEntry const* entry = sGuildPerkSpellsStore.LookupEntry(i))
                    if (entry->Level <= GetLevel())
                        player->learnSpell(entry->SpellId, true);
        }
    }

    _UpdateAccountsNumber();

    return true;
}

void Guild::DeleteMember(uint64 guid, bool isDisbanding, bool isKicked)
{
    uint32 lowguid = GUID_LOPART(guid);
    Player* player = ObjectAccessor::FindPlayer(guid);

    // Guild master can be deleted when loading guild and guid doesn't exist in characters table
    // or when he is removed from guild by gm command
    if (m_leaderGuid == guid && !isDisbanding)
    {
        Member* oldLeader = NULL;
        Member* newLeader = NULL;
        for (Guild::Members::iterator i = m_members.begin(); i != m_members.end(); ++i)
        {
            if (i->first == lowguid)
                oldLeader = i->second;
            else if (!newLeader || newLeader->GetRankId() > i->second->GetRankId())
                newLeader = i->second;
        }

        if (!newLeader)
        {
            Disband();
            return;
        }

        _SetLeaderGUID(newLeader);

        // If player not online data in data field will be loaded from guild tabs no need to update it !!
        if (Player* newLeaderPlayer = newLeader->FindPlayer())
            newLeaderPlayer->SetRank(GR_GUILDMASTER);

        // If leader does not exist (at guild loading with deleted leader) do not send broadcasts
        if (oldLeader)
        {
            _BroadcastEvent(GE_LEADER_CHANGED, 0, oldLeader->GetName().c_str(), newLeader->GetName().c_str());
            _BroadcastEvent(GE_LEFT, guid, oldLeader->GetName().c_str());
        }
    }

    if (Member* member = GetMember(guid))
        delete member;
    m_members.erase(lowguid);

    // If player not online data in data field will be loaded from guild tabs no need to update it !!
    if (player)
    {
        player->SetInGuild(0);
        player->SetRank(0);
        player->SetGuildLevel(0);
        for (uint32 i = 0; i < sGuildPerkSpellsStore.GetNumRows(); ++i)
            if (GuildPerkSpellsEntry const* entry = sGuildPerkSpellsStore.LookupEntry(i))
                if (entry->Level <= GetLevel())
                    player->removeSpell(entry->SpellId, false, false);
    }

    _DeleteMemberFromDB(lowguid);
    if (!isDisbanding)
        _UpdateAccountsNumber();
}

bool Guild::ChangeMemberRank(uint64 guid, uint8 newRank)
{
    if (newRank <= _GetLowestRankId())                    // Validate rank (allow only existing ranks)
        if (Member* member = GetMember(guid))
        {
            member->ChangeRank(newRank);
            return true;
        }
    return false;
}

bool Guild::IsMember(uint64 guid)
{
    Members::const_iterator itr = m_members.find(GUID_LOPART(guid));
    return itr != m_members.end();
}

// Bank (items move)
void Guild::SwapItems(Player* player, uint8 tabId, uint8 slotId, uint8 destTabId, uint8 destSlotId, uint32 splitedAmount)
{
    if (tabId >= _GetPurchasedTabsSize() || slotId >= GUILD_BANK_MAX_SLOTS ||
        destTabId >= _GetPurchasedTabsSize() || destSlotId >= GUILD_BANK_MAX_SLOTS)
        return;

    if (tabId == destTabId && slotId == destSlotId)
        return;

    BankMoveItemData from(this, player, tabId, slotId);
    BankMoveItemData to(this, player, destTabId, destSlotId);
    _MoveItems(&from, &to, splitedAmount);
}

void Guild::SwapItemsWithInventory(Player* player, bool toChar, uint8 tabId, uint8 slotId, uint8 playerBag, uint8 playerSlotId, uint32 splitedAmount)
{
    if ((slotId >= GUILD_BANK_MAX_SLOTS && slotId != NULL_SLOT) || tabId >= _GetPurchasedTabsSize())
        return;

    BankMoveItemData bankData(this, player, tabId, slotId);
    PlayerMoveItemData charData(this, player, playerBag, playerSlotId);
    if (toChar)
        _MoveItems(&bankData, &charData, splitedAmount);
    else
        _MoveItems(&charData, &bankData, splitedAmount);
}

// Bank tabs
void Guild::SetBankTabText(uint8 tabId, const std::string& text)
{
    if (BankTab* pTab = GetBankTab(tabId))
    {
        pTab->SetText(text);
        pTab->SendText(this, NULL);
    }
}

// Private methods
void Guild::_CreateLogHolders()
{
    m_eventLog = new LogHolder(m_id, sWorld->getIntConfig(CONFIG_GUILD_EVENT_LOG_COUNT));
    for (uint8 tabId = 0; tabId <= GUILD_BANK_MAX_TABS; ++tabId)
        m_bankEventLog[tabId] = new LogHolder(m_id, sWorld->getIntConfig(CONFIG_GUILD_BANK_EVENT_LOG_COUNT));
}

bool Guild::_CreateNewBankTab()
{
    if (_GetPurchasedTabsSize() >= GUILD_BANK_MAX_TABS)
        return false;

    uint8 tabId = _GetPurchasedTabsSize();                      // Next free id
    m_bankTabs.push_back(new BankTab(m_id, tabId));

    PreparedStatement* stmt = NULL;
    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_TAB);
    stmt->setUInt32(0, m_id);
    stmt->setUInt8 (1, tabId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_TAB);
    stmt->setUInt32(0, m_id);
    stmt->setUInt8 (1, tabId);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
    return true;
}

void Guild::_CreateDefaultGuildRanks(LocaleConstant loc)
{
    PreparedStatement* stmt = NULL;

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_RANKS);
    stmt->setUInt32(0, m_id);
    CharacterDatabase.Execute(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GUILD_BANK_RIGHTS);
    stmt->setUInt32(0, m_id);
    CharacterDatabase.Execute(stmt);

    _CreateRank(sObjectMgr->GetTrinityString(LANG_GUILD_MASTER,   loc), GR_RIGHT_ALL);
    _CreateRank(sObjectMgr->GetTrinityString(LANG_GUILD_OFFICER,  loc), GR_RIGHT_ALL);
    _CreateRank(sObjectMgr->GetTrinityString(LANG_GUILD_VETERAN,  loc), GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);
    _CreateRank(sObjectMgr->GetTrinityString(LANG_GUILD_MEMBER,   loc), GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);
    _CreateRank(sObjectMgr->GetTrinityString(LANG_GUILD_INITIATE, loc), GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);
}

void Guild::_CreateRank(const std::string& name, uint32 rights)
{
    uint32 newRankId = _GetRanksSize();
    if (newRankId >= GUILD_RANKS_MAX_COUNT)
        return;

    // Ranks represent sequence 0, 1, 2, ... where 0 means guildmaster
    RankInfo info(m_id, newRankId, name, rights, 0);
    m_ranks.push_back(info);

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    for (uint8 i = 0; i < _GetPurchasedTabsSize(); ++i)
    {
        // Create bank rights with default values
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GUILD_BANK_RIGHT_DEFAULT);
        stmt->setUInt32(0, m_id);
        stmt->setUInt8 (1, i);
        stmt->setUInt8 (2, newRankId);
        trans->Append(stmt);
    }
    info.SaveToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
}

// Updates the number of accounts that are in the guild
// Player may have many characters in the guild, but with the same account
void Guild::_UpdateAccountsNumber()
{
    // We use a set to be sure each element will be unique
    std::set<uint32> accountsIdSet;
    for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        accountsIdSet.insert(itr->second->GetAccountId());

    m_accountsNumber = accountsIdSet.size();
}

// Detects if player is the guild master.
// Check both leader guid and player's rank (otherwise multiple feature with
// multiple guild masters won't work)
bool Guild::_IsLeader(Player* player) const
{
    if (player->GetGUID() == m_leaderGuid)
        return true;
    if (const Member* member = GetMember(player->GetGUID()))
        return member->IsRank(GR_GUILDMASTER);
    return false;
}

void Guild::_DeleteBankItems(SQLTransaction& trans, bool removeItemsFromDB)
{
    for (uint8 tabId = 0; tabId < _GetPurchasedTabsSize(); ++tabId)
    {
        m_bankTabs[tabId]->Delete(trans, removeItemsFromDB);
        delete m_bankTabs[tabId];
        m_bankTabs[tabId] = NULL;
    }
    m_bankTabs.clear();
}

bool Guild::_ModifyBankMoney(SQLTransaction& trans, uint64 amount, bool add)
{
    if (add)
        m_bankMoney += amount;
    else
    {
        // Check if there is enough money in bank.
        if (m_bankMoney < amount)
            return false;
        m_bankMoney -= amount;
    }

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_BANK_MONEY);
    stmt->setUInt64(0, m_bankMoney);
    stmt->setUInt32(1, m_id);
    trans->Append(stmt);
    return true;
}

void Guild::_SetLeaderGUID(Member* pLeader)
{
    if (!pLeader)
        return;

    m_leaderGuid = pLeader->GetGUID();
    pLeader->ChangeRank(GR_GUILDMASTER);

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_LEADER);
    stmt->setUInt32(0, GUID_LOPART(m_leaderGuid));
    stmt->setUInt32(1, m_id);
    CharacterDatabase.Execute(stmt);
}

void Guild::_SetRankBankMoneyPerDay(uint32 rankId, uint32 moneyPerDay)
{
    if (RankInfo* rankInfo = GetRankInfo(rankId))
    {
        for (Members::iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
            if (itr->second->IsRank(rankId))
                itr->second->ResetMoneyTime();

        rankInfo->SetBankMoneyPerDay(moneyPerDay);
    }
}

void Guild::_SetRankBankTabRightsAndSlots(uint32 rankId, uint8 tabId, GuildBankRightsAndSlots rightsAndSlots, bool saveToDB)
{
    if (tabId >= _GetPurchasedTabsSize())
        return;

    if (RankInfo* rankInfo = GetRankInfo(rankId))
    {
        for (Members::iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
            if (itr->second->IsRank(rankId))
                itr->second->ResetTabTimes();

        rankInfo->SetBankTabSlotsAndRights(tabId, rightsAndSlots, saveToDB);
    }
}

inline std::string Guild::_GetRankName(uint32 rankId) const
{
    if (const RankInfo* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetName();
    return "<unknown>";
}

inline uint32 Guild::_GetRankRights(uint32 rankId) const
{
    if (const RankInfo* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetRights();
    return 0;
}

inline uint32 Guild::_GetRankBankMoneyPerDay(uint32 rankId) const
{
    if (const RankInfo* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetBankMoneyPerDay();
    return 0;
}

inline uint32 Guild::_GetRankBankTabSlotsPerDay(uint32 rankId, uint8 tabId) const
{
    if (tabId < _GetPurchasedTabsSize())
        if (const RankInfo* rankInfo = GetRankInfo(rankId))
            return rankInfo->GetBankTabSlotsPerDay(tabId);
    return 0;
}

inline uint32 Guild::_GetRankBankTabRights(uint32 rankId, uint8 tabId) const
{
    if (const RankInfo* rankInfo = GetRankInfo(rankId))
        return rankInfo->GetBankTabRights(tabId);
    return 0;
}

inline uint32 Guild::_GetMemberRemainingSlots(uint64 guid, uint8 tabId) const
{
    if (const Member* member = GetMember(guid))
        return member->GetBankRemainingValue(tabId, this);
    return 0;
}

inline uint32 Guild::_GetMemberRemainingMoney(uint64 guid) const
{
    if (const Member* member = GetMember(guid))
        return member->GetBankRemainingValue(GUILD_BANK_MAX_TABS, this);
    return 0;
}

inline void Guild::_DecreaseMemberRemainingSlots(SQLTransaction& trans, uint64 guid, uint8 tabId)
{
    // Remaining slots must be more then 0
    if (uint32 remainingSlots = _GetMemberRemainingSlots(guid, tabId))
        // Ignore guild master
        if (remainingSlots < uint32(GUILD_WITHDRAW_SLOT_UNLIMITED))
            if (Member* member = GetMember(guid))
                member->DecreaseBankRemainingValue(trans, tabId, 1);
}

inline bool Guild::_MemberHasTabRights(uint64 guid, uint8 tabId, uint32 rights) const
{
    if (const Member* member = GetMember(guid))
    {
        // Leader always has full rights
        if (member->IsRank(GR_GUILDMASTER) || m_leaderGuid == guid)
            return true;
        return (_GetRankBankTabRights(member->GetRankId(), tabId) & rights) == rights;
    }
    return false;
}

// Add new event log record
inline void Guild::_LogEvent(GuildEventLogTypes eventType, uint32 playerGuid1, uint32 playerGuid2, uint8 newRank)
{
    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    m_eventLog->AddEvent(trans, new EventLogEntry(m_id, m_eventLog->GetNextGUID(), eventType, playerGuid1, playerGuid2, newRank));
    CharacterDatabase.CommitTransaction(trans);
}

// Add new bank event log record
void Guild::_LogBankEvent(SQLTransaction& trans, GuildBankEventLogTypes eventType, uint8 tabId, uint32 lowguid, uint32 itemOrMoney, uint16 itemStackCount, uint8 destTabId)
{
    if (tabId > GUILD_BANK_MAX_TABS)
        return;

    // not logging moves within the same tab
    if (eventType == GUILD_BANK_LOG_MOVE_ITEM && tabId == destTabId)
        return;

    uint8 dbTabId = tabId;
    if (BankEventLogEntry::IsMoneyEvent(eventType))
    {
        tabId = GUILD_BANK_MAX_TABS;
        dbTabId = GUILD_BANK_MONEY_LOGS_TAB;
    }
    LogHolder* pLog = m_bankEventLog[tabId];
    pLog->AddEvent(trans, new BankEventLogEntry(m_id, pLog->GetNextGUID(), eventType, dbTabId, lowguid, itemOrMoney, itemStackCount, destTabId));
}

inline Item* Guild::_GetItem(uint8 tabId, uint8 slotId) const
{
    if (const BankTab* tab = GetBankTab(tabId))
        return tab->GetItem(slotId);
    return NULL;
}

inline void Guild::_RemoveItem(SQLTransaction& trans, uint8 tabId, uint8 slotId)
{
    if (BankTab* pTab = GetBankTab(tabId))
        pTab->SetItem(trans, slotId, NULL);
}

void Guild::_MoveItems(MoveItemData* pSrc, MoveItemData* pDest, uint32 splitedAmount)
{
    // 1. Initialize source item
    if (!pSrc->InitItem())
        return; // No source item

    // 2. Check source item
    if (!pSrc->CheckItem(splitedAmount))
        return; // Source item or splited amount is invalid
    /*
    if (pItemSrc->GetCount() == 0)
    {
        sLog->outFatal(LOG_FILTER_GUILD, "Guild::SwapItems: Player %s(GUIDLow: %u) tried to move item %u from tab %u slot %u to tab %u slot %u, but item %u has a stack of zero!",
            player->GetName(), player->GetGUIDLow(), pItemSrc->GetEntry(), tabId, slotId, destTabId, destSlotId, pItemSrc->GetEntry());
        //return; // Commented out for now, uncomment when it's verified that this causes a crash!!
    }
    // */

    // 3. Check destination rights
    if (!pDest->HasStoreRights(pSrc))
        return; // Player has no rights to store item in destination

    // 4. Check source withdraw rights
    if (!pSrc->HasWithdrawRights(pDest))
        return; // Player has no rights to withdraw items from source

    // 5. Check split
    if (splitedAmount)
    {
        // 5.1. Clone source item
        if (!pSrc->CloneItem(splitedAmount))
            return; // Item could not be cloned

        // 5.2. Move splited item to destination
        _DoItemsMove(pSrc, pDest, true, splitedAmount);
    }
    else // 6. No split
    {
        // 6.1. Try to merge items in destination (pDest->GetItem() == NULL)
        if (!_DoItemsMove(pSrc, pDest, false)) // Item could not be merged
        {
            // 6.2. Try to swap items
            // 6.2.1. Initialize destination item
            if (!pDest->InitItem())
                return;

            // 6.2.2. Check rights to store item in source (opposite direction)
            if (!pSrc->HasStoreRights(pDest))
                return; // Player has no rights to store item in source (opposite direction)

            if (!pDest->HasWithdrawRights(pSrc))
                return; // Player has no rights to withdraw item from destination (opposite direction)

            // 6.2.3. Swap items (pDest->GetItem() != NULL)
            _DoItemsMove(pSrc, pDest, true);
        }
    }
    // 7. Send changes
    _SendBankContentUpdate(pSrc, pDest);
}

bool Guild::_DoItemsMove(MoveItemData* pSrc, MoveItemData* pDest, bool sendError, uint32 splitedAmount)
{
    Item* pDestItem = pDest->GetItem();
    bool swap = (pDestItem != NULL);

    Item* pSrcItem = pSrc->GetItem(splitedAmount);
    // 1. Can store source item in destination
    if (!pDest->CanStore(pSrcItem, swap, sendError))
        return false;

    // 2. Can store destination item in source
    if (swap)
        if (!pSrc->CanStore(pDestItem, true, true))
            return false;

    // GM LOG (TODO: move to scripts)
    pDest->LogAction(pSrc);
    if (swap)
        pSrc->LogAction(pDest);

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    // 3. Log bank events
    pDest->LogBankEvent(trans, pSrc, pSrcItem->GetCount());
    if (swap)
        pSrc->LogBankEvent(trans, pDest, pDestItem->GetCount());

    // 4. Remove item from source
    pSrc->RemoveItem(trans, pDest, splitedAmount);

    // 5. Remove item from destination
    if (swap)
        pDest->RemoveItem(trans, pSrc);

    // 6. Store item in destination
    pDest->StoreItem(trans, pSrcItem);

    // 7. Store item in source
    if (swap)
        pSrc->StoreItem(trans, pDestItem);

    CharacterDatabase.CommitTransaction(trans);
    return true;
}

void Guild::_SendBankContentUpdate(MoveItemData* pSrc, MoveItemData* pDest) const
{
    ASSERT(pSrc->IsBank() || pDest->IsBank());

    uint8 tabId = 0;
    SlotIds slots;
    if (pSrc->IsBank()) // B ->
    {
        tabId = pSrc->GetContainer();
        slots.insert(pSrc->GetSlotId());
        if (pDest->IsBank()) // B -> B
        {
            // Same tab - add destination slots to collection
            if (pDest->GetContainer() == pSrc->GetContainer())
                pDest->CopySlots(slots);
            else // Different tabs - send second message
            {
                SlotIds destSlots;
                pDest->CopySlots(destSlots);
                _SendBankContentUpdate(pDest->GetContainer(), destSlots);
            }
        }
    }
    else if (pDest->IsBank()) // C -> B
    {
        tabId = pDest->GetContainer();
        pDest->CopySlots(slots);
    }

    _SendBankContentUpdate(tabId, slots);
}

void Guild::_SendBankContentUpdate(uint8 tabId, SlotIds slots) const
{
    if (BankTab const* tab = GetBankTab(tabId))
    {
        ByteBuffer tabData;
        WorldPacket data(SMSG_GUILD_BANK_LIST, 1200);
        data.WriteBit(false);
        data.WriteBits(slots.size(), 20);                                           // Item count
        data.WriteBits(0, 22);                                                      // Tab count

        for (SlotIds::const_iterator itr = slots.begin(); itr != slots.end(); ++itr)
        {
            data.WriteBit(false);

            Item const* tabItem = tab->GetItem(*itr);
            uint32 enchantCount = 0;
            if (tabItem)
            {
                for (uint32 i = 0; i < MAX_GEM_SOCKETS; ++i)
                {
                    if (uint32 enchantId = tabItem->GetEnchantmentId(EnchantmentSlot(i)))
                    {
                        tabData << uint32(enchantId) << uint32(i);
                        ++enchantCount;
                    }
                }
            }

            data.WriteBits(enchantCount, 23);                                       // enchantment count

            tabData << uint32(0);
            tabData << uint32(0);
            tabData << uint32(0);
            tabData << uint32(tabItem ? tabItem->GetCount() : 0);                   // ITEM_FIELD_STACK_COUNT
            tabData << uint32(*itr);
            tabData << uint32(0);
            tabData << uint32(tabItem ? tabItem->GetEntry() : 0);
            tabData << uint32(tabItem ? tabItem->GetItemRandomPropertyId() : 0);
            tabData << uint32(tabItem ? abs(tabItem->GetSpellCharges()) : 0);       // Spell charges
            tabData << uint32(tabItem ? tabItem->GetItemSuffixFactor() : 0);        // SuffixFactor
        }

        data << uint64(m_bankMoney);
        if (!tabData.empty())
            data.append(tabData);

        data << uint32(tabId);

        size_t rempos = data.wpos();
        data << uint32(0);                                      // Item withdraw amount, will be filled later

        for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
            if (_MemberHasTabRights(itr->second->GetGUID(), tabId, GUILD_BANK_RIGHT_VIEW_TAB))
                if (Player* player = itr->second->FindPlayer())
                {
                    data.put<uint32>(rempos, uint32(_GetMemberRemainingSlots(player->GetGUID(), tabId)));
                    player->GetSession()->SendPacket(&data);
                }
    }
}

void Guild::_BroadcastEvent(GuildEvents guildEvent, uint64 guid, const char* param1, const char* param2, const char* param3) const
{
    uint8 count = !param3 ? (!param2 ? (!param1 ? 0 : 1) : 2) : 3;

    WorldPacket data(SMSG_GUILD_EVENT, 1 + 1 + count + (guid ? 8 : 0));
    data << uint8(guildEvent);
    data << uint8(count);

    if (param3)
        data << param1 << param2 << param3;
    else if (param2)
        data << param1 << param2;
    else if (param1)
        data << param1;

    if (guid)
        data << uint64(guid);

    BroadcastPacket(&data);
}

void Guild::SendGuildRanksUpdate(uint64 setterGuid, uint64 targetGuid, uint32 rank)
{
    ObjectGuid tarGuid = targetGuid;
    ObjectGuid setGuid = setterGuid;

    Member* member = GetMember(targetGuid);
    ASSERT(member);

    WorldPacket data(SMSG_GUILD_RANKS_UPDATE, 100);
    data.WriteByteMask(setGuid[7]);
    data.WriteByteMask(setGuid[2]);
    data.WriteByteMask(tarGuid[2]);
    data.WriteByteMask(setGuid[1]);
    data.WriteByteMask(tarGuid[1]);
    data.WriteByteMask(tarGuid[7]);
    data.WriteByteMask(tarGuid[0]);
    data.WriteByteMask(tarGuid[5]);
    data.WriteByteMask(tarGuid[4]);
    data.WriteBit(rank < member->GetRankId()); // 1 == higher, 0 = lower?
    data.WriteByteMask(setGuid[5]);
    data.WriteByteMask(setGuid[0]);
    data.WriteByteMask(tarGuid[6]);
    data.WriteByteMask(setGuid[3]);
    data.WriteByteMask(setGuid[6]);
    data.WriteByteMask(tarGuid[3]);
    data.WriteByteMask(setGuid[4]);

    data << uint32(rank);
    data.WriteByteSeq(setGuid[3]);
    data.WriteByteSeq(tarGuid[7]);
    data.WriteByteSeq(setGuid[6]);
    data.WriteByteSeq(setGuid[2]);
    data.WriteByteSeq(tarGuid[5]);
    data.WriteByteSeq(tarGuid[0]);
    data.WriteByteSeq(setGuid[7]);
    data.WriteByteSeq(setGuid[5]);
    data.WriteByteSeq(tarGuid[2]);
    data.WriteByteSeq(tarGuid[1]);
    data.WriteByteSeq(setGuid[0]);
    data.WriteByteSeq(setGuid[4]);
    data.WriteByteSeq(setGuid[1]);
    data.WriteByteSeq(tarGuid[3]);
    data.WriteByteSeq(tarGuid[6]);
    data.WriteByteSeq(tarGuid[4]);
    BroadcastPacket(&data);

    member->ChangeRank(rank);
}

void Guild::GainReputationForXP(uint32 rep, Player* player)
{
    if (Guild::Member* pMember = GetMember(player->GetGUID()))
    {
        uint32 weekrep = pMember->GetWeeklyReputation();
        uint32 currep = pMember->GetGuildReputation();
        uint32 weekcap = sWorld->getIntConfig(CONFIG_GUILD_WEEKLY_REP_CAP);

        if (weekrep >= weekcap)
            return;

        if (weekrep + rep > weekcap)
            rep = weekcap - weekrep;

        weekrep += rep;

        pMember->SetWeeklyReputation(weekrep);

        // Guild Champion
        if (Player* plr = pMember->FindPlayer())
        {
            if (plr->HasAura(97340))
                AddPct(rep, 50);
            else if (plr->HasAura(97341))
                AddPct(rep, 100);
        }

        pMember->SetGuildReputation(currep + rep);

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_WEEKLY_REPUTATION);
        stmt->setUInt32(0, pMember->GetWeeklyReputation());
        stmt->setUInt32(1, m_id);
        stmt->setUInt32(2, GUID_LOPART(pMember->GetGUID()));
        CharacterDatabase.Execute(stmt);
    }
}

void Guild::GiveXP(uint32 xp, Player* source /*=NULL*/)
{
    if (!sWorld->getBoolConfig(CONFIG_GUILD_LEVELING_ENABLED))
        return;

    if (GetLevel() >= sWorld->getIntConfig(CONFIG_GUILD_MAX_LEVEL))
        xp = 0; // SMSG_GUILD_XP_GAIN is always sent, even for no gains

    uint32 xp_cap = sWorld->getIntConfig(CONFIG_GUILD_DAILY_XP_CAP) + CalculateXPCapFromChallenge();
    if (GetLevel() < GUILD_EXPERIENCE_UNCAPPED_LEVEL)
        xp = std::min(xp,  xp_cap - uint32(_todayExperience));

    WorldPacket data(SMSG_GUILD_XP_GAIN, 8);
    data << uint64(xp);
    if (source)
        source->GetSession()->SendPacket(&data);

    if (!xp)
        return;

    _experience += xp;
    _todayExperience += xp;

    if (source)
    {
        if (Member* member = GetMember(source->GetGUID()))
        {

            member->AddXPContrib(xp);
            member->AddXPContribWeek(xp);

            PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GUILD_MEMBER_XP);
            stmt->setUInt64(0, member->GetXPContrib());
            stmt->setUInt64(1, member->GetXPContribWeek());
            stmt->setUInt32(2, source->GetGUIDLow());
            CharacterDatabase.Execute(stmt);
        }
    }

    uint32 oldLevel = GetLevel();

    // Ding, mon!
    while (GetExperience() >= sGuildMgr->GetXPForGuildLevel(GetLevel()) && GetLevel() < sWorld->getIntConfig(CONFIG_GUILD_MAX_LEVEL))
    {
        _experience -= sGuildMgr->GetXPForGuildLevel(GetLevel());
        ++_level;

        // Find all guild perks to learn
        std::vector<uint32> perksToLearn;
        for (uint32 i = 0; i < sGuildPerkSpellsStore.GetNumRows(); ++i)
            if (GuildPerkSpellsEntry const* entry = sGuildPerkSpellsStore.LookupEntry(i))
                if (entry->Level > oldLevel && entry->Level <= GetLevel())
                    perksToLearn.push_back(entry->SpellId);

        // Notify all online players that guild level changed and learn perks
        for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        {
            if (Player* player = itr->second->FindPlayer())
            {
                player->SetGuildLevel(GetLevel());
                for (size_t i = 0; i < perksToLearn.size(); ++i)
                    player->learnSpell(perksToLearn[i], true);
            }
        }

        GetNewsLog().AddNewEvent(GUILD_NEWS_LEVEL_UP, time(NULL), 0, 0, _level);
        GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_REACH_GUILD_LEVEL, GetLevel(), 0, 0, NULL, source);

        ++oldLevel;
    }

    if(source)
        SendGuildXP(source->GetSession()); //this is needed, to display correct update on gain.
    else
        SendGuildXP();
}

void Guild::SendGuildXP(WorldSession* session) const
{
    Member const* member = GetMember(session->GetGuidLow());
    ASSERT(member != NULL);

    WorldPacket data(SMSG_GUILD_XP, 40);
    data << uint64(member->GetXPContrib());
    data << uint64(sGuildMgr->GetXPForGuildLevel(GetLevel()) - GetExperience());    // XP missing for next level
    data << uint64(GetTodayExperience());
    data << uint64(member->GetXPContribWeek());
    data << uint64(GetExperience());
    session->SendPacket(&data);
}

void Guild::SendGuildRename(std::string name)
{
    m_name = name;

    ObjectGuid gguid = GetGUID();
    WorldPacket data(SMSG_GUILD_RENAMED);

    data.WriteBit(gguid[5]);
    data.WriteBits(name.size(), 8);
    data.WriteBit(gguid[4]);
    data.WriteBit(gguid[0]);
    data.WriteBit(gguid[6]);
    data.WriteBit(gguid[3]);
    data.WriteBit(gguid[1]);
    data.WriteBit(gguid[7]);
    data.WriteBit(gguid[2]);

    data.WriteByteSeq(gguid[3]);
    data.WriteByteSeq(gguid[2]);
    data.WriteByteSeq(gguid[7]);
    data.WriteByteSeq(gguid[1]);
    data.WriteByteSeq(gguid[0]);
    data.WriteByteSeq(gguid[6]);
    data.WriteString(name);
    data.WriteByteSeq(gguid[4]);
    data.WriteByteSeq(gguid[5]);

    BroadcastPacket(&data);
}

void Guild::ResetDailyExperience()
{
    _todayExperience = 0;

    for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        if (Player* player = itr->second->FindPlayer())
            SendGuildXP(player->GetSession());
}

void Guild::ResetWeeklyReputation()
{
    for (Members::const_iterator itr = m_members.begin(); itr != m_members.end(); ++itr)
        if (Guild::Member* pMember = itr->second)
        {
            pMember->SetWeeklyReputation(0);
            pMember->ResetXPContribWeek();
            if (Player* pPlayer = pMember->FindPlayer())
                SendGuildReputationWeeklyCap(pPlayer->GetSession()); 
        }
}

void Guild::GuildNewsLog::AddNewEvent(GuildNews eventType, time_t date, uint64 playerGuid, uint32 flags, uint32 data)
{
    uint32 id = _newsLog.size();

    GuildNewsEntry& log = _newsLog[id];
    log.EventType = eventType;
    log.PlayerGuid = playerGuid;
    log.Data = data;
    log.Flags = flags;
    log.Date = date;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SAVE_GUILD_NEWS);
    stmt->setUInt32(0, GetGuild()->GetId());
    stmt->setUInt32(1, id);
    stmt->setUInt32(2, log.EventType);
    stmt->setUInt64(3, log.PlayerGuid);
    stmt->setUInt32(4, log.Data);
    stmt->setUInt32(5, log.Flags);
    stmt->setUInt32(6, uint32(log.Date));
    CharacterDatabase.Execute(stmt);

    WorldPacket packet;
    BuildNewsData(id, log, packet);
    GetGuild()->BroadcastPacket(&packet);
}

void Guild::GuildNewsLog::LoadFromDB(PreparedQueryResult result)
{
    if (!result)
        return;
    do
    {
        Field* fields = result->Fetch();
        uint32 id = fields[0].GetInt32();
        GuildNewsEntry& log = _newsLog[id];
        log.EventType = GuildNews(fields[1].GetInt32());
        log.PlayerGuid = fields[2].GetInt64();
        log.Data = fields[3].GetInt32();
        log.Flags = fields[4].GetInt32();
        log.Date = time_t(fields[5].GetInt32());
    }
    while (result->NextRow());
}

void Guild::GuildNewsLog::BuildNewsData(uint32 id, GuildNewsEntry& guildNew, WorldPacket& data)
{
    data.Initialize(SMSG_GUILD_NEWS_UPDATE, 7 + 32);
    data.WriteBits(1, 21); // size, we are only sending 1 news here

    data.WriteBits(0, 26); // Not yet implemented used for guild achievements
    ObjectGuid guid = guildNew.PlayerGuid;

    data.WriteByteMask(guid[7]);
    data.WriteByteMask(guid[0]);
    data.WriteByteMask(guid[6]);
    data.WriteByteMask(guid[5]);
    data.WriteByteMask(guid[4]);
    data.WriteByteMask(guid[3]);
    data.WriteByteMask(guid[1]);
    data.WriteByteMask(guid[2]);

    data.WriteByteSeq(guid[5]);

    data << uint32(guildNew.Flags);   // 1 sticky
    data << uint32(guildNew.Data);
    data << uint32(0);                // always 0

    data.WriteByteSeq(guid[7]);
    data.WriteByteSeq(guid[6]);
    data.WriteByteSeq(guid[2]);
    data.WriteByteSeq(guid[3]);
    data.WriteByteSeq(guid[0]);
    data.WriteByteSeq(guid[4]);
    data.WriteByteSeq(guid[1]);

    data << uint32(id);
    data << uint32(guildNew.EventType);
    data.AppendPackedTime(guildNew.Date);
}

void Guild::GuildNewsLog::BuildNewsData(WorldPacket& data)
{
    data.Initialize(SMSG_GUILD_NEWS_UPDATE, (21 + _newsLog.size() * (26 + 8)) / 8 + (8 + 6 * 4) * _newsLog.size());
    data.WriteBits(_newsLog.size(), 21);

    for (GuildNewsLogMap::const_iterator it = _newsLog.begin(); it != _newsLog.end(); ++it)
    {
        data.WriteBits(0, 26); // Not yet implemented used for guild achievements
        ObjectGuid guid = it->second.PlayerGuid;

        data.WriteByteMask(guid[7]);
        data.WriteByteMask(guid[0]);
        data.WriteByteMask(guid[6]);
        data.WriteByteMask(guid[5]);
        data.WriteByteMask(guid[4]);
        data.WriteByteMask(guid[3]);
        data.WriteByteMask(guid[1]);
        data.WriteByteMask(guid[2]);
    }

    for (GuildNewsLogMap::const_iterator it = _newsLog.begin(); it != _newsLog.end(); ++it)
    {
        ObjectGuid guid = it->second.PlayerGuid;
        data.WriteByteSeq(guid[5]);

        data << uint32(it->second.Flags);   // 1 sticky
        data << uint32(it->second.Data);
        data << uint32(0);

        data.WriteByteSeq(guid[7]);
        data.WriteByteSeq(guid[6]);
        data.WriteByteSeq(guid[2]);
        data.WriteByteSeq(guid[3]);
        data.WriteByteSeq(guid[0]);
        data.WriteByteSeq(guid[4]);
        data.WriteByteSeq(guid[1]);

        data << uint32(it->first);
        data << uint32(it->second.EventType);
        data.AppendPackedTime(it->second.Date);
    }
}

void Guild::LoadGuildChallenge(Field* fields)
{
    m_guildChallenges[CHALLENGE_DUNGEON] = fields[1].GetUInt8();
    m_guildChallenges[CHALLENGE_RAID] = fields[2].GetUInt8();
    m_guildChallenges[CHALLENGE_RATED_BG] = fields[3].GetUInt8();
}

void Guild::ResetGuildChallenge()
{
    m_guildChallenges[CHALLENGE_DUNGEON] = 0;
    m_guildChallenges[CHALLENGE_RAID] = 0;
    m_guildChallenges[CHALLENGE_RATED_BG] = 0;
}

uint32 Guild::GetGuildChallenge(uint8 type) const
{
    if (type >= CHALLENGE_MAX)
        return 0;
     return m_guildChallenges[type];
}

void Guild::CompleteGuildChallenge(uint8 type)
{
    if (type >= CHALLENGE_MAX)
        return;

    GuildChallengeRewardData const& reward = sObjectMgr->GetGuildChallengeRewardData();
    uint32 max_count = reward[type].ChallengeCount;
    uint32 cur_count = m_guildChallenges[type];
    if (cur_count >= max_count)
        return;
    
    uint32 add_exp = reward[type].Expirience;
    uint64 add_gold = (cur_count > 0) ? reward[type].Gold2 : reward[type].Gold;

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
   
    // Add Money
    _ModifyBankMoney(trans, add_gold * 10000, true);

    // Add XP
    GiveXP(add_exp, NULL);

    m_guildChallenges[type]++;
    
    trans->PAppend("REPLACE INTO guild_challenge (guildId, dungeonCount, raidCount, RBGCount) VALUES (%u, %u, %u, %u)", 
        m_id, m_guildChallenges[CHALLENGE_DUNGEON], m_guildChallenges[CHALLENGE_RAID], m_guildChallenges[CHALLENGE_RATED_BG]);
    
    CharacterDatabase.CommitTransaction(trans);

    WorldPacket data(SMSG_GUILD_CHALLENGE_COMPLETED, 5 * 4);

    data << uint32(type); // type
    data << uint32(add_gold); // gold
    data << uint32(m_guildChallenges[type]); // current
    data << uint32(add_exp); // exp
    data << uint32(max_count); // max

    BroadcastPacket(&data);

    // Achievements
    GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_GUILD_CHALLENGE, 1, 0, 0, NULL, NULL);
    GetAchievementMgr().UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_GUILD_CHALLENGE_TYPE, type, 1, 0, NULL, NULL);
}

uint32 Guild::CalculateXPCapFromChallenge() const
{
    uint32 ret = 0;
    GuildChallengeRewardData const& reward = sObjectMgr->GetGuildChallengeRewardData();
    for (uint8 i = 0; i < CHALLENGE_MAX; ++i)
        ret += reward[i].Expirience * m_guildChallenges[i];
    return ret;
}