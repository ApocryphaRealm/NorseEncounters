// NorseEncounters - who you fight in each mission, by rule (UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood), 2026-11-17.
// Norse places every enemy through a SpawnPoint actor in the mission's encounter maps; the point holds a data object
// (SpawnUnitData) naming the unit to spawn (UnitData, a UnitDataAsset). Rules change that per map, unit, point, team and
// chance - swap the unit, leave the point empty, set the point's fields, or spawn the unit extra times - so packs make
// fights larger, mix factions or bring a new enemy into the existing missions. Applied as each spawn point begins play,
// streamed encounter maps included. Survey: RESEARCH-more-frameworks.md (#14).
//
// encounters\*.ini (NorseManifest order). Each [section] is one rule; its name is only a label:
//   [Docks get a heavier brute]
//   Map = Borre_ADebtOfBlood_Encounter1_*         ; the map's name (wildcards, -exclude); empty = every map
//   Unit = *Brigand*, -*Boss*                     ; the unit the point spawns now (its asset name)
//   Point = SpawnPoint_3                          ; the spawn point's name (optional)
//   Team = 1                                      ; SpawnTeamID (optional)
//   Chance = 50                                   ; per point, the same roll every time the map loads
//   Replace = /Game/.../UnitData_X.UnitData_X     ; spawn this unit instead
//   Remove = true                                 ; spawn nothing here
//   Set = OverrideCombatRatingScale=150           ; any field of the point (or of its unit data object); repeatable
//   Extra = 1                                     ; spawn the unit this many more times once the point has spawned
// Rules run in order; a later rule's Unit filter sees an earlier rule's Replace. Console: enc.reload, enc.list.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseFilters.hpp"

