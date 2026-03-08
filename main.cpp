#include "obse/PluginAPI.h"
#include "obse/CommandTable.h"

#if OBLIVION
#include "obse/GameAPI.h"
#include "obse/GameData.h"
#include "obse/GameObjects.h"
#include "obse/GameForms.h"
#include "obse/GameTasks.h"
#include "obse/GameActorValues.h"
#include "obse/GameExtraData.h"
#include "obse/Utilities.h"
#include "obse/NiCollision.h"
#include "obse/NiHavokObjects.h"
#include "obse/NiRTTI.h"
#include "obse/Tasks.h"

#define ENABLE_EXTRACT_ARGS_MACROS 1
OBSEScriptInterface * g_scriptInterface = NULL;
#define ExtractArgsEx(...) g_scriptInterface->ExtractArgsEx(__VA_ARGS__)
#define ExtractFormatStringArgs(...) g_scriptInterface->ExtractFormatStringArgs(__VA_ARGS__)

#else
#include "obse_editor/EditorAPI.h"
#endif

#include "obse/ParamInfos.h"
#include "obse/Script.h"
#include "obse/GameObjects.h"

#include <cstdio>
#include <cstring>
#include <shlobj.h>
#include <vector>
#include <string>

IDebugLog		gLog("varla_import.log");

PluginHandle			g_pluginHandle = kPluginHandle_Invalid;

// ============================================================================
// ImportSaveDump - reads target.txt and applies values to the player
// Ported from obse64 Commands_Import.cpp, adapted for xOBSE (32-bit)
// ============================================================================

#ifdef OBLIVION

// ---- SaveDumpData: parsed dump values ----
struct SaveDumpData
{
	bool	hasLevel;
	UInt16	level;

	bool hasFame;    int fame;
	bool hasInfamy;  int infamy;
	bool hasBounty;  int bounty;

	bool hasAttribute[8];
	float attributeBase[8];

	// Derived stats: Health=0, Magicka=1, Fatigue=2
	bool hasDerived[3];
	float derivedBase[3];

	// Skills (AV indices 12-32 -> array index 0-20)
	bool hasSkill[21];
	float skillBase[21];

	// Misc stats (array index + value)
	struct MiscStatValue { UInt32 index; UInt32 value; const char* name; };
	std::vector<MiscStatValue> miscStats;

	// Factions
	struct FactionEntry { UInt32 formID; int rank; };
	std::vector<FactionEntry> factions;

	// Globals
	struct GlobalEntry { UInt32 formID; float value; };
	std::vector<GlobalEntry> globals;

	// Game time
	bool hasGameDaysPassed; float gameDaysPassed;
	bool hasGameHour;       float gameHour;

	// Active quest
	bool hasActiveQuestFormID; UInt32 activeQuestFormID;

	// Inventory
	struct InventoryItem { UInt32 formID; SInt32 count; };
	std::vector<InventoryItem> inventory;

	// Spells (base game only - just formIDs)
	struct SpellEntry { UInt32 formID; };
	std::vector<SpellEntry> baseGameSpells;

	// Player-created spells (full reconstruction data)
	struct SpellEffect {
		UInt32 effectCode;   // 4-char code as UInt32
		UInt32 magnitude;
		UInt32 duration;
		UInt32 area;
		UInt32 range;
		UInt32 actorValue;
		float  cost;
	};
	struct PlayerCreatedSpell {
		UInt32 formID;
		char   name[256];
		UInt32 spellType;
		UInt32 magickaCost;
		UInt32 masteryLevel;
		UInt32 spellFlags;
		std::vector<SpellEffect> effects;
	};
	std::vector<PlayerCreatedSpell> playerCreatedSpells;

	// Active quest stage
	bool hasActiveQuestStage; UInt8 activeQuestStage;

	// Quest list (active + completed)
	struct QuestListEntry {
		UInt32 formID;
		UInt8  stage;
		bool   isCompleted;
		UInt8  questFlags;   // runtime flags from dump (Active, RepeatConv, etc.)
	};
	std::vector<QuestListEntry> questList;

	// Inventory detail (extra data per item)
	struct InventoryDetailEntry {
		UInt32 formID;
		float  health;    // -1 = not set
		float  charge;    // -1 = not set
		UInt8  soul;      // 0xFF = not set
		bool   isWorn;
	};
	std::vector<InventoryDetailEntry> inventoryDetail;

	// Map markers
	struct MapMarkerEntry { UInt32 refFormID; UInt16 flags; };
	std::vector<MapMarkerEntry> mapMarkers;

	// Dialog topic infos (TESTopicInfo formIDs that were SAID)
	std::vector<UInt32> dialogTopicInfosSaid;

	// Player-created potions
	struct PotionEntry {
		char   name[256];
		SInt32 count;
		float  weight;
		UInt32 value;
		UInt8  alchFlags;
		std::vector<SpellEffect> effects;
	};
	std::vector<PotionEntry> playerCreatedPotions;

	// Cell items (loose items on the ground)
	struct CellItemEntry {
		UInt32 refFormID;
		UInt32 baseFormID;
		float posX, posY, posZ;
		float rotX, rotY, rotZ;
		float scale;
		bool hasScale;
	};
	std::vector<CellItemEntry> cellItems;

	// Appearance
	bool hasHairFormID;    UInt32 hairFormID;
	bool hasEyesFormID;    UInt32 eyesFormID;
	bool hasHairColor;     UInt8  hairColorRGB[3];
	bool hasHairColorFloat; float hairColorFloat;  // obse64 format (single float)
	bool hasHairLength;    float  hairLength;
	static const int kFaceGenArrayCount = 6;
	std::vector<float> faceGenArrays[kFaceGenArrayCount];

	SaveDumpData()
		: hasLevel(false), level(0)
		, hasFame(false), fame(0), hasInfamy(false), infamy(0)
		, hasBounty(false), bounty(0)
		, hasGameDaysPassed(false), gameDaysPassed(0.0f)
		, hasGameHour(false), gameHour(0.0f)
		, hasActiveQuestFormID(false), activeQuestFormID(0)
		, hasActiveQuestStage(false), activeQuestStage(0)
		, hasHairFormID(false), hairFormID(0)
		, hasEyesFormID(false), eyesFormID(0)
		, hasHairColor(false), hasHairColorFloat(false), hairColorFloat(0.0f)
		, hasHairLength(false), hairLength(0.0f)
	{
		memset(hairColorRGB, 0, sizeof(hairColorRGB));
		memset(hasAttribute, 0, sizeof(hasAttribute));
		memset(attributeBase, 0, sizeof(attributeBase));
		memset(hasDerived, 0, sizeof(hasDerived));
		memset(derivedBase, 0, sizeof(derivedBase));
		memset(hasSkill, 0, sizeof(hasSkill));
		memset(skillBase, 0, sizeof(skillBase));
	}
};

// ---- Misc stat name-to-index table (matches Hooks_SaveDump.cpp output) ----
struct MiscStatDef { const char* name; UInt32 index; };

static const MiscStatDef s_miscStatTable[] = {
	{ "Days in Prison",        PlayerCharacter::kMiscStat_DaysInPrison },
	{ "Days Passed",           PlayerCharacter::kMiscStat_DaysPassed },
	{ "Skill Increases",       PlayerCharacter::kMiscStat_SkillIncreases },
	{ "Training Sessions",     PlayerCharacter::kMiscStat_TrainingSessions },
	{ "Largest Bounty",        PlayerCharacter::kMiscStat_LargestBounty },
	{ "Creatures Killed",      PlayerCharacter::kMiscStat_CreaturesKilled },
	{ "People Killed",         PlayerCharacter::kMiscStat_PeopleKilled },
	{ "Places Discovered",     PlayerCharacter::kMiscStat_PlacesDiscovered },
	{ "Locks Picked",          PlayerCharacter::kMiscStat_LocksPicked },
	{ "Lockpicks Broken",      PlayerCharacter::kMiscStat_LockpicksBroken },
	{ "Souls Trapped",         PlayerCharacter::kMiscStat_SoulsTrapped },
	{ "Ingredients Eaten",     PlayerCharacter::kMiscStat_IngredientsEaten },
	{ "Potions Made",          PlayerCharacter::kMiscStat_PotionsMade },
	{ "Oblivion Gates Shut",   PlayerCharacter::kMiscStat_OblivionGatesShut },
	{ "Horses Owned",          PlayerCharacter::kMiscStat_HorsesOwned },
	{ "Houses Owned",          PlayerCharacter::kMiscStat_HousesOwned },
	{ "Stores Invested In",    PlayerCharacter::kMiscStat_StoresInvestedIn },
	{ "Books Read",            PlayerCharacter::kMiscStat_BooksRead },
	{ "Skill Books Read",      PlayerCharacter::kMiscStat_SkillBooksRead },
	{ "Artifacts Found",       PlayerCharacter::kMiscStat_ArtifactsFound },
	{ "Hours Slept",           PlayerCharacter::kMiscStat_HoursSlept },
	{ "Hours Waited",          PlayerCharacter::kMiscStat_HoursWaited },
	{ "Days as a Vampire",     PlayerCharacter::kMiscStat_DaysAsAVampire },
	{ "Last Day as a Vampire", PlayerCharacter::kMiscStat_LastDayAsAVampire },
	{ "People Fed On",         PlayerCharacter::kMiscStat_PeopleFedOn },
	{ "Jokes Told",            PlayerCharacter::kMiscStat_JokesTold },
	{ "Diseases Contracted",   PlayerCharacter::kMiscStat_DiseasesContracted },
	{ "Nirnroots Found",       PlayerCharacter::kMiscStat_NirnrootsFound },
	{ "Items Stolen",          PlayerCharacter::kMiscStat_ItemsStolen },
	{ "Items Pickpocketed",    PlayerCharacter::kMiscStat_ItemsPickpocketed },
	{ "Trespasses",            PlayerCharacter::kMiscStat_Trespasses },
	{ "Assaults",              PlayerCharacter::kMiscStat_Assaults },
	{ "Murders",               PlayerCharacter::kMiscStat_Murders },
	{ "Horses Stolen",         PlayerCharacter::kMiscStat_HorsesStolen },
};

static const int kNumMiscStats = sizeof(s_miscStatTable) / sizeof(s_miscStatTable[0]);

// ---- Path builder ----
static bool GetSaveDumpPath(char* outPath, size_t maxLen)
{
	char myDocuments[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocuments)))
	{
		sprintf_s(outPath, maxLen, "%s\\My Games\\Oblivion\\OBSE\\target.txt", myDocuments);
		return true;
	}
	return false;
}

