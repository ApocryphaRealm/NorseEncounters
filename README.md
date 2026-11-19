# NorseEncounters

Who you fight in each mission, by rule, for the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0). Norse places
every enemy through a SpawnPoint actor in the mission's encounter maps (`Maps/Missions/.../*_Encounter*.umap`). Rules
change those points per map, unit, point, team and chance, so packs can make fights larger, mix factions or bring a
new enemy into the existing missions without editing a map. Survey: `RESEARCH-more-frameworks.md` (#14).

```ini
[Docks get a brute]
Map = Borre_ADebtOfBlood_Encounter1_*
Unit = Outcast_Nithing
Chance = 50
Replace = /Game/Norse/Core/Data/CombatUnits/EnemyTypes/Outcasts/Outcast_Brute.Outcast_Brute

[One more spearman]
Unit = Outcast_Spearman
Extra = 1
```

Keys: `Map`, `Unit`, `Point` (wildcards, `-exclude`), `Team`, `Chance` (rolled per point, the same on every load);
actions `Replace` (another unit asset), `Remove = true`, `Set = Field=Value` (any field of the point or its unit data
object, repeatable), `Extra = n` (0-8 more of the point's unit). Rules run in load order (NorseManifest), and a later
rule's Unit filter sees an earlier Replace. Author guide: `package\encounters\README.txt`; the tested rules:
`docs\examples\test-enc.ini`.

How it works: the game spawns a point's unit while the level's actors are set up, before the point's BeginPlay. The
rules run at the point's BeginPlay (UE4SS BeginPlay hook, streamed encounter maps included) and change its data; right
after, a removed point loses what it spawned (`DestroyAllSpawned`), a changed point (Replace or Set) loses it and spawns
again with the change (`SpawnActor`), and extras are spawned the same way. Unit data lives in
`SpawnUnitData` (`SpawnPointDataObject_CombatUnit.CombatUnit` or `SpawnPointDataObject_UnitData.UnitData`).

Console: `enc.list` (every loaded spawn point: map, name, unit, team, AutoSpawn), `enc.reload` (rules apply as points
start, so reload the map afterwards). NorseDevTools runs `enc.reload` when a file in `encounters\` is saved.

0.1.0 tested in game on the combat test map (TEST-RESULT-0.1.0.txt). Not yet seen: a mission's own encounter maps, and
whether Norse's mission scripts that address a point's spawned unit by reference cope with a respawned one.

## Licence

GPL-3.0-or-later (see LICENSE).