#include <map>
#include <mutex>
#include <set>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod, g_out;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    struct Rule
    {
        std::wstring label;
        Filter maps, units, points;
        int team = -1, chance = 100, extra = 0;
        std::wstring replace;
        bool remove = false;
        std::vector<std::pair<std::wstring, std::wstring>> sets;
        std::string where;
    };
    std::vector<Rule> g_rules;

    void LoadRules()
    {
        g_rules.clear();
        fs::path dir = g_mod / "encounters";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        nm::Manifest man;
        man.Resolve(g_mod.parent_path().parent_path());
        files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        for (const auto& f : files)
        {
            Rule* cur = nullptr;
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty()) continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                if (l.front() == L'[' && l.back() == L']')
                {
                    g_rules.push_back({});
                    cur = &g_rules.back();
                    cur->label = Trim(l.substr(1, l.size() - 2));
                    cur->where = where;
                    continue;
                }
                size_t eq = l.find(L'=');
                if (!cur || eq == std::wstring::npos)
                {
                    NRN_WARN("%s: not a [rule] or key = value line - skipped", where.c_str());
                    continue;
                }
                std::wstring k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
                if (k == L"map") cur->maps = ParseFilter(v);
                else if (k == L"unit") cur->units = ParseFilter(v);
                else if (k == L"point") cur->points = ParseFilter(v);
                else if (k == L"team") cur->team = v.empty() ? -1 : _wtoi(v.c_str());
                else if (k == L"chance") cur->chance = ParseChance(v);
                else if (k == L"replace") cur->replace = v;
                else if (k == L"remove") cur->remove = _wcsicmp(v.c_str(), L"true") == 0 || v == L"1";
                else if (k == L"extra") cur->extra = std::clamp(_wtoi(v.c_str()), 0, 8);
                else if (k == L"set")
                {
                    size_t e2 = v.find(L'=');
                    if (e2 == std::wstring::npos) NRN_WARN("%s: Set needs Field=Value - skipped", where.c_str());
                    else cur->sets.push_back({Trim(v.substr(0, e2)), Trim(v.substr(e2 + 1))});
                }
                else NRN_WARN("%s: unknown key '%s' (Map, Unit, Point, Team, Chance, Replace, Remove, Set, Extra)", where.c_str(), Narrow(k).c_str());
            }
        }
        g_rules.erase(std::remove_if(g_rules.begin(), g_rules.end(), [](const Rule& r) {
                          bool none = r.replace.empty() && !r.remove && r.sets.empty() && !r.extra;
                          if (none) NRN_WARN("[%s] (%s) changes nothing (no Replace, Remove, Set or Extra) - left out", Narrow(r.label).c_str(), r.where.c_str());
                          return none;
                      }),
                      g_rules.end());
        NRN_INFO("encounter rules: %zu from %zu file(s)", g_rules.size(), files.size());
    }

    bool Pass(const Filter& f, const std::wstring& s)
    {
        std::wstring l = Lower(s);
        if (!f.inc.empty() && std::none_of(f.inc.begin(), f.inc.end(), [&](auto& p) { return Glob(p.c_str(), l.c_str()); })) return false;
        return std::none_of(f.exc.begin(), f.exc.end(), [&](auto& p) { return Glob(p.c_str(), l.c_str()); });
    }

    // ---- the spawn point and what it spawns
    UClass* SpawnPointClass()
    {
        static UClass* c = reinterpret_cast<UClass*>(FindObj(L"/Script/Norse.SpawnPoint"));
        return c;
    }
    bool IsSpawnPoint(UObject* a)
    {
        UClass* sp = SpawnPointClass();
        if (!sp || !a) return false;
        for (UStruct* c = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(a)); c; c = c->GetInheritanceSuper())
            if (c == reinterpret_cast<UStruct*>(sp)) return true;
        return false;
    }

    std::wstring PathOf(UObject* o)
    {
        std::wstring full = o->GetFullName(nullptr);
        return full.substr(full.find(L' ') + 1);
    }
    // /Game/.../Borre_ADebtOfBlood_Encounter1_Docks.Borre_ADebtOfBlood_Encounter1_Docks:PersistentLevel.SpawnPoint_3
    std::wstring MapOf(const std::wstring& path)
    {
        size_t colon = path.find(L':');
        std::wstring pkg = path.substr(0, colon);
        size_t dot = pkg.find(L'.');
        pkg = pkg.substr(0, dot);
        return pkg.substr(pkg.find_last_of(L'/') + 1);
    }

    struct Field
    {
        UObject* owner = nullptr;
        FProperty* prop = nullptr;
        void* addr = nullptr;
        explicit operator bool() const { return prop != nullptr; }
    };
    Field FieldOf(UObject* o, const std::wstring& name)
    {
        if (!o) return {};
        FProperty* p = FindProp(reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(o)), name);
        if (!p) return {};
        return {o, p, reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal()};
    }
    UObject* ObjectIn(const Field& f)
    {
        UObject* v = nullptr;
        if (f) std::memcpy(&v, f.addr, sizeof(v));
        return v;
    }
    // The point's unit field: SpawnUnitData.UnitData (or .CombatUnit on the older data object)
    Field UnitField(UObject* point)
    {
        UObject* data = ObjectIn(FieldOf(point, L"SpawnUnitData"));
        if (!data) return {};
        if (Field f = FieldOf(data, L"UnitData")) return f;
        return FieldOf(data, L"CombatUnit");
    }
    std::wstring UnitName(UObject* point)
    {
        UObject* u = ObjectIn(UnitField(point));
        return u ? ShortName(PathOf(u)) : L"";
    }
    std::wstring Read(UObject* o, const wchar_t* name)
    {
        Field f = FieldOf(o, name);
        return f ? Export(f.prop, f.addr, o) : L"";
    }
    bool Write(const Field& f, const std::wstring& text)
    {
        bool faulted = false;
        return SehImport(f.prop, text.c_str(), f.addr, f.owner, &faulted) && !faulted;
    }

    std::mutex g_mx;
    std::map<UObject*, int> g_extra;     // points waiting to spawn extra units once their own spawn is done
    std::set<UObject*> g_removed;        // points told to spawn nothing
    std::set<UObject*> g_respawn;        // points whose unit or fields changed: spawn again with the change
    int g_seen = 0, g_changed = 0;

    void ApplyTo(UObject* point)
    {
        std::wstring path = PathOf(point), map = MapOf(path), name = ShortName(path);
        int team = _wtoi(Read(point, L"SpawnTeamID").c_str());
        ++g_seen;
        std::string done;
        for (const Rule& r : g_rules)
        {
            std::wstring unit = UnitName(point);
            if (!Pass(r.maps, map) || !Pass(r.units, unit) || !Pass(r.points, name)) continue;
            if (r.team >= 0 && r.team != team) continue;
            if (r.chance < 100 && static_cast<int>(Fnv(map + L"|" + name + L"|" + r.label) % 100) >= r.chance) continue;
            if (!r.replace.empty())
            {
                Field f = UnitField(point);
                UObject* u = LoadByPath(r.replace.c_str());
                if (!f) NRN_WARN("[%s] %s: no unit data object to replace", Narrow(r.label).c_str(), Narrow(name).c_str());
                else if (!u) NRN_WARN("[%s] Replace %s: not found", Narrow(r.label).c_str(), Narrow(r.replace).c_str());
                else
                {
                    std::memcpy(f.addr, &u, sizeof(u));
                    done += " " + Narrow(unit) + "->" + Narrow(ShortName(r.replace));
                    std::lock_guard lk(g_mx);
                    g_respawn.insert(point);
                }
            }
            for (const auto& [field, value] : r.sets)
            {
                Field f = FieldOf(point, field);
                if (!f) f = FieldOf(ObjectIn(FieldOf(point, L"SpawnUnitData")), field);
                if (!f) NRN_WARN("[%s] Set %s: no such field on %s or its unit data", Narrow(r.label).c_str(), Narrow(field).c_str(), Narrow(name).c_str());
                else if (!Write(f, value)) NRN_WARN("[%s] Set %s=%s: Unreal did not accept the value", Narrow(r.label).c_str(), Narrow(field).c_str(), Narrow(value).c_str());
                else
                {
                    done += " " + Narrow(field) + "=" + Narrow(value);
                    std::lock_guard lk(g_mx);
                    g_respawn.insert(point);
                }
            }
            if (r.remove)
            {
                if (Field f = FieldOf(point, L"AutoSpawn")) Write(f, L"False");
                std::lock_guard lk(g_mx);
                g_removed.insert(point);
                done += " removed";
            }
            if (r.extra)
            {
                std::lock_guard lk(g_mx);
                g_extra[point] += r.extra;
                done += " +" + std::to_string(r.extra) + " extra";
            }
        }
        if (!done.empty())
        {
            ++g_changed;
            NRN_INFO("%s %s (%s, team %d):%s", Narrow(map).c_str(), Narrow(name).c_str(), Narrow(UnitName(point)).c_str(), team, done.c_str());
        }
        else NRN_DEBUG("%s %s (%s, team %d): no rule", Narrow(map).c_str(), Narrow(name).c_str(), Narrow(UnitName(point)).c_str(), team);
    }

    UObject* SpawnOnce(UObject* point)
    {
        uint16_t ps = Reflect::Get().ParmsSizeOf(point, L"SpawnActor");
        if (ps != 8) return nullptr;
        Buf b;
        if (!Reflect::Get().Call(point, L"SpawnActor", b.b, 8, sizeof(b.b))) return nullptr;
        UObject* out = nullptr;
        std::memcpy(&out, b.b, 8);
        return out;
    }

    // Norse spawns a point's unit before the point's BeginPlay (as the level's actors are set up), so a change made at
    // BeginPlay-pre is too late for the unit already standing there. After the point's BeginPlay: a removed point loses
    // what it spawned; a changed point (Replace, Set) loses it and spawns again with the change; then the extras.
    int DestroySpawned(UObject* point)
    {
        Buf b;
        if (Reflect::Get().ParmsSizeOf(point, L"DestroyAllSpawned") != 4 || !Reflect::Get().Call(point, L"DestroyAllSpawned", b.b, 4, sizeof(b.b))) return -1;
        int32_t n = 0;
        std::memcpy(&n, b.b, 4);
        return n;
    }

    void AfterBeginPlay(UObject* point)
    {
        bool removed = false, respawn = false;
        int extra = 0;
        {
            std::lock_guard lk(g_mx);
            removed = g_removed.erase(point) > 0;
            respawn = g_respawn.erase(point) > 0;
            if (auto it = g_extra.find(point); it != g_extra.end())
            {
                extra = it->second;
                g_extra.erase(it);
            }
        }
        std::string name = Narrow(ShortName(PathOf(point)));
        if (removed)
        {
            int n = DestroySpawned(point);
            NRN_INFO("%s: emptied (%d unit(s) removed)", name.c_str(), n);
            return;
        }
        if (respawn)
        {
            int n = DestroySpawned(point);
            if (n > 0)
            {
                UObject* a = SpawnOnce(point);
                if (a) NRN_INFO("%s: respawned with the change (%d removed, %s now)", name.c_str(), n, Narrow(ShortName(PathOf(a))).c_str());
                else NRN_WARN("%s: %d unit(s) removed, but SpawnActor gave nothing", name.c_str(), n);
            }
            // n == 0: the point had not spawned yet - it will spawn with the change
        }
        for (int i = 0; i < extra; ++i)
        {
            UObject* a = SpawnOnce(point);
            if (a) NRN_INFO("%s: extra unit %d/%d spawned (%s)", name.c_str(), i + 1, extra, Narrow(ShortName(PathOf(a))).c_str());
            else NRN_WARN("%s: extra unit %d/%d - SpawnActor gave nothing", name.c_str(), i + 1, extra);
        }
    }

    void ListPoints()
    {
        std::vector<UObject*> pts;
        try
        {
            RC::Unreal::UObjectGlobals::FindAllOf(L"SpawnPoint", pts);
        }
        catch (...)
        {
        }
        int n = 0;
        for (UObject* p : pts)
        {
            std::wstring path = PathOf(p);
            if (ShortName(path).rfind(L"Default__", 0) == 0) continue;
            ++n;
            NRN_INFO("point %s | %s | unit %s | team %s | AutoSpawn %s | data %s", Narrow(MapOf(path)).c_str(), Narrow(ShortName(path)).c_str(),
                     Narrow(UnitName(p)).c_str(), Narrow(Read(p, L"SpawnTeamID")).c_str(), Narrow(Read(p, L"AutoSpawn")).c_str(),
                     Narrow(ShortName(Read(p, L"SpawnUnitData"))).c_str());
        }
        NRN_INFO("enc.list: %d spawn point(s); this map so far: %d looked at, %d changed", n, g_seen, g_changed);
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Lower(Trim(c));
        NRN_INFO("command: %s", Narrow(cmd).c_str());
        if (cmd == L"enc.reload") LoadRules();
        else if (cmd == L"enc.list") ListPoints();
        else NRN_WARN("unknown command - enc.reload | enc.list");
        return true;
    }
} // namespace