// ---- Actor value name matching (case-insensitive prefix) ----
static UInt32 MatchActorValueName(const char* trimmedName)
{
	static const struct { const char* name; UInt32 av; } avNames[] = {
		{ "Strength",      kActorVal_Strength },
		{ "Intelligence",  kActorVal_Intelligence },
		{ "Willpower",     kActorVal_Willpower },
		{ "Agility",       kActorVal_Agility },
		{ "Speed",         kActorVal_Speed },
		{ "Endurance",     kActorVal_Endurance },
		{ "Personality",   kActorVal_Personality },
		{ "Luck",          kActorVal_Luck },
		{ "Health",        kActorVal_Health },
		{ "Magicka",       kActorVal_Magicka },
		{ "Fatigue",       kActorVal_Fatigue },
		{ "Armorer",       kActorVal_Armorer },
		{ "Athletics",     kActorVal_Athletics },
		{ "Blade",         kActorVal_Blade },
		{ "Block",         kActorVal_Block },
		{ "Blunt",         kActorVal_Blunt },
		{ "Hand To Hand",  kActorVal_HandToHand },
		{ "HandToHand",    kActorVal_HandToHand },
		{ "Heavy Armor",   kActorVal_HeavyArmor },
		{ "HeavyArmor",    kActorVal_HeavyArmor },
		{ "Alchemy",       kActorVal_Alchemy },
		{ "Alteration",    kActorVal_Alteration },
		{ "Conjuration",   kActorVal_Conjuration },
		{ "Destruction",   kActorVal_Destruction },
		{ "Illusion",      kActorVal_Illusion },
		{ "Mysticism",     kActorVal_Mysticism },
		{ "Restoration",   kActorVal_Restoration },
		{ "Acrobatics",    kActorVal_Acrobatics },
		{ "Light Armor",   kActorVal_LightArmor },
		{ "LightArmor",    kActorVal_LightArmor },
		{ "Marksman",      kActorVal_Marksman },
		{ "Mercantile",    kActorVal_Mercantile },
		{ "Security",      kActorVal_Security },
		{ "Sneak",         kActorVal_Sneak },
		{ "Speechcraft",   kActorVal_Speechcraft },
	};

	for (int i = 0; i < sizeof(avNames) / sizeof(avNames[0]); i++)
	{
		if (_strnicmp(trimmedName, avNames[i].name, strlen(avNames[i].name)) == 0)
			return avNames[i].av;
	}
	return 0xFFFFFFFF;
}

// ---- Helper: Extract formID from a line in either format ----
static UInt32 ExtractFormID(const char* line)
{
	const char* p = strstr(line, "(0x");
	if (p)
	{
		UInt32 fid = 0;
		if (sscanf_s(p, "(0x%X)", &fid) == 1 && fid != 0)
			return fid;
	}

	const char* bracket = strchr(line, '[');
	if (bracket)
	{
		UInt32 fid = 0;
		if (sscanf_s(bracket, "[%X]", &fid) == 1 && fid > 0xFFF)
			return fid;
	}

	return 0;
}

// ---- Parser: section-aware line reader ----
enum ParseSection
{
	kSection_None = 0,
	kSection_CharacterInfo,
	kSection_FameInfamy,
	kSection_Attributes,
	kSection_DerivedStats,
	kSection_Skills,
	kSection_MiscStats,
	kSection_Factions,
	kSection_Globals,
	kSection_GameTime,
	kSection_ActiveQuest,
	kSection_Inventory,
	kSection_Spells,
	kSection_QuestList,
	kSection_InventoryDetail,
	kSection_MapMarkers,
	kSection_DialogTopics,
	kSection_PlayerCreatedPotions,
	kSection_Appearance,
	kSection_CellItems,
};

