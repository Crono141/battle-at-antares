#pragma once

// WP3.2b (D25): the one table of commands and queries that native views use. The contract (verbs,
// arguments, results, errors, snapshots) is notes/antares/native-bridge.md, "The interface".
//
// Every entry point that drives the game without screen state goes through Run(): the native loop
// (frontends such as the Godot host), the control channel's `native` steps (tools/antares-mcp and
// input scripts, so the SDL build can run them headless) and the unit tests. Game thread only.
//
// Commands take plain arguments by game id ("scrap ships 3, 7, 9"), never screen state, and call
// the same rules functions the legacy screen calls (in argument form, D25). Queries only read: a
// query that changes the game is a bug, and the read-only guard reports it (Guard_Enabled).

#include <cstdint>
#include <string>
#include <vector>

struct yyjson_val;
struct yyjson_mut_doc;
struct yyjson_mut_val;

namespace antares::commands {

    // Plain arguments: one JSON object, read by name.
    class s_args {
      public:
        explicit s_args(yyjson_val* object) : object_(object) {}
        bool Has(const char* key) const;
        // False when the key is missing or not an integer.
        bool Int(const char* key, int64_t* value) const;
        bool Str(const char* key, std::string* value) const;
        // False when the key is missing, not an array, or holds anything but integers.
        bool Int_List(const char* key, std::vector<int64_t>* values) const;

      private:
        yyjson_val* object_;
    };

    // The result object; "id" and "ok" are filled in by Run.
    class s_out {
      public:
        s_out(yyjson_mut_doc* doc, yyjson_mut_val* object) : doc_(doc), object_(object) {}
        void Int(const char* key, int64_t value);
        void Bool(const char* key, bool value);
        void Str(const char* key, const std::string& value);
        // Nested values: an array of objects, or an array of integers.
        s_out Add_Object(const char* key);
        s_out Append_Object(); // on an object made by Add_List
        s_out Add_List(const char* key);
        void Append_Int(int64_t value);
        // An error code from the contract ("bad_args", "refused", ...) and a readable message.
        void Fail(const char* code, const std::string& message);
        bool Failed() const {
            return failed_;
        }

      private:
        yyjson_mut_doc* doc_;
        yyjson_mut_val* object_;
        bool failed_ = false;
    };

    enum class e_kind { command,
        query };

    // Screens a verb may run on: ANY, or a bit per SCREEN_* value.
    constexpr uint64_t ANY_SCREEN = ~uint64_t{0};
    constexpr uint64_t Screen_Bit(int16_t screen) {
        return uint64_t{1} << screen;
    }

    struct s_entry {
        const char* name; // the verb (commands) or the "what" (queries)
        e_kind kind;
        uint64_t screens; // where it may run
        int32_t version;  // queries: bumped when the result's shape changes
        const char* args; // for the contract listing, e.g. "ships: [ship id]"
        void (*run)(int16_t screen, const s_args& args, s_out& out);
    };

    // Runs one command or query (JSON text) for a native screen; returns the result JSON:
    // {"id": id, "ok": true|false, ...fields} or {"id", "ok": false, "error": code, "message": text}.
    std::string Run(int16_t screen, uint64_t id, const std::string& json);

    // Runs a query by name, for snapshots: the result fields plus "version", or "" when unknown.
    std::string Run_Query(int16_t screen, const std::string& what);

    // The table, for the contract listing and tests.
    const std::vector<s_entry>& Table();

    // ---- The read-only guard ----
    // On in debug builds, and in any build with ANTARES_QUERY_GUARD=1. Around every query it
    // compares the game state hash (the save the game would write), the rules seed and the number
    // of random draws; a change fails the query with "query_wrote_state" and logs an error.
    bool Guard_Enabled();
    // How many queries the guard has caught since start.
    uint64_t Guard_Violations();

    // ---- Test seams ----
    void Set_Guard_For_Test(bool on);
    // Replaces the state probe (hash, seed, draws) with a function returning one number.
    void Set_State_Probe_For_Test(uint64_t (*probe)());
    // Adds an entry (or replaces one with the same name and kind) for the rest of the process.
    void Register_For_Test(const s_entry& entry);

} // namespace antares::commands
