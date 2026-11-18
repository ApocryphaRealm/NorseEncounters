NorseEncounters rules
=====================

Every *.ini here changes who a mission's spawn points bring into the fight. Each [section] is one rule (its name is
only a label); rules run in load order, and nothing in the game's files changes.

    [Docks get a brute]
    Map = Borre_ADebtOfBlood_Encounter1_*      ; the map's name (wildcards, -exclude); empty = every map
    Unit = Outcast_Nithing                     ; the unit the point spawns now (asset name, wildcards)
    Point = SpawnPoint_3                       ; the spawn point's name (optional)
    Team = 1                                   ; the point's team (optional)
    Chance = 50                                ; rolled per point, the same every time the map loads
    Replace = /Game/Norse/Core/Data/CombatUnits/EnemyTypes/Outcasts/Outcast_Brute.Outcast_Brute
    Remove = true                              ; this point spawns nothing
    Set = OverrideCombatRatingScale=150        ; any field of the point or its unit data (repeatable)
    Extra = 1                                  ; spawn the unit this many more times (0-8)

A later rule's Unit filter sees an earlier rule's Replace. Console: enc.list (every loaded spawn point: map, name,
unit, team), enc.reload (rules apply as each spawn point starts, so reload the map afterwards).