static bool ParseSaveDump(const char* path, SaveDumpData& data)
{
	FILE* f = NULL;
	if (fopen_s(&f, path, "r") != 0 || !f)
	{
		_MESSAGE("ImportSaveDump: Failed to open dump file: %s", path);
		return false;
	}

	_MESSAGE("ImportSaveDump: Successfully opened dump file");

	char line[1024];
	ParseSection section = kSection_None;
	int lineNum = 0;
	bool inCompletedQuests = false;

	while (fgets(line, sizeof(line), f))
	{
		lineNum++;
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		if (len == 0) continue;

		if (strncmp(line, "===", 3) == 0 || strncmp(line, "---", 3) == 0)
		{
			if (section == kSection_QuestList && strncmp(line, "  ===", 5) == 0)
				goto parse_line;

			ParseSection newSection = kSection_None;

			if (strstr(line, "CHARACTER INFO"))            newSection = kSection_CharacterInfo;
			else if (strstr(line, "Character Info"))        newSection = kSection_CharacterInfo;
			else if (strstr(line, "FAME / INFAMY"))         newSection = kSection_FameInfamy;
			else if (strstr(line, "Fame / Infamy"))         newSection = kSection_FameInfamy;
			else if (strstr(line, "ATTRIBUTES"))            newSection = kSection_Attributes;
			else if (strstr(line, "Attributes"))            newSection = kSection_Attributes;
			else if (strstr(line, "DERIVED STATS"))         newSection = kSection_DerivedStats;
			else if (strstr(line, "Derived Stats"))         newSection = kSection_DerivedStats;
			else if (strstr(line, "SKILLS") && !strstr(line, "SKILL EXPERIENCE"))
			                                                newSection = kSection_Skills;
			else if (strstr(line, "Skills") && !strstr(line, "Skill Experience"))
			                                                newSection = kSection_Skills;
			else if (strstr(line, "MISC STATISTICS"))       newSection = kSection_MiscStats;
			else if (strstr(line, "Misc Statistics"))       newSection = kSection_MiscStats;
			else if (strstr(line, "FACTIONS"))              newSection = kSection_Factions;
			else if (strstr(line, "Factions"))              newSection = kSection_Factions;
			else if (strstr(line, "GLOBAL VARIABLES"))      newSection = kSection_Globals;
			else if (strstr(line, "Global Variables"))      newSection = kSection_Globals;
			else if (strstr(line, "GAME TIME"))             newSection = kSection_GameTime;
			else if (strstr(line, "Game Time"))             newSection = kSection_GameTime;
			else if (strstr(line, "PLAYER QUEST LIST"))     newSection = kSection_QuestList;
			else if (strstr(line, "Player Quest List"))     newSection = kSection_QuestList;
			else if (strstr(line, "ACTIVE QUESTS"))         { newSection = kSection_QuestList; inCompletedQuests = false; }
			else if (strstr(line, "Active Quests"))         { newSection = kSection_QuestList; inCompletedQuests = false; }
			else if (strstr(line, "COMPLETED QUESTS"))      { newSection = kSection_QuestList; inCompletedQuests = true; }
			else if (strstr(line, "Completed Quests"))      { newSection = kSection_QuestList; inCompletedQuests = true; }
			else if (strstr(line, "ACTIVE QUEST") && !strstr(line, "QUESTS"))
			                                                newSection = kSection_ActiveQuest;
			else if (strstr(line, "Active Quest") && !strstr(line, "Quests"))
			                                                newSection = kSection_ActiveQuest;
			else if (strstr(line, "INVENTORY EXTRADATA"))   newSection = kSection_InventoryDetail;
			else if (strstr(line, "Inventory ExtraData"))   newSection = kSection_InventoryDetail;
			else if (strstr(line, "MAP MARKERS"))           newSection = kSection_MapMarkers;
			else if (strstr(line, "Map Markers"))           newSection = kSection_MapMarkers;
			else if (strstr(line, "DIALOG TOPICS"))         newSection = kSection_DialogTopics;
			else if (strstr(line, "Dialog Topics"))         newSection = kSection_DialogTopics;
			else if (strstr(line, "Player-Created Potions"))newSection = kSection_PlayerCreatedPotions;
			else if (strstr(line, "PLAYER-CREATED POTIONS"))newSection = kSection_PlayerCreatedPotions;
			else if (strstr(line, "CELL ITEMS"))             newSection = kSection_CellItems;
			else if (strstr(line, "Cell Items"))             newSection = kSection_CellItems;
			else if (strstr(line, "APPEARANCE"))            newSection = kSection_Appearance;
			else if (strstr(line, "Appearance"))            newSection = kSection_Appearance;
			else if (strstr(line, "INVENTORY"))             newSection = kSection_Inventory;
			else if (strstr(line, "Inventory"))             newSection = kSection_Inventory;
			else if (strstr(line, "SPELLS") && !strstr(line, "LEVELED"))
			                                                newSection = kSection_Spells;
			else if (strstr(line, "Known Spells"))          newSection = kSection_Spells;

			if (strncmp(line, "===", 3) == 0)
				section = newSection;
			else if (newSection != kSection_None)
				section = newSection;
			continue;
		}

parse_line:

		switch (section)
		{
		case kSection_CharacterInfo:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
			unsigned int levelVal = 0;
			if (sscanf_s(trimmed, "Level: %u", &levelVal) == 1)
			{
				data.hasLevel = true;
				data.level = (UInt16)levelVal;
				_MESSAGE("ImportSaveDump: Parsed Level: %u", levelVal);
			}
			break;
		}

		case kSection_FameInfamy:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
			int intVal = 0;
			if (sscanf_s(trimmed, "Fame: %d", &intVal) == 1)
			{
				data.hasFame = true;
				data.fame = intVal;
			}
			else if (sscanf_s(trimmed, "Infamy: %d", &intVal) == 1)
			{
				data.hasInfamy = true;
				data.infamy = intVal;
			}
			else if (sscanf_s(trimmed, "Bounty: %d", &intVal) == 1)
			{
				data.hasBounty = true;
				data.bounty = intVal;
			}
			break;
		}

		case kSection_Attributes:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			char name[64] = {};
			int current = 0;
			int base = 0;
			if (sscanf_s(trimmed, "%[^:]: %d (Base: %d)", name, (unsigned)sizeof(name), &current, &base) == 3
			 || sscanf_s(trimmed, "%[^:]: %d (base: %d)", name, (unsigned)sizeof(name), &current, &base) == 3)
			{
				size_t nLen = strlen(name);
				while (nLen > 0 && name[nLen - 1] == ' ') name[--nLen] = '\0';

				UInt32 avCode = MatchActorValueName(name);
				if (avCode <= kActorVal_Luck)
				{
					data.hasAttribute[avCode] = true;
					data.attributeBase[avCode] = (float)base;
				}
			}
			break;
		}

		case kSection_DerivedStats:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			char name[64] = {};
			int current = 0, maxVal = 0;
			if (sscanf_s(trimmed, "%[^:]: %d / %d", name, (unsigned)sizeof(name), &current, &maxVal) == 3)
			{
				size_t nLen = strlen(name);
				while (nLen > 0 && name[nLen - 1] == ' ') name[--nLen] = '\0';

				UInt32 avCode = MatchActorValueName(name);
				if (avCode >= kActorVal_Health && avCode <= kActorVal_Fatigue)
				{
					int idx = avCode - kActorVal_Health;
					data.hasDerived[idx] = true;
					data.derivedBase[idx] = (float)maxVal;
				}
			}
			break;
		}

		case kSection_Skills:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			char name[64] = {};
			int current = 0, base = 0;
			if (sscanf_s(trimmed, "%[^:]: %d (Base: %d)", name, (unsigned)sizeof(name), &current, &base) == 3
			 || sscanf_s(trimmed, "%[^:]: %d (base: %d)", name, (unsigned)sizeof(name), &current, &base) == 3)
			{
				size_t nLen = strlen(name);
				while (nLen > 0 && name[nLen - 1] == ' ') name[--nLen] = '\0';

				UInt32 avCode = MatchActorValueName(name);
				if (avCode >= kActorVal_Armorer && avCode <= kActorVal_Speechcraft)
				{
					int idx = avCode - kActorVal_Armorer;
					data.hasSkill[idx] = true;
					data.skillBase[idx] = (float)base;
				}
			}
			break;
		}

		case kSection_MiscStats:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			for (int i = 0; i < kNumMiscStats; i++)
			{
				size_t nameLen = strlen(s_miscStatTable[i].name);
				if (_strnicmp(trimmed, s_miscStatTable[i].name, nameLen) == 0)
				{
					const char* afterName = trimmed + nameLen;
					while (*afterName == ' ') afterName++;
					if (*afterName == ':')
					{
						UInt32 val = 0;
						if (sscanf_s(afterName + 1, " %u", &val) == 1)
						{
							SaveDumpData::MiscStatValue msv;
							msv.index = s_miscStatTable[i].index;
							msv.value = val;
							msv.name = s_miscStatTable[i].name;
							data.miscStats.push_back(msv);
						}
						break;
					}
				}
			}
			break;
		}

		case kSection_Factions:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			const char* rankStr = strstr(trimmed, "Rank:");
			if (rankStr)
			{
				UInt32 formID = ExtractFormID(trimmed);
				if (formID != 0)
				{
					int rank = 0;
					sscanf_s(rankStr, "Rank: %d", &rank);
					data.factions.push_back({ formID, rank });
				}
			}
			break;
		}

		case kSection_Globals:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			if (*trimmed == '[')
			{
				UInt32 formID = 0;
				if (sscanf_s(trimmed, "[%X]", &formID) == 1 && formID != 0)
				{
					const char* eqStr = strstr(trimmed, " = ");
					if (eqStr)
					{
						float value = 0.0f;
						if (sscanf_s(eqStr, " = %f", &value) == 1)
						{
							data.globals.push_back({ formID, value });
						}
					}
				}
			}
			break;
		}

		case kSection_GameTime:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			float fVal = 0.0f;
			int iVal = 0;
			if (sscanf_s(trimmed, "Days Passed: %d", &iVal) == 1)
			{
				data.hasGameDaysPassed = true;
				data.gameDaysPassed = (float)iVal;
			}
			else if (sscanf_s(trimmed, "Hour: %f", &fVal) == 1)
			{
				data.hasGameHour = true;
				data.gameHour = fVal;
			}
			break;
		}

		case kSection_ActiveQuest:
		{
			const char* idStr = strstr(line, "(ID:");
			if (idStr)
			{
				UInt32 formID = 0;
				if (sscanf_s(idStr, "(ID: %X)", &formID) == 1 && formID != 0)
				{
					data.hasActiveQuestFormID = true;
					data.activeQuestFormID = formID;
				}
			}
			const char* stageStr = strstr(line, "Stage:");
			if (stageStr)
			{
				int stage = 0;
				if (sscanf_s(stageStr, "Stage: %d", &stage) == 1)
				{
					data.hasActiveQuestStage = true;
					data.activeQuestStage = (UInt8)stage;
				}
			}
			break;
		}

		case kSection_Inventory:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			if (*trimmed == '[')
			{
				const char* xStr = strstr(trimmed, " x");
				if (xStr)
				{
					UInt32 formID = ExtractFormID(trimmed);
					if (formID != 0)
					{
						int count = 0;
						if (sscanf_s(xStr, " x%d", &count) == 1)
						{
							data.inventory.push_back({ formID, (SInt32)count });
						}
					}
				}
			}
			break;
		}

		case kSection_Spells:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			if (!data.playerCreatedSpells.empty() && strstr(trimmed, "- Mag:"))
			{
				SaveDumpData::PlayerCreatedSpell& pcs = data.playerCreatedSpells.back();
				SaveDumpData::SpellEffect eff;
				memset(&eff, 0, sizeof(eff));

				const char* magStr = strstr(trimmed, "Mag:");
				if (magStr) sscanf_s(magStr, "Mag: %u", &eff.magnitude);

				const char* durStr = strstr(trimmed, "Dur:");
				if (durStr) sscanf_s(durStr, "Dur: %u", &eff.duration);

				const char* areaStr = strstr(trimmed, "Area:");
				if (areaStr) sscanf_s(areaStr, "Area: %u", &eff.area);

				const char* rangeStr = strstr(trimmed, "Range:");
				if (rangeStr)
				{
					char rangeName[16] = {};
					if (sscanf_s(rangeStr, "Range: %15[^,]", rangeName, (unsigned)sizeof(rangeName)) == 1)
					{
						if (_stricmp(rangeName, "Self") == 0)        eff.range = 0;
						else if (_stricmp(rangeName, "Touch") == 0)  eff.range = 1;
						else if (_stricmp(rangeName, "Target") == 0) eff.range = 2;
					}
				}

				const char* costStr = strstr(trimmed, "Cost:");
				if (costStr) sscanf_s(costStr, "Cost: %f", &eff.cost);

				pcs.effects.push_back(eff);
			}
			else if (*trimmed == '[')
			{
				UInt32 formID = ExtractFormID(trimmed);
				if (formID != 0)
				{
					bool isPlayerCreated = strstr(trimmed, "[PLAYER_CREATED]") != NULL
						|| ((formID >> 24) == 0xFF);

					if (isPlayerCreated)
					{
						SaveDumpData::PlayerCreatedSpell pcs;
						memset(&pcs, 0, sizeof(pcs));
						pcs.formID = formID;

						const char* nameStart = strchr(trimmed, ']');
						if (nameStart) {
							nameStart++;
							while (*nameStart == ' ') nameStart++;
							const char* nameEnd = strstr(nameStart, " (");
							if (nameEnd) {
								size_t len = nameEnd - nameStart;
								if (len >= sizeof(pcs.name)) len = sizeof(pcs.name) - 1;
								strncpy_s(pcs.name, sizeof(pcs.name), nameStart, len);
							}
						}

						data.playerCreatedSpells.push_back(pcs);
					}
					else
					{
						data.baseGameSpells.push_back({ formID });
					}
				}
			}
			else if (!data.playerCreatedSpells.empty())
			{
				SaveDumpData::PlayerCreatedSpell& pcs = data.playerCreatedSpells.back();

				if (strstr(trimmed, "SpellType:"))
				{
					sscanf_s(trimmed, "SpellType: %u MagickaCost: %u MasteryLevel: %u Flags: %X",
						&pcs.spellType, &pcs.magickaCost, &pcs.masteryLevel, &pcs.spellFlags);
				}
				else if (strncmp(trimmed, "Type:", 5) == 0)
				{
					if (strstr(trimmed, "Disease"))       pcs.spellType = 1;
					else if (strstr(trimmed, "Power"))    pcs.spellType = 2;
					else if (strstr(trimmed, "Ability"))  pcs.spellType = 4;
					else                                  pcs.spellType = 0;

					const char* costStr = strstr(trimmed, "Cost:");
					if (costStr)
					{
						UInt32 cost = 0;
						if (sscanf_s(costStr, "Cost: %u", &cost) == 1)
							pcs.magickaCost = cost;
					}

					const char* mastStr = strstr(trimmed, "Mastery:");
					if (mastStr)
					{
						char mastName[16] = {};
						if (sscanf_s(mastStr, "Mastery: %15s", mastName, (unsigned)sizeof(mastName)) == 1)
						{
							if (_stricmp(mastName, "Apprentice") == 0)       pcs.masteryLevel = 1;
							else if (_stricmp(mastName, "Journeyman") == 0)  pcs.masteryLevel = 2;
							else if (_stricmp(mastName, "Expert") == 0)      pcs.masteryLevel = 3;
							else if (_stricmp(mastName, "Master") == 0)      pcs.masteryLevel = 4;
							else                                             pcs.masteryLevel = 0;
						}
					}
				}
				else if (strncmp(trimmed, "Flags:", 6) == 0)
				{
					UInt32 flags = 0;
					sscanf_s(trimmed, "Flags: 0x%X", &flags);
					pcs.spellFlags = flags;
				}
				else if (strstr(trimmed, "EffectCode:"))
				{
					char codeStr[8] = {};
					if (sscanf_s(trimmed, "EffectCode: '%4s'", codeStr, (unsigned)sizeof(codeStr)) >= 1)
					{
						SaveDumpData::SpellEffect eff;
						memset(&eff, 0, sizeof(eff));

						eff.effectCode = (UInt32)codeStr[0]
							| ((UInt32)codeStr[1] << 8)
							| ((UInt32)codeStr[2] << 16)
							| ((UInt32)codeStr[3] << 24);

						const char* avStr = strstr(trimmed, "ActorValue:");
						if (avStr) sscanf_s(avStr, "ActorValue: %u", &eff.actorValue);

						if (!pcs.effects.empty() && pcs.effects.back().effectCode == 0)
						{
							pcs.effects.back().effectCode = eff.effectCode;
							pcs.effects.back().actorValue = eff.actorValue;
						}
						else
						{
							pcs.effects.push_back(eff);
						}
					}
				}
				else if (strstr(trimmed, "Effect ") && strstr(trimmed, "Magnitude:"))
				{
					SaveDumpData::SpellEffect eff;
					memset(&eff, 0, sizeof(eff));

					char codeStr[8] = {};
					UInt32 effIdx = 0;
					if (sscanf_s(trimmed, "Effect %u: %4s", &effIdx, codeStr, (unsigned)sizeof(codeStr)) >= 2)
					{
						eff.effectCode = (UInt32)codeStr[0]
							| ((UInt32)codeStr[1] << 8)
							| ((UInt32)codeStr[2] << 16)
							| ((UInt32)codeStr[3] << 24);

						const char* magStr = strstr(trimmed, "Magnitude:");
						if (magStr) sscanf_s(magStr, "Magnitude: %u", &eff.magnitude);

						const char* durStr = strstr(trimmed, "Duration:");
						if (durStr) sscanf_s(durStr, "Duration: %u", &eff.duration);

						const char* areaStr = strstr(trimmed, "Area:");
						if (areaStr) sscanf_s(areaStr, "Area: %u", &eff.area);

						const char* rangeStr = strstr(trimmed, "Range:");
						if (rangeStr) sscanf_s(rangeStr, "Range: %u", &eff.range);

						const char* avStr = strstr(trimmed, "ActorValue:");
						if (avStr) sscanf_s(avStr, "ActorValue: %u", &eff.actorValue);

						const char* costStr = strstr(trimmed, "Cost:");
						if (costStr) sscanf_s(costStr, "Cost: %f", &eff.cost);

						pcs.effects.push_back(eff);
					}
				}
			}
			break;
		}

		case kSection_QuestList:
		{
			if (strncmp(line, "  ===", 5) == 0 || strncmp(line, "===", 3) == 0)
			{
				if (strstr(line, "Completed Quests") || strstr(line, "COMPLETED QUESTS"))
					inCompletedQuests = true;
				else if (strstr(line, "Active Quests") || strstr(line, "ACTIVE QUESTS"))
					inCompletedQuests = false;
				break;
			}

			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			if (*trimmed == '[')
			{
				UInt32 formID = ExtractFormID(trimmed);
				if (formID != 0)
				{
					SaveDumpData::QuestListEntry qe;
					qe.formID = formID;
					qe.stage = 0;
					qe.isCompleted = inCompletedQuests;
					qe.questFlags = inCompletedQuests
						? TESQuest::kQuestFlag_Completed
						: TESQuest::kQuestFlag_Active;
					data.questList.push_back(qe);
				}
			}
			else if (!data.questList.empty())
			{
				if (strstr(trimmed, "Current Stage:"))
				{
					int stage = 0;
					if (sscanf_s(strstr(trimmed, "Current Stage:"), "Current Stage: %d", &stage) == 1)
						data.questList.back().stage = (UInt8)stage;
				}
				else if (strstr(trimmed, "Final Stage:"))
				{
					int stage = 0;
					if (sscanf_s(strstr(trimmed, "Final Stage:"), "Final Stage: %d", &stage) == 1)
						data.questList.back().stage = (UInt8)stage;
				}
				else if (strncmp(trimmed, "Flags:", 6) == 0)
				{
					UInt32 flags = 0;
					if (sscanf_s(trimmed, "Flags: 0x%X", &flags) == 1)
						data.questList.back().questFlags = (UInt8)flags;
				}
				else if (strstr(trimmed, "Stage:") && !strstr(trimmed, "Editor"))
				{
					int stage = 0;
					if (sscanf_s(trimmed, "Stage: %d", &stage) == 1)
						data.questList.back().stage = (UInt8)stage;
				}
			}
			break;
		}

		case kSection_InventoryDetail:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			UInt32 formID = 0;
			if (*trimmed == '[')
				sscanf_s(trimmed, "[%X]", &formID);
			else if (strncmp(trimmed, "0x", 2) == 0)
				sscanf_s(trimmed, "0x%X", &formID);
			if (formID != 0)
				{
					SaveDumpData::InventoryDetailEntry ide;
					ide.formID = formID;
					ide.health = -1.0f;
					ide.charge = -1.0f;
					ide.soul = 0xFF;
					ide.isWorn = (strstr(trimmed, "[WORN]") != NULL);

					const char* hpStr = strstr(trimmed, "HP=");
					if (hpStr) sscanf_s(hpStr, "HP=%f", &ide.health);

					const char* chargeStr = strstr(trimmed, "Charge=");
					if (chargeStr) sscanf_s(chargeStr, "Charge=%f", &ide.charge);

					const char* soulStr = strstr(trimmed, "Soul=");
					if (soulStr)
					{
						char soulName[16] = {};
						if (sscanf_s(soulStr, "Soul=%15s", soulName, (unsigned)sizeof(soulName)) == 1)
						{
							if      (_stricmp(soulName, "None") == 0)    ide.soul = 0;
							else if (_stricmp(soulName, "Petty") == 0)   ide.soul = 1;
							else if (_stricmp(soulName, "Lesser") == 0)  ide.soul = 2;
							else if (_stricmp(soulName, "Common") == 0)  ide.soul = 3;
							else if (_stricmp(soulName, "Greater") == 0) ide.soul = 4;
							else if (_stricmp(soulName, "Grand") == 0)   ide.soul = 5;
						}
					}

					if (ide.health >= 0.0f || ide.charge >= 0.0f || ide.soul != 0xFF)
						data.inventoryDetail.push_back(ide);
				}
			break;
		}

		case kSection_MapMarkers:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			if (*trimmed == '[')
			{
				UInt32 formID = ExtractFormID(trimmed);
				if (formID != 0)
				{
					SaveDumpData::MapMarkerEntry entry;
					entry.refFormID = formID;
					entry.flags = 0;

					const char* flagsStr = strstr(trimmed, "Flags:");
					if (flagsStr)
					{
						UInt32 flags = 0;
						sscanf_s(flagsStr, "Flags: 0x%X", &flags);
						entry.flags = (UInt16)flags;
					}

					data.mapMarkers.push_back(entry);
				}
			}
			else if (strstr(trimmed, "Flags:") && !data.mapMarkers.empty())
			{
				UInt32 flags = 0;
				const char* flagsStr = strstr(trimmed, "Flags:");
				if (flagsStr && sscanf_s(flagsStr, "Flags: 0x%X", &flags) == 1)
				{
					data.mapMarkers.back().flags = (UInt16)flags;
				}
			}
			break;
		}

		case kSection_DialogTopics:
		{
			if (strstr(line, "SAID") && !strstr(line, "NOT_SAID"))
			{
				const char* trimmed = line;
				while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

				const char* infoStart = strstr(trimmed, "info");
				if (infoStart)
				{
					const char* hexStart = strstr(infoStart, "0x");
					if (hexStart)
					{
						UInt32 formID = 0;
						if (sscanf_s(hexStart, "0x%X", &formID) == 1 && formID != 0)
							data.dialogTopicInfosSaid.push_back(formID);
					}
					else
					{
						const char* bracket = strstr(infoStart, "[");
						if (bracket)
						{
							UInt32 formID = 0;
							if (sscanf_s(bracket, "[%X]", &formID) == 1 && formID > 0xFFF)
								data.dialogTopicInfosSaid.push_back(formID);
						}
					}
				}
			}
			break;
		}

		case kSection_PlayerCreatedPotions:
		{
			const char* trimmed = line;
			while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

			if (*trimmed == '[' && strstr(trimmed, " x"))
			{
				UInt32 formID = 0;
				if (sscanf_s(trimmed, "[%X]", &formID) == 1 && formID != 0)
				{
					SaveDumpData::PotionEntry pot;
					memset(&pot, 0, sizeof(pot));
					pot.count = 1;
					pot.weight = 0.5f;

					const char* nameStart = strchr(trimmed, ']');
					const char* xStr = strstr(trimmed, " x");
					if (nameStart && xStr)
					{
						nameStart++;
						while (*nameStart == ' ') nameStart++;
						size_t nLen = xStr - nameStart;
						if (nLen >= sizeof(pot.name)) nLen = sizeof(pot.name) - 1;
						strncpy_s(pot.name, sizeof(pot.name), nameStart, nLen);
						while (nLen > 0 && pot.name[nLen - 1] == ' ') pot.name[--nLen] = '\0';
					}

					if (xStr)
						sscanf_s(xStr, " x%d", &pot.count);

					data.playerCreatedPotions.push_back(pot);
				}
			}
			else if (!data.playerCreatedPotions.empty() && strstr(trimmed, "Weight:"))
			{
				SaveDumpData::PotionEntry& last = data.playerCreatedPotions.back();
				float w = 0; UInt32 v = 0;
				if (sscanf_s(trimmed, "Weight: %f, Value: %u", &w, &v) == 2)
				{
					last.weight = w;
					last.value = v;
				}
			}
			else if (!data.playerCreatedPotions.empty() && strstr(trimmed, "Flags:"))
			{
				SaveDumpData::PotionEntry& last = data.playerCreatedPotions.back();
				UInt32 fl = 0;
				if (sscanf_s(trimmed, "Flags: 0x%X", &fl) == 1)
					last.alchFlags = (UInt8)fl;
			}
			else if (!data.playerCreatedPotions.empty() && strstr(trimmed, "Effect ") && strstr(trimmed, "Magnitude:"))
			{
				SaveDumpData::SpellEffect eff;
				memset(&eff, 0, sizeof(eff));

				char codeStr[8] = {};
				UInt32 effIdx = 0;
				if (sscanf_s(trimmed, "Effect %u: %4s", &effIdx, codeStr, (unsigned)sizeof(codeStr)) >= 2)
				{
					eff.effectCode = (UInt32)codeStr[0]
						| ((UInt32)codeStr[1] << 8)
						| ((UInt32)codeStr[2] << 16)
						| ((UInt32)codeStr[3] << 24);

					const char* magStr = strstr(trimmed, "Magnitude:");
					if (magStr) sscanf_s(magStr, "Magnitude: %u", &eff.magnitude);

					const char* durStr = strstr(trimmed, "Duration:");
					if (durStr) sscanf_s(durStr, "Duration: %u", &eff.duration);

					const char* areaStr = strstr(trimmed, "Area:");
					if (areaStr) sscanf_s(areaStr, "Area: %u", &eff.area);

					const char* rangeStr = strstr(trimmed, "Range:");
					if (rangeStr) sscanf_s(rangeStr, "Range: %u", &eff.range);

					const char* avStr = strstr(trimmed, "ActorValue:");
					if (avStr) sscanf_s(avStr, "ActorValue: %u", &eff.actorValue);

					const char* costStr = strstr(trimmed, "Cost:");
					if (costStr) sscanf_s(costStr, "Cost: %f", &eff.cost);

					data.playerCreatedPotions.back().effects.push_back(eff);
				}
			}
			if (strstr(line, "Total Player-Created"))
			{
				section = kSection_Inventory;
			}
			break;
		}

		case kSection_Appearance:
		{
			UInt32 uVal = 0;
			if (strncmp(line, "Hair:", 5) == 0 && strstr(line, "(none)") == NULL)
			{
				const char* lastParen = strrchr(line, '(');
				if (lastParen && sscanf_s(lastParen, "(0x%X)", &uVal) == 1 && uVal != 0)
				{
					data.hasHairFormID = true;
					data.hairFormID = uVal;
				}
			}
			else if (strncmp(line, "Eyes:", 5) == 0 && strstr(line, "(none)") == NULL)
			{
				const char* lastParen = strrchr(line, '(');
				if (lastParen && sscanf_s(lastParen, "(0x%X)", &uVal) == 1 && uVal != 0)
				{
					data.hasEyesFormID = true;
					data.eyesFormID = uVal;
				}
			}
			else if (strncmp(line, "HairColor:", 10) == 0)
			{
				unsigned r, g, b;
				float fVal;
				if (sscanf_s(line, "HairColor: %u %u %u", &r, &g, &b) == 3)
				{
					data.hasHairColor = true;
					data.hairColorRGB[0] = (UInt8)r;
					data.hairColorRGB[1] = (UInt8)g;
					data.hairColorRGB[2] = (UInt8)b;
				}
				else if (sscanf_s(line, "HairColor: %f", &fVal) == 1)
				{
					data.hasHairColorFloat = true;
					data.hairColorFloat = fVal;
				}
			}
			else if (strncmp(line, "HairLength:", 11) == 0)
			{
				float fVal;
				if (sscanf_s(line, "HairLength: %f", &fVal) == 1)
				{
					data.hasHairLength = true;
					data.hairLength = fVal;
				}
			}
			else
			{
				static const char* fgNames[] = {
					"FaceGenGeometry:", "FaceGenAsymmetry:", "FaceGenTexture:",
					"FaceGenGeometry2:", "FaceGenAsymmetry2:", "FaceGenTexture2:"
				};
				static const char* fgNamesAlt[] = {
					"FaceGenGeometry:", "FaceGenUnk30a:", "FaceGenTexture:",
					"FaceGenGeometry2:", "FaceGenUnk30b:", "FaceGenTexture2:"
				};
				for (int fi = 0; fi < 6; fi++)
				{
					const char* matchName = NULL;
					if (strncmp(line, fgNames[fi], strlen(fgNames[fi])) == 0)
						matchName = fgNames[fi];
					else if (strncmp(line, fgNamesAlt[fi], strlen(fgNamesAlt[fi])) == 0)
						matchName = fgNamesAlt[fi];
					if (matchName)
					{
						const char* p = line + strlen(matchName);
						data.faceGenArrays[fi].clear();
						while (*p)
						{
							while (*p == ' ' || *p == '\t') p++;
							if (*p == '\0' || *p == '\n' || *p == '\r') break;
							char* endp = nullptr;
							float val = strtof(p, &endp);
							if (endp == p) break;
							data.faceGenArrays[fi].push_back(val);
							p = endp;
						}
						_MESSAGE("ImportSaveDump: Parsed %s %u floats",
							matchName, (unsigned)data.faceGenArrays[fi].size());
						break;
					}
				}
			}
			break;
		}

		case kSection_CellItems:
		{
			const char* looseTag = strstr(line, "[Loose]");
			if (looseTag)
			{
				UInt32 refID = 0, baseID = 0;
				const char* refStr = strstr(line, "(ref 0x");
				if (refStr)
					sscanf_s(refStr, "(ref 0x%X, base 0x%X)", &refID, &baseID);
				if (refID != 0)
				{
					SaveDumpData::CellItemEntry entry = {};
					entry.refFormID = refID;
					entry.baseFormID = baseID;
					entry.scale = 1.0f;
					entry.hasScale = false;
					data.cellItems.push_back(entry);
				}
			}
			else if (!data.cellItems.empty())
			{
				SaveDumpData::CellItemEntry& last = data.cellItems.back();
				float a = 0, b = 0, c = 0;
				if (sscanf_s(line, " Pos: %f %f %f", &a, &b, &c) == 3)
					{ last.posX = a; last.posY = b; last.posZ = c; }
				else if (sscanf_s(line, " Rot: %f %f %f", &a, &b, &c) == 3)
					{ last.rotX = a; last.rotY = b; last.rotZ = c; }
				else if (sscanf_s(line, " Scale: %f", &a) == 1)
					{ last.scale = a; last.hasScale = true; }
			}
			break;
		}

		default:
			break;
		}
	}

	_MESSAGE("ImportSaveDump: Parsed %d lines total", lineNum);
	_MESSAGE("ImportSaveDump:   Level=%s(%u) Fame=%s(%d) Infamy=%s(%d) Bounty=%s(%d)",
		data.hasLevel ? "Y" : "N", data.level,
		data.hasFame ? "Y" : "N", data.fame,
		data.hasInfamy ? "Y" : "N", data.infamy,
		data.hasBounty ? "Y" : "N", data.bounty);
	_MESSAGE("ImportSaveDump:   MiscStats=%u Factions=%u Globals=%u",
		(unsigned)data.miscStats.size(), (unsigned)data.factions.size(), (unsigned)data.globals.size());
	_MESSAGE("ImportSaveDump:   GameDaysPassed=%s(%.2f) GameHour=%s(%.2f)",
		data.hasGameDaysPassed ? "Y" : "N", data.gameDaysPassed,
		data.hasGameHour ? "Y" : "N", data.gameHour);
	_MESSAGE("ImportSaveDump:   ActiveQuest=%s(0x%08X) Stage=%s(%u)",
		data.hasActiveQuestFormID ? "Y" : "N", data.activeQuestFormID,
		data.hasActiveQuestStage ? "Y" : "N", (unsigned)data.activeQuestStage);
	_MESSAGE("ImportSaveDump:   InventoryItems=%u BaseGameSpells=%u PlayerCreatedSpells=%u",
		(unsigned)data.inventory.size(), (unsigned)data.baseGameSpells.size(),
		(unsigned)data.playerCreatedSpells.size());
	_MESSAGE("ImportSaveDump:   QuestList=%u InventoryDetail=%u",
		(unsigned)data.questList.size(), (unsigned)data.inventoryDetail.size());
	_MESSAGE("ImportSaveDump:   MapMarkers=%u DialogTopicsSaid=%u PlayerCreatedPotions=%u",
		(unsigned)data.mapMarkers.size(), (unsigned)data.dialogTopicInfosSaid.size(),
		(unsigned)data.playerCreatedPotions.size());
	{
		int fgCount = 0;
		for (int i = 0; i < SaveDumpData::kFaceGenArrayCount; i++)
			if (!data.faceGenArrays[i].empty()) fgCount++;
		_MESSAGE("ImportSaveDump:   Hair=%s(0x%08X) Eyes=%s(0x%08X) HairColor=%s HairLength=%s FaceGenArrays=%d",
			data.hasHairFormID ? "Y" : "N", data.hairFormID,
			data.hasEyesFormID ? "Y" : "N", data.eyesFormID,
			(data.hasHairColor || data.hasHairColorFloat) ? "Y" : "N",
			data.hasHairLength ? "Y" : "N", fgCount);
	}
	_MESSAGE("ImportSaveDump:   CellItems=%u", (unsigned)data.cellItems.size());

	fclose(f);
	return true;
}

