# Varla Import — OBSE Plugin for Oblivion Character Transfer

![Varla Stone](stone.png)

An OBSE plugin that imports character data from one save to another save in The Elder Scrolls IV: Oblivion.

It reads a text-based save dump file and applies the character data to the current player.

## What it does

The plugin registers two script commands:

- **`ExportSave`** — exports the current player's data to `save_dump.txt`
- **`ImportSave`** — imports character data from `target.txt` into the current player

### ExportSave

Dumps the current player's character data to `My Documents\My Games\Oblivion\OBSE\save_dump.txt`. The export is controlled by `varla.ini` in the same folder — each section can be toggled on/off. If `varla.ini` doesn't exist, a default one is generated with `bDumpPlayerCharacter=1` and all other sections disabled.

Exported sections include: player identity, appearance (hair, eyes, FaceGen morphs), position, character info (class, race, birthsign, equipped items), fame/infamy/bounty, game time, global variables, misc statistics, active quest, quest list, quest script variables, factions, attributes, derived stats, skills, magic resistances, spells (including player-created), inventory (including player-created potions), active magic effects, status effects, skill XP progress, AV modifiers, quick keys, cell items, and plugin list.

### ImportSave

When called (via console or spell script), it reads `target.txt` from your OBSE data folder and applies the following to the current player:

### Character Stats
- **Level** — sets player level
- **Attributes** — Strength, Intelligence, Willpower, Agility, Speed, Endurance, Personality, Luck (base values)
- **Derived Stats** — Health, Magicka, Fatigue (base values)
- **Skills** — all 21 skills (Armorer through Speechcraft, base values)
- **Fame, Infamy, Bounty**

### Progression
- **Quests** — restores active and completed quests with their stages and flags, adds journal entries
- **Active Quest** — sets the currently tracked quest
- **Misc Statistics** — Days in Prison, Creatures Killed, Places Discovered, Books Read, Nirnroots Found, and 30+ other stats
- **Factions** — adds faction memberships with correct ranks
- **Map Markers** — discovers map markers and sets their travel flags
- **Dialog Topics** — marks topic infos as SAID so NPCs don't repeat introductions

### Items & Magic
- **Inventory** — adds all items with correct counts
- **Inventory Detail** — restores item health/condition, enchantment charge, and soul gem levels
- **Spells** — adds known spells (base game spells by form ID)
- **Player-Created Spells** — fully reconstructs custom spells with all effects, costs, and mastery levels
- **Player-Created Potions** — reconstructs custom potions with effects, weight, and value

### World
- **Global Variables** — restores game globals (quest state trackers, rent flags, fame counters, etc.)
- **Game Time** — sets days passed and current hour
- **Cell Items** — repositions loose items on the ground (books on shelves, weapons on racks, etc.) and spawns missing ones with physics deactivation so they stay in place

### Appearance
- **Hair and Eyes** — sets hair style and eye type by form ID
- **Hair Color** — supports both RGB (original Oblivion) and single-float (Remastered) formats
- **Hair Length**
- **FaceGen Morphs** — restores all 6 face geometry/texture arrays (FGGS, FGGA, FGTS and their secondary variants) for full face reconstruction
- Triggers a 3D model rebuild after appearance changes so they take effect immediately

## Input file format

The plugin reads: `My Documents\My Games\Oblivion\OBSE\target.txt`

This is a plain text dump with `=== SECTION NAME ===` headers. It's designed to be compatible with dumps from both obse64 (Oblivion Remastered) and xOBSE (original Oblivion). The parser handles format differences between the two automatically:

- Form IDs: both `[HEXID]` (xOBSE) and `(0xHEXID)` (obse64) formats
- Hair color: both `R G B` (3 integers) and single float formats
- FaceGen labels: both `FaceGenAsymmetry` and `FaceGenUnk30a` names
- Quest sections: both flat and hierarchical layouts

## How to use in-game

### Export (via console)
Open the console (`~`) and type:
```
ExportSave
```
This writes `save_dump.txt` to your OBSE data folder.

### Import (via console)
```
ImportSave
```

### Via spell script
Create a spell with a script effect that calls `ImportSave` or `ExportSave`. The return value is the number of fields that were changed (import) or 1 on success (export).

## Building from source

### Prerequisites
- Visual Studio 2019 or later (needs the v145 or compatible platform toolset)
- xOBSE source code (from [GitHub](https://github.com/llde/xOBSE) or [Nexus](https://www.nexusmods.com/oblivion/mods/53668))

### Steps

1. Clone or download the xOBSE source:
   ```
   git clone https://github.com/llde/xOBSE.git
   ```

2. Copy the `varla_import_plugin` folder into the xOBSE root directory:
   ```
   xOBSE/
     common/
     obse/
     obse_plugin_example/
     varla_import_plugin/    <-- here
   ```

3. Open `varla_import_plugin/varla_import.sln` in Visual Studio

4. Build with **Release | Win32**

5. The output DLL is at `varla_import_plugin/Release/varla_import.dll`

### Installation

Copy `varla_import.dll` to:
```
<Oblivion Install>/Data/OBSE/Plugins/varla_import.dll
```

OBSE will load it automatically on game startup.

## Logging

The plugin writes its own log file at:
```
<Oblivion Install>/varla_import.log
```

Every import operation logs what was parsed, what was applied, and what was skipped (with before/after values), which is useful for debugging.

## Credits

- [xOBSE](https://github.com/llde/xOBSE) — the script extender this plugin is built on
- [OBSE](https://obse.silverlock.org/) — the original Oblivion Script Extender

## License

This plugin is open source. The OBSE source files it compiles against are subject to their own license terms.