class NorseEncounters : public RC::CppUserModBase
{
  public:
    NorseEncounters()
    {
        ModName = L"NorseEncounters";
        ModVersion = L"" NEN_VERSION;
        ModDescription = L"Who you fight in each mission, by rule: swap, remove or add the enemies a mission's spawn points bring.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        std::wstring dir;
        for (const auto& raw : ReadLines(g_mod / "NorseEncounters.ini"))
        {
            std::wstring l = Trim(raw);
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
        }
        g_out = dir.empty() ? g_mod : fs::path(dir);
        Log::Get().Open(g_out / "NorseEncounters.log", 2);
        Log::Get().Write(Level::Error, "NorseEncounters v%s", NEN_VERSION);
        LoadRules();
    }

    auto on_unreal_init() -> void override
    {
        try
        {
            RC::Unreal::Hook::RegisterBeginPlayPreCallback([](RC::Unreal::AActor* a) {
                auto* o = reinterpret_cast<UObject*>(a);
                if (g_rules.empty() || !IsSpawnPoint(o)) return;
                try
                {
                    ApplyTo(o);
                }
                catch (...)
                {
                    NRN_ERROR("spawn point rule pass threw");
                }
            });
            RC::Unreal::Hook::RegisterBeginPlayPostCallback([](RC::Unreal::AActor* a) {
                auto* o = reinterpret_cast<UObject*>(a);
                {
                    std::lock_guard lk(g_mx);
                    if (!g_removed.count(o) && !g_extra.count(o) && !g_respawn.count(o)) return;
                }
                try
                {
                    AfterBeginPlay(o);
                }
                catch (...)
                {
                    NRN_ERROR("spawn point after-begin pass threw");
                }
            });
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                NRN_INFO("map load: %d spawn point(s) looked at, %d changed before it", g_seen, g_changed);
                g_seen = g_changed = 0;
            });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"enc.", 4) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseEncounters();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