// ---- Apply functions ----

static int ApplyLevel(const SaveDumpData& data)
{
	if (!data.hasLevel) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	TESNPC* npc = OBLIVION_CAST(player->baseForm, TESForm, TESNPC);
	if (!npc) return 0;

	SInt16 oldLevel = npc->actorBaseData.level;
	if (oldLevel == (SInt16)data.level)
	{
		_MESSAGE("ImportSaveDump: [Level] already %d, skipping", oldLevel);
		return 0;
	}

	npc->actorBaseData.level = (SInt16)data.level;
	_MESSAGE("ImportSaveDump: [Level] %d -> %d", oldLevel, data.level);
	return 1;
}

static int ApplyFameInfamyBounty(const SaveDumpData& data)
{
	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	int applied = 0;

	if (data.hasFame)
	{
		UInt32 oldBase = player->GetBaseActorValue(kActorVal_Fame);
		if ((int)oldBase != data.fame)
		{
			player->SetActorValue(kActorVal_Fame, (UInt32)data.fame);
			_MESSAGE("ImportSaveDump: [Fame] %u -> %d", oldBase, data.fame);
			applied++;
		}
	}

	if (data.hasInfamy)
	{
		UInt32 oldBase = player->GetBaseActorValue(kActorVal_Infamy);
		if ((int)oldBase != data.infamy)
		{
			player->SetActorValue(kActorVal_Infamy, (UInt32)data.infamy);
			_MESSAGE("ImportSaveDump: [Infamy] %u -> %d", oldBase, data.infamy);
			applied++;
		}
	}

	if (data.hasBounty)
	{
		UInt32 oldBase = player->GetBaseActorValue(kActorVal_Bounty);
		if ((int)oldBase != data.bounty)
		{
			player->SetActorValue(kActorVal_Bounty, (UInt32)data.bounty);
			_MESSAGE("ImportSaveDump: [Bounty] %u -> %d", oldBase, data.bounty);
			applied++;
		}
	}

	return applied;
}

