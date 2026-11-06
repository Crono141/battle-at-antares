#pragma once

#include <cstdint>
#include <string>
#include <vector>

// WP3.2b: native commands from the input script and the control channel (ANTARES_INPUT_SCRIPT,
// ANTARES_CONTROL, tools/antares-mcp), so the SDL build can drive native screens headless and the
// rules replay can compare native and legacy runs. The control channel attaches as the frontend
// (native::Attach_Control). Platform thread only. Steps:
//   native_claim <screen> <0|1>        claim SCREEN_* <screen> for the native loop (or release it)
//   native_requests <kind,kind|none>   answer these request kinds ("message,confirm,warning,choose")
//   native <json>                      send a command or query; waits until its result arrives or
//                                      a request is pending (300 s)
//   native_send <json>                 send without waiting (when a legacy popup must be clicked first)
//   native_answer <value>              answer the pending request (waits for one, 60 s): 1 OK/Yes,
//                                      0 No, the option for "choose", -1 to cancel
// Results, requests and snapshots are noted as "native result {...}", "native request {...}" and
// "native snapshot {...}" (control replies and the log).
namespace antares::native_script {

    enum class e_step { not_mine,
        done,
        wait };

    /** One script step. `wait` means call again with the same step later. */
    e_step Step(const std::string& command, const std::string& text, int a, int b, uint64_t now_ms, std::vector<std::string>* notes);

    /** Collects results, requests and snapshots that arrived, once a native step has attached the
     *  control channel (never under a frontend that hasn't). Call every poll. */
    void Poll(std::vector<std::string>* notes);

} // namespace antares::native_script
