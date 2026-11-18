# Changelog

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Rules in encounters/*.ini per map, unit, point, team and chance: Replace, Remove, Set, Extra.
- A changed point spawns again with the change; a removed point is emptied.
- Console enc.list, enc.reload.

### Fixed (2026-11-18)
- A Replace unit is kept loaded (root set).
- Remove now holds on a point the mission spawns later (AutoSpawn off, as on every enemy point of a real mission):
  the point is watched and emptied each time it spawns. Before, Remove only switched AutoSpawn off, which those
  points already were, so the mission still brought the unit (found on SideMission_SeasideSkirmish).
- Extra on such a point waits for the point's own spawn instead of putting the unit there at map load.
- The per-point rule sets are cleared at each map load, so a point whose BeginPlay never finished cannot leave its
  address behind for a new object on the next map (2026-11-18, cached-pointer source gate); built, not yet tested.