static int ApplyAttributes(const SaveDumpData& data)
{
	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	int applied = 0;
	for (int i = 0; i < 8; i++)
	{
		if (!data.hasAttribute[i]) continue;

		UInt32 avCode = (UInt32)i;
		UInt32 oldBase = player->GetBaseActorValue(avCode);
		UInt32 newBase = (UInt32)data.attributeBase[i];

		if (oldBase != newBase)
		{
			player->SetActorValue(avCode, newBase);
			_MESSAGE("ImportSaveDump: [Attribute] %s base: %u -> %u",
				GetActorValueString(avCode), oldBase, newBase);
			applied++;
		}
	}
	return applied;
}

static int ApplyDerivedStats(const SaveDumpData& data)
{
	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	static const UInt32 derivedAV[] = { kActorVal_Health, kActorVal_Magicka, kActorVal_Fatigue };

	int applied = 0;
	for (int i = 0; i < 3; i++)
	{
		if (!data.hasDerived[i]) continue;

		UInt32 avCode = derivedAV[i];
		UInt32 oldBase = player->GetBaseActorValue(avCode);
		UInt32 newBase = (UInt32)data.derivedBase[i];

		if (oldBase != newBase)
		{
			player->SetActorValue(avCode, newBase);
			_MESSAGE("ImportSaveDump: [DerivedStat] %s base: %u -> %u",
				GetActorValueString(avCode), oldBase, newBase);
			applied++;
		}
	}
	return applied;
}

static int ApplySkills(const SaveDumpData& data)
{
	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	int applied = 0;
	for (int i = 0; i < 21; i++)
	{
		if (!data.hasSkill[i]) continue;

		UInt32 avCode = kActorVal_Armorer + i;
		UInt32 oldBase = player->GetBaseActorValue(avCode);
		UInt32 newBase = (UInt32)data.skillBase[i];

		if (oldBase != newBase)
		{
			player->SetActorValue(avCode, newBase);
			_MESSAGE("ImportSaveDump: [Skill] %s base: %u -> %u",
				GetActorValueString(avCode), oldBase, newBase);
			applied++;
		}
	}
	return applied;
}

static int ApplyMiscStats(const SaveDumpData& data)
{
	if (data.miscStats.empty()) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	int applied = 0;
	for (size_t i = 0; i < data.miscStats.size(); i++)
	{
		const SaveDumpData::MiscStatValue& ms = data.miscStats[i];
		if (ms.index >= PlayerCharacter::kMiscStat_Max) continue;

		UInt32 oldVal = player->miscStats[ms.index];
		if (oldVal != ms.value)
		{
			player->miscStats[ms.index] = ms.value;
			_MESSAGE("ImportSaveDump: [MiscStat] %s: %u -> %u", ms.name, oldVal, ms.value);
			applied++;
		}
	}
	return applied;
}

static int ApplyFactions(const SaveDumpData& data)
{
	if (data.factions.empty()) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player || !player->baseForm) return 0;

	TESActorBaseData* baseData = OBLIVION_CAST(player->baseForm, TESForm, TESActorBaseData);
	if (!baseData) return 0;

	int applied = 0;
	for (size_t fi = 0; fi < data.factions.size(); fi++)
	{
		const SaveDumpData::FactionEntry& fe = data.factions[fi];

		TESActorBaseData::FactionListEntry* entry = &baseData->factionList;
		TESActorBaseData::FactionListEntry* tail = &baseData->factionList;
		bool found = false;

		while (entry)
		{
			if (entry->data && entry->data->faction &&
				entry->data->faction->refID == fe.formID)
			{
				UInt8 oldRank = entry->data->rank;
				if (oldRank != (UInt8)fe.rank)
				{
					entry->data->rank = (UInt8)fe.rank;
					_MESSAGE("ImportSaveDump: [Faction] 0x%08X rank: %d -> %d",
						fe.formID, oldRank, fe.rank);
					applied++;
				}
				found = true;
				break;
			}
			tail = entry;
			entry = entry->next;
		}

		if (!found)
		{
			TESForm* factionForm = LookupFormByID(fe.formID);
			if (!factionForm) continue;

			TESFaction* faction = OBLIVION_CAST(factionForm, TESForm, TESFaction);
			if (!faction) continue;

			TESActorBaseData::FactionListData* newData =
				(TESActorBaseData::FactionListData*)FormHeap_Allocate(sizeof(TESActorBaseData::FactionListData));
			TESActorBaseData::FactionListEntry* newEntry =
				(TESActorBaseData::FactionListEntry*)FormHeap_Allocate(sizeof(TESActorBaseData::FactionListEntry));

			if (!newData || !newEntry)
			{
				if (newData) FormHeap_Free(newData);
				if (newEntry) FormHeap_Free(newEntry);
				continue;
			}

			memset(newData, 0, sizeof(TESActorBaseData::FactionListData));
			memset(newEntry, 0, sizeof(TESActorBaseData::FactionListEntry));

			newData->faction = faction;
			newData->rank = (UInt8)fe.rank;
			newEntry->data = newData;
			newEntry->next = NULL;

			tail->next = newEntry;

			_MESSAGE("ImportSaveDump: [Faction] 0x%08X ADDED rank %d", fe.formID, fe.rank);
			applied++;
		}
	}
	return applied;
}

static int ApplyGlobals(const SaveDumpData& data)
{
	if (data.globals.empty()) return 0;

	int applied = 0;
	for (size_t i = 0; i < data.globals.size(); i++)
	{
		const SaveDumpData::GlobalEntry& ge = data.globals[i];

		TESForm* form = LookupFormByID(ge.formID);
		if (!form || form->typeID != kFormType_Global) continue;

		TESGlobal* global = (TESGlobal*)form;
		float oldVal = global->data;
		if (oldVal != ge.value)
		{
			global->data = ge.value;
			_MESSAGE("ImportSaveDump: [Global] 0x%08X: %.2f -> %.2f", ge.formID, oldVal, ge.value);
			applied++;
		}
	}
	return applied;
}

static int ApplyGameTime(const SaveDumpData& data)
{
	int applied = 0;

	if (data.hasGameDaysPassed)
	{
		TESForm* form = LookupFormByID(0x39);
		if (form && form->typeID == kFormType_Global)
		{
			TESGlobal* global = (TESGlobal*)form;
			float oldVal = global->data;
			if (oldVal != data.gameDaysPassed)
			{
				global->data = data.gameDaysPassed;
				_MESSAGE("ImportSaveDump: [GameTime] DaysPassed: %.2f -> %.2f", oldVal, data.gameDaysPassed);
				applied++;
			}
		}
	}

	if (data.hasGameHour)
	{
		TESForm* form = LookupFormByID(0x38);
		if (form && form->typeID == kFormType_Global)
		{
			TESGlobal* global = (TESGlobal*)form;
			float oldVal = global->data;
			if (oldVal != data.gameHour)
			{
				global->data = data.gameHour;
				_MESSAGE("ImportSaveDump: [GameTime] GameHour: %.2f -> %.2f", oldVal, data.gameHour);
				applied++;
			}
		}
	}

	return applied;
}

static int ApplyActiveQuest(const SaveDumpData& data)
{
	if (!data.hasActiveQuestFormID) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	TESForm* form = LookupFormByID(data.activeQuestFormID);
	if (!form) return 0;

	TESQuest* quest = OBLIVION_CAST(form, TESForm, TESQuest);
	if (!quest) return 0;

	int applied = 0;

	if (player->activeQuest != quest)
	{
		player->activeQuest = quest;
		_MESSAGE("ImportSaveDump: [ActiveQuest] set to 0x%08X", data.activeQuestFormID);
		applied++;
	}

	if (data.hasActiveQuestStage)
	{
		UInt8 oldStage = quest->stageIndex;
		if (oldStage != data.activeQuestStage)
		{
			quest->stageIndex = data.activeQuestStage;
			_MESSAGE("ImportSaveDump: [ActiveQuest] stage: %u -> %u",
				(unsigned)oldStage, (unsigned)data.activeQuestStage);
			applied++;
		}
	}

	return applied;
}

static int ApplyQuestList(const SaveDumpData& data)
{
	if (data.questList.empty()) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	int applied = 0;

	for (size_t i = 0; i < data.questList.size(); i++)
	{
		const SaveDumpData::QuestListEntry& qe = data.questList[i];

		TESForm* form = LookupFormByID(qe.formID);
		if (!form) continue;

		TESQuest* quest = OBLIVION_CAST(form, TESForm, TESQuest);
		if (!quest) continue;

		if (qe.isCompleted)
		{
			quest->questFlags |= TESQuest::kQuestFlag_Completed;

			if (qe.stage > 0)
				quest->stageIndex = qe.stage;

			for (TESQuest::StageEntryList::Iterator iter = quest->stageList.Begin();
				!iter.End(); ++iter)
			{
				TESQuest::StageEntry* se = iter.Get();
				if (!se) continue;
				if (se->index > qe.stage && qe.stage > 0) continue;

				for (TESQuest::StageItemList::Iterator itemIter = se->itemList.Begin();
					!itemIter.End(); ++itemIter)
				{
					QuestStageItem* qsi = itemIter.Get();
					if (!qsi) continue;

					bool alreadyKnown = false;
					for (tList<QuestStageItem>::Iterator knownIter = player->knownQuestStageItems.Begin();
						!knownIter.End(); ++knownIter)
					{
						if (knownIter.Get() == qsi) { alreadyKnown = true; break; }
					}
					if (alreadyKnown) continue;

					if (!qsi->logDate)
					{
						qsi->logDate = (QuestStageItem::LogDate*)FormHeap_Allocate(sizeof(QuestStageItem::LogDate));
						if (qsi->logDate)
						{
							qsi->logDate->dayOfYear = 1;
							qsi->logDate->year = 433;
						}
					}

					qsi->owningQuest = quest;
					player->knownQuestStageItems.AddAt(qsi, 0);
				}
			}

			quest->MarkAsModified(TESQuest::kModified_QuestFlags | TESQuest::kModified_QuestStage);
			applied++;
		}
		else
		{
			if (qe.stage == 0)
			{
				quest->questFlags = qe.questFlags;
				quest->MarkAsModified(TESQuest::kModified_QuestFlags);
				continue;
			}

			quest->questFlags = qe.questFlags;
			quest->stageIndex = qe.stage;

			for (TESQuest::StageEntryList::Iterator iter = quest->stageList.Begin();
				!iter.End(); ++iter)
			{
				TESQuest::StageEntry* se = iter.Get();
				if (!se) continue;
				if (se->index > qe.stage) continue;

				for (TESQuest::StageItemList::Iterator itemIter = se->itemList.Begin();
					!itemIter.End(); ++itemIter)
				{
					QuestStageItem* qsi = itemIter.Get();
					if (!qsi) continue;

					bool alreadyKnown = false;
					for (tList<QuestStageItem>::Iterator knownIter = player->knownQuestStageItems.Begin();
						!knownIter.End(); ++knownIter)
					{
						if (knownIter.Get() == qsi) { alreadyKnown = true; break; }
					}
					if (alreadyKnown) continue;

					if (!qsi->logDate)
					{
						qsi->logDate = (QuestStageItem::LogDate*)FormHeap_Allocate(sizeof(QuestStageItem::LogDate));
						if (qsi->logDate)
						{
							qsi->logDate->dayOfYear = 1;
							qsi->logDate->year = 433;
						}
					}

					qsi->owningQuest = quest;
					player->knownQuestStageItems.AddAt(qsi, 0);
				}
			}

			quest->MarkAsModified(TESQuest::kModified_QuestFlags | TESQuest::kModified_QuestStage);
			applied++;
		}
	}

	_MESSAGE("ImportSaveDump: [QuestList] %d/%u quests applied", applied, (unsigned)data.questList.size());
	return applied;
}

static int ApplyInventoryDetail(const SaveDumpData& data)
{
	if (data.inventoryDetail.empty()) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	ExtraContainerChanges* changes = ExtraContainerChanges::GetForRef(player);
	if (!changes || !changes->data || !changes->data->objList) return 0;

	int applied = 0;

	for (size_t i = 0; i < data.inventoryDetail.size(); i++)
	{
		const SaveDumpData::InventoryDetailEntry& ide = data.inventoryDetail[i];

		for (tList<ExtraContainerChanges::EntryData>::Iterator iter = changes->data->objList->Begin();
			!iter.End(); ++iter)
		{
			ExtraContainerChanges::EntryData* entry = iter.Get();
			if (!entry || !entry->type || entry->type->refID != ide.formID)
				continue;

			if (!entry->extendData)
			{
				entry->extendData = (tList<ExtraDataList>*)FormHeap_Allocate(sizeof(tList<ExtraDataList>));
				if (!entry->extendData) break;
				memset(entry->extendData, 0, sizeof(tList<ExtraDataList>));
			}

			ExtraDataList* edl = NULL;
			for (tList<ExtraDataList>::Iterator edlIter = entry->extendData->Begin();
				!edlIter.End(); ++edlIter)
			{
				edl = edlIter.Get();
				if (edl) break;
			}
			if (!edl)
			{
				edl = ExtraDataList::Create();
				if (!edl) break;
				entry->extendData->AddAt(edl, eListEnd);
			}

			bool itemChanged = false;

			if (ide.health >= 0.0f)
			{
				ExtraHealth* xHealth = (ExtraHealth*)edl->GetByType(kExtraData_Health);
				if (xHealth)
				{
					if (xHealth->health != ide.health) { xHealth->health = ide.health; itemChanged = true; }
				}
				else
				{
					xHealth = ExtraHealth::Create();
					if (xHealth) { xHealth->health = ide.health; edl->Add(xHealth); itemChanged = true; }
				}
			}

			if (ide.charge >= 0.0f)
			{
				ExtraCharge* xCharge = (ExtraCharge*)edl->GetByType(kExtraData_Charge);
				if (xCharge)
				{
					if (xCharge->charge != ide.charge) { xCharge->charge = ide.charge; itemChanged = true; }
				}
				else
				{
					xCharge = ExtraCharge::Create();
					if (xCharge) { xCharge->charge = ide.charge; edl->Add(xCharge); itemChanged = true; }
				}
			}

			if (ide.soul != 0xFF)
			{
				ExtraSoul* xSoul = (ExtraSoul*)edl->GetByType(kExtraData_Soul);
				if (xSoul)
				{
					if (xSoul->soul != ide.soul) { xSoul->soul = ide.soul; itemChanged = true; }
				}
				else
				{
					xSoul = ExtraSoul::Create();
					if (xSoul) { xSoul->soul = ide.soul; edl->Add(xSoul); itemChanged = true; }
				}
			}

			if (itemChanged) applied++;
			break;
		}
	}

	_MESSAGE("ImportSaveDump: [InvDetail] %d/%u items updated", applied, (unsigned)data.inventoryDetail.size());
	return applied;
}

static int ApplyInventory(const SaveDumpData& data)
{
	if (data.inventory.empty()) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	ExtraContainerChanges* changes = ExtraContainerChanges::GetForRef(player);
	if (!changes || !changes->data || !changes->data->objList) return 0;

	int applied = 0;

	for (size_t i = 0; i < data.inventory.size(); i++)
	{
		const SaveDumpData::InventoryItem& item = data.inventory[i];
		if (item.count == 0) continue;

		bool found = false;
		for (tList<ExtraContainerChanges::EntryData>::Iterator iter = changes->data->objList->Begin();
			!iter.End(); ++iter)
		{
			ExtraContainerChanges::EntryData* entry = iter.Get();
			if (entry && entry->type && entry->type->refID == item.formID)
			{
				SInt32 oldCount = entry->countDelta;
				if (oldCount != item.count)
				{
					entry->countDelta = item.count;
					_MESSAGE("ImportSaveDump: [Inventory] 0x%08X count: %d -> %d",
						item.formID, oldCount, item.count);
					applied++;
				}
				found = true;
				break;
			}
		}

		if (!found)
		{
			TESForm* form = LookupFormByID(item.formID);
			if (!form) continue;

			ExtraContainerChanges::EntryData* newEntry = ExtraContainerChanges::EntryData::Create(item.count, form);
			if (newEntry)
			{
				changes->data->objList->AddAt(newEntry, eListEnd);
				_MESSAGE("ImportSaveDump: [Inventory] 0x%08X x%d (new)", item.formID, item.count);
				applied++;
			}
		}
	}

	_MESSAGE("ImportSaveDump: [Inventory] %d/%u items applied", applied, (unsigned)data.inventory.size());
	return applied;
}

static void AddSpellToList(TESSpellList* spellList, TESForm* spellForm)
{
	if (!spellList->spellList.type)
	{
		spellList->spellList.type = spellForm;
	}
	else
	{
		TESSpellList::Entry* newEntry =
			(TESSpellList::Entry*)FormHeap_Allocate(sizeof(TESSpellList::Entry));
		if (newEntry)
		{
			newEntry->type = spellForm;
			newEntry->next = NULL;
			TESSpellList::Entry* tail = &spellList->spellList;
			while (tail->next) tail = tail->next;
			tail->next = newEntry;
		}
	}
}

static int ApplySpells(const SaveDumpData& data)
{
	PlayerCharacter* player = *g_thePlayer;
	if (!player || !player->baseForm) return 0;

	TESSpellList* spellList = OBLIVION_CAST(player->baseForm, TESForm, TESSpellList);
	if (!spellList) return 0;

	int applied = 0;

	if (!data.baseGameSpells.empty())
	{
		for (size_t i = 0; i < data.baseGameSpells.size(); i++)
		{
			const SaveDumpData::SpellEntry& sp = data.baseGameSpells[i];

			TESForm* form = LookupFormByID(sp.formID);
			if (!form) continue;

			bool alreadyKnown = false;
			for (TESSpellList::Entry* entry = &spellList->spellList; entry; entry = entry->next)
			{
				if (entry->type && entry->type->refID == sp.formID)
				{
					alreadyKnown = true;
					break;
				}
			}

			if (alreadyKnown) continue;

			AddSpellToList(spellList, form);
			applied++;
			_MESSAGE("ImportSaveDump: [Spells] added base 0x%08X", sp.formID);
		}
	}

	if (!data.playerCreatedSpells.empty())
	{
		for (size_t i = 0; i < data.playerCreatedSpells.size(); i++)
		{
			const SaveDumpData::PlayerCreatedSpell& pcs = data.playerCreatedSpells[i];

			if (pcs.effects.empty()) continue;

			SpellItem* newSpell = (SpellItem*)CreateFormInstance(kFormType_Spell);
			if (!newSpell) continue;

			newSpell->spellType = pcs.spellType;
			newSpell->magickaCost = pcs.magickaCost;
			newSpell->masteryLevel = pcs.masteryLevel;
			newSpell->spellFlags = pcs.spellFlags;

			newSpell->magicItem.name.Set(pcs.name);
			newSpell->magicItem.list.RemoveAllItems();

			for (size_t e = 0; e < pcs.effects.size(); e++)
			{
				const SaveDumpData::SpellEffect& se = pcs.effects[e];

				EffectSetting* effSetting = EffectSetting::EffectSettingForC(se.effectCode);
				if (!effSetting) continue;

				EffectItem* effItem = EffectItem::Create(se.effectCode);
				if (!effItem) continue;

				effItem->magnitude = se.magnitude;
				effItem->duration = se.duration;
				effItem->area = se.area;
				effItem->range = se.range;
				effItem->actorValueOrOther = se.actorValue;
				effItem->cost = se.cost;
				effItem->setting = effSetting;

				newSpell->magicItem.list.AddItem(effItem);
			}

			if (newSpell->magicItem.list.CountItems() == 0) continue;

			AddFormToDataHandler(*g_dataHandler, newSpell);
			AddFormToCreatedBaseObjectsList(*g_createdBaseObjList, newSpell);

			AddSpellToList(spellList, newSpell);
			applied++;

			_MESSAGE("ImportSaveDump: [Spells] reconstructed '%s' (0x%08X -> 0x%08X) with %u effects",
				pcs.name, pcs.formID, newSpell->refID,
				newSpell->magicItem.list.CountItems());
		}
	}

	return applied;
}

static int ApplyMapMarkers(const SaveDumpData& data)
{
	if (data.mapMarkers.empty()) return 0;

	int applied = 0;

	for (size_t i = 0; i < data.mapMarkers.size(); i++)
	{
		const SaveDumpData::MapMarkerEntry& entry = data.mapMarkers[i];

		TESForm* form = LookupFormByID(entry.refFormID);
		if (!form) continue;

		TESObjectREFR* ref = OBLIVION_CAST(form, TESForm, TESObjectREFR);
		if (!ref) continue;

		ExtraMapMarker* mm = (ExtraMapMarker*)ref->baseExtraList.GetByType(kExtraData_MapMarker);
		if (!mm || !mm->data) continue;

		UInt16 oldFlags = mm->data->flags;

		if (entry.flags & ExtraMapMarker::kFlag_Visible)
			mm->data->flags |= ExtraMapMarker::kFlag_Visible;

		if (entry.flags & ExtraMapMarker::kFlag_CanTravel)
			mm->data->flags |= ExtraMapMarker::kFlag_CanTravel;

		if (mm->data->flags != oldFlags)
		{
			_MESSAGE("ImportSaveDump: [MapMarker] 0x%08X flags 0x%02X -> 0x%02X",
				entry.refFormID, oldFlags, mm->data->flags);
			applied++;
		}
	}

	_MESSAGE("ImportSaveDump: [MapMarkers] %d/%u markers applied",
		applied, (unsigned)data.mapMarkers.size());
	return applied;
}

static int ApplyDialogTopics(const SaveDumpData& data)
{
	if (data.dialogTopicInfosSaid.empty()) return 0;

	int applied = 0;

	for (size_t i = 0; i < data.dialogTopicInfosSaid.size(); i++)
	{
		UInt32 formID = data.dialogTopicInfosSaid[i];

		TESForm* form = LookupFormByID(formID);
		if (!form) continue;

		if (form->typeID != kFormType_DialogInfo) continue;

		UInt8 oldVal = *((UInt8*)form + 0x22);
		if (oldVal == 0)
		{
			*((UInt8*)form + 0x22) = 1;
			applied++;
		}
	}

	_MESSAGE("ImportSaveDump: [DialogTopics] %d infos set to SAID (%u total parsed)",
		applied, (unsigned)data.dialogTopicInfosSaid.size());
	return applied;
}

static int ApplyPlayerCreatedPotions(const SaveDumpData& data)
{
	if (data.playerCreatedPotions.empty()) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	ExtraContainerChanges* changes = ExtraContainerChanges::GetForRef(player);
	if (!changes || !changes->data || !changes->data->objList) return 0;

	int applied = 0;

	for (size_t i = 0; i < data.playerCreatedPotions.size(); i++)
	{
		const SaveDumpData::PotionEntry& pot = data.playerCreatedPotions[i];

		if (pot.effects.empty()) continue;

		AlchemyItem* newPotion = (AlchemyItem*)CreateFormInstance(kFormType_AlchemyItem);
		if (!newPotion) continue;

		newPotion->magicItem.name.Set(pot.name);
		newPotion->weight.weight = pot.weight;
		newPotion->goldValue = pot.value;

		UInt32 alchFlags = pot.alchFlags ? pot.alchFlags : AlchemyItem::kAlchemy_NoAutocalc;
		newPotion->moreFlags = alchFlags;

		newPotion->magicItem.list.RemoveAllItems();

		for (size_t e = 0; e < pot.effects.size(); e++)
		{
			const SaveDumpData::SpellEffect& se = pot.effects[e];

			EffectSetting* effSetting = EffectSetting::EffectSettingForC(se.effectCode);
			if (!effSetting) continue;

			EffectItem* effItem = EffectItem::Create(se.effectCode);
			if (!effItem) continue;

			effItem->magnitude = se.magnitude;
			effItem->duration = se.duration;
			effItem->area = se.area;
			effItem->range = se.range;
			effItem->actorValueOrOther = se.actorValue;
			effItem->cost = se.cost;
			effItem->setting = effSetting;

			newPotion->magicItem.list.AddItem(effItem);
		}

		if (newPotion->magicItem.list.CountItems() == 0) continue;

		AddFormToDataHandler(*g_dataHandler, newPotion);
		AddFormToCreatedBaseObjectsList(*g_createdBaseObjList, newPotion);

		ExtraContainerChanges::EntryData* newEntry =
			ExtraContainerChanges::EntryData::Create(pot.count, newPotion);
		if (newEntry)
		{
			changes->data->objList->AddAt(newEntry, eListEnd);
			applied++;
			_MESSAGE("ImportSaveDump: [Potions] created '%s' x%d (0x%08X) with %u effects",
				pot.name, pot.count, newPotion->refID,
				newPotion->magicItem.list.CountItems());
		}
	}

	_MESSAGE("ImportSaveDump: [Potions] %d/%u potions created",
		applied, (unsigned)data.playerCreatedPotions.size());
	return applied;
}

// ---- Apply appearance (hair, eyes, hair color, hair length, face gen morphs) ----
static int ApplyAppearance(const SaveDumpData& data)
{
	int applied = 0;
	PlayerCharacter* player = *g_thePlayer;
	if (!player || !player->baseForm) return 0;

	TESNPC* npc = OBLIVION_CAST(player->baseForm, TESForm, TESNPC);
	if (!npc) return 0;

	if (data.hasHairFormID)
	{
		TESForm* newHairForm = LookupFormByID(data.hairFormID);
		if (newHairForm)
		{
			TESHair* newHair = OBLIVION_CAST(newHairForm, TESForm, TESHair);
			if (newHair) { npc->hair = newHair; applied++; }
		}
	}

	if (data.hasEyesFormID)
	{
		TESForm* newEyesForm = LookupFormByID(data.eyesFormID);
		if (newEyesForm)
		{
			TESEyes* newEyes = OBLIVION_CAST(newEyesForm, TESForm, TESEyes);
			if (newEyes) { npc->eyes = newEyes; applied++; }
		}
	}

	if (data.hasHairColor)
	{
		npc->hairColorRGB[0] = data.hairColorRGB[0];
		npc->hairColorRGB[1] = data.hairColorRGB[1];
		npc->hairColorRGB[2] = data.hairColorRGB[2];
		applied++;
	}
	else if (data.hasHairColorFloat)
	{
		UInt8 gray = (UInt8)(data.hairColorFloat * 255.0f);
		npc->hairColorRGB[0] = gray;
		npc->hairColorRGB[1] = gray;
		npc->hairColorRGB[2] = gray;
		applied++;
	}

	if (data.hasHairLength)
	{
		*(float*)&npc->hairLength = data.hairLength;
		applied++;
	}

	// FaceGen morph data
	struct FGArrayDef { UInt32 baseOff; const char* label; };
	static const FGArrayDef fgDefs[] = {
		{0x108, "FaceGenGeometry"},
		{0x120, "FaceGenAsymmetry"},
		{0x138, "FaceGenTexture"},
		{0x168, "FaceGenGeometry2"},
		{0x180, "FaceGenAsymmetry2"},
		{0x198, "FaceGenTexture2"},
	};

	UInt8* npcBytes = (UInt8*)npc;
	for (int ai = 0; ai < 6; ai++)
	{
		if (data.faceGenArrays[ai].empty()) continue;

		__try
		{
			UInt32 dataPtrOff = fgDefs[ai].baseOff + 0x0C;
			UInt32 endPtrOff  = fgDefs[ai].baseOff + 0x10;
			UInt32 dataPtr = *(UInt32*)(npcBytes + dataPtrOff);
			UInt32 endPtr  = *(UInt32*)(npcBytes + endPtrOff);

			if (!dataPtr || endPtr <= dataPtr || IsBadReadPtr((void*)dataPtr, 4))
				continue;

			UInt32 existingCount = (endPtr - dataPtr) / 4;
			UInt32 importCount = (UInt32)data.faceGenArrays[ai].size();
			UInt32 writeCount = (importCount < existingCount) ? importCount : existingCount;

			float* dest = (float*)dataPtr;
			for (UInt32 fi = 0; fi < writeCount; fi++)
				dest[fi] = data.faceGenArrays[ai][fi];

			_MESSAGE("ImportSaveDump: [Appearance] %s: wrote %u/%u floats",
				fgDefs[ai].label, writeCount, importCount);
			applied++;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			_MESSAGE("ImportSaveDump: [Appearance] Exception writing %s", fgDefs[ai].label);
		}
	}

	return applied;
}

// ---- Havok physics sleeping helper ----
static bool PutPhysicsToSleep(TESObjectREFR* ref)
{
	if (!ref || !ref->niNode) return false;

	NiObject* collisionNiObj = ref->niNode->m_spCollisionObject;
	if (!collisionNiObj) return false;

	NiRTTI* collRTTI = collisionNiObj->GetType();
	if (collRTTI != NiRTTI_bhkCollisionObject &&
		collRTTI != NiRTTI_bhkNiCollisionObject &&
		collRTTI != NiRTTI_bhkBlendCollisionObject &&
		collRTTI != NiRTTI_bhkBlendCollisionObjectAddRotation)
		return false;

	bhkNiCollisionObject* bhkCollObj = (bhkNiCollisionObject*)collisionNiObj;
	void* worldObjPtr = bhkCollObj->bhkWorldObject;
	if (!worldObjPtr) return false;

	NiObject* worldNiObj = (NiObject*)worldObjPtr;
	NiRTTI* bodyRTTI = worldNiObj->GetType();
	if (bodyRTTI != NiRTTI_bhkRigidBody && bodyRTTI != NiRTTI_bhkRigidBodyT)
		return false;

	float zeroVel[3] = { 0.0f, 0.0f, 0.0f };
	ThisStdCall(0x004d9960, (bhkRigidBody*)worldObjPtr, (float*)zeroVel);
	ThisStdCall(0x004d99e0, (bhkRigidBody*)worldObjPtr, (float*)zeroVel);

	hkReferencedObject* hkObj = ((bhkRefObject*)worldObjPtr)->hkObj;
	if (hkObj)
		ThisStdCall(0x008a6440, hkObj);

	return true;
}

// ---- Deferred physics freeze for spawned items ----
static std::vector<UInt32> s_pendingPhysicsFreeze;
static int s_physicsRetryCount = 0;

static bool DeferredPhysicsFreezeTask()
{
	s_physicsRetryCount++;
	if (s_pendingPhysicsFreeze.empty()) return true;

	for (auto it = s_pendingPhysicsFreeze.begin(); it != s_pendingPhysicsFreeze.end(); ) {
		TESForm* form = LookupFormByID(*it);
		if (!form) {
			it = s_pendingPhysicsFreeze.erase(it);
			continue;
		}
		TESObjectREFR* ref = OBLIVION_CAST(form, TESForm, TESObjectREFR);
		if (ref && ref->niNode) {
			PutPhysicsToSleep(ref);
			it = s_pendingPhysicsFreeze.erase(it);
		} else {
			++it;
		}
	}

	if (s_pendingPhysicsFreeze.empty()) {
		s_physicsRetryCount = 0;
		return true;
	}

	if (s_physicsRetryCount > 60) {
		s_pendingPhysicsFreeze.clear();
		s_physicsRetryCount = 0;
		return true;
	}

	return false;
}

// ---- Apply cell items ----
static int ApplyCellItems(const SaveDumpData& data)
{
	if (data.cellItems.empty()) return 0;

	PlayerCharacter* player = *g_thePlayer;
	if (!player) return 0;

	TESObjectCELL* playerCell = player->parentCell;
	if (!playerCell) return 0;

	int applied = 0;

	for (size_t i = 0; i < data.cellItems.size(); i++)
	{
		const SaveDumpData::CellItemEntry& entry = data.cellItems[i];

		TESForm* existingForm = LookupFormByID(entry.refFormID);
		if (existingForm)
		{
			TESObjectREFR* ref = OBLIVION_CAST(existingForm, TESForm, TESObjectREFR);
			if (ref && ref->parentCell == playerCell && ref->niNode)
			{
				float dx = ref->posX - entry.posX;
				float dy = ref->posY - entry.posY;
				float dz = ref->posZ - entry.posZ;
				if (dx*dx + dy*dy + dz*dz < 25.0f) continue;

				ref->posX = entry.posX;
				ref->posY = entry.posY;
				ref->posZ = entry.posZ;
				ref->rotX = entry.rotX;
				ref->rotY = entry.rotY;
				ref->rotZ = entry.rotZ;
				if (entry.hasScale) ref->scale = entry.scale;

				ref->niNode->m_localTranslate.x = entry.posX;
				ref->niNode->m_localTranslate.y = entry.posY;
				ref->niNode->m_localTranslate.z = entry.posZ;
				if (entry.hasScale) ref->niNode->m_fLocalScale = entry.scale;

				PutPhysicsToSleep(ref);
				applied++;
				continue;
			}
		}

		// Spawn new ref
		TESForm* baseForm = LookupFormByID(entry.baseFormID);
		if (!baseForm) continue;

		__try
		{
			player->AddItem(baseForm, NULL, 1);

			float dropPos[3] = { entry.posX, entry.posY, entry.posZ };
			float dropRot[3] = { entry.rotX, entry.rotY, entry.rotZ };
			player->RemoveItem(baseForm, NULL, 1, 0, 1, NULL, dropPos, dropRot, 0, 0);

			for (TESObjectCELL::ObjectListEntry* cur = &playerCell->objectList; cur; cur = cur->next)
			{
				TESObjectREFR* droppedRef = cur->refr;
				if (droppedRef && droppedRef->baseForm == baseForm)
				{
					float dx = droppedRef->posX - entry.posX;
					float dy = droppedRef->posY - entry.posY;
					float dz = droppedRef->posZ - entry.posZ;
					if (dx*dx + dy*dy + dz*dz < 10000.0f)
					{
						if (droppedRef->niNode)
							PutPhysicsToSleep(droppedRef);
						else
							s_pendingPhysicsFreeze.push_back(droppedRef->refID);
						break;
					}
				}
			}

			applied++;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// Enqueue deferred physics freeze
	if (!s_pendingPhysicsFreeze.empty())
	{
		s_physicsRetryCount = 0;
		PluginAPI::EnqueueTask(DeferredPhysicsFreezeTask);
	}

	_MESSAGE("ImportSaveDump: [CellItem] %d/%u items applied", applied, (unsigned)data.cellItems.size());
	return applied;
}

// ---- Main command ----
bool Cmd_ImportSaveDump_Execute(COMMAND_ARGS)
{
	*result = 0;

	_MESSAGE("ImportSaveDump: === COMMAND INVOKED ===");

	char dumpPath[MAX_PATH];
	if (!GetSaveDumpPath(dumpPath, sizeof(dumpPath)))
	{
		_MESSAGE("ImportSaveDump: Failed to get save dump path");
		return true;
	}

	_MESSAGE("ImportSaveDump: Reading from %s", dumpPath);

	{
		FILE* checkF = NULL;
		if (fopen_s(&checkF, dumpPath, "r") == 0 && checkF)
		{
			fseek(checkF, 0, SEEK_END);
			long fileSize = ftell(checkF);
			fclose(checkF);
			_MESSAGE("ImportSaveDump: File size = %ld bytes", fileSize);
		}
		else
		{
			_MESSAGE("ImportSaveDump: File does NOT exist or cannot be opened");
			return true;
		}
	}

	SaveDumpData data;
	if (!ParseSaveDump(dumpPath, data))
	{
		_MESSAGE("ImportSaveDump: ParseSaveDump returned false");
		return true;
	}

	int fieldsApplied = 0;

	fieldsApplied += ApplyLevel(data);
	fieldsApplied += ApplyFameInfamyBounty(data);
	fieldsApplied += ApplyAttributes(data);
	fieldsApplied += ApplyDerivedStats(data);
	fieldsApplied += ApplySkills(data);
	fieldsApplied += ApplyMiscStats(data);
	fieldsApplied += ApplyFactions(data);
	fieldsApplied += ApplyGlobals(data);
	fieldsApplied += ApplyGameTime(data);
	fieldsApplied += ApplyActiveQuest(data);
	fieldsApplied += ApplyQuestList(data);
	fieldsApplied += ApplyInventory(data);
	fieldsApplied += ApplyInventoryDetail(data);
	fieldsApplied += ApplySpells(data);
	fieldsApplied += ApplyMapMarkers(data);
	fieldsApplied += ApplyDialogTopics(data);
	fieldsApplied += ApplyPlayerCreatedPotions(data);
	fieldsApplied += ApplyCellItems(data);

	// Apply appearance
	{
		int n = ApplyAppearance(data);
		fieldsApplied += n;
		if (n > 0)
		{
			PlayerCharacter* pc = *g_thePlayer;
			if (pc)
			{
				bool wasFirstPerson = !pc->IsThirdPerson();
				if (wasFirstPerson)
					pc->TogglePOV(false);
				pc->Update3D();
				_MESSAGE("ImportSaveDump: Update3D() called");
				if (wasFirstPerson)
					pc->TogglePOV(true);
			}
		}
	}

	_MESSAGE("ImportSaveDump: === TOTAL: %d field(s) applied ===", fieldsApplied);

	*result = (double)fieldsApplied;

	if (IsConsoleMode())
		Console_Print("ImportSaveDump: %d field(s) applied", fieldsApplied);

	return true;
}

#else
// Editor stub
bool Cmd_ImportSaveDump_Execute(COMMAND_ARGS)
{
	*result = 0;
	return true;
}
#endif  // OBLIVION

// ---- Command definition ----
static CommandInfo kCommandInfo_ImportSaveDump =
{
	"ImportSaveDump",
	"",
	0,
	"Reads target.txt and applies values to the player",
	0,
	0,
	NULL,
	HANDLER(Cmd_ImportSaveDump_Execute),
	NULL,
	NULL,
	0
};

// ---- Plugin entry points ----

extern "C" {

bool OBSEPlugin_Query(const OBSEInterface * obse, PluginInfo * info)
{
	_MESSAGE("varla_import: query");

	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "varla_import";
	info->version = 1;

	if(!obse->isEditor)
	{
		if(obse->obseVersion < OBSE_VERSION_INTEGER)
		{
			_ERROR("OBSE version too old (got %u expected at least %u)", obse->obseVersion, OBSE_VERSION_INTEGER);
			return false;
		}

#if OBLIVION
		if(obse->oblivionVersion != OBLIVION_VERSION)
		{
			_ERROR("incorrect Oblivion version (got %08X need %08X)", obse->oblivionVersion, OBLIVION_VERSION);
			return false;
		}
#endif
	}

	return true;
}

bool OBSEPlugin_Load(const OBSEInterface * obse)
{
	_MESSAGE("varla_import: load");

	g_pluginHandle = obse->GetPluginHandle();

	// Use opcode base 0x2800 (private/development range)
	obse->SetOpcodeBase(0x2800);
	obse->RegisterCommand(&kCommandInfo_ImportSaveDump);
	_MESSAGE("varla_import: ImportSaveDump registered");

	if(!obse->isEditor)
	{
		g_scriptInterface = (OBSEScriptInterface*)obse->QueryInterface(kInterface_Script);
	}

	return true;
}

};
