#include "pch.h"

#include "antares/native_script.h"

#include "antares/native.h"

namespace antares::native_script {
    namespace {
        uint64_t s_sent_id = 0; // the `native` step's command, while it waits
        uint64_t s_sent_ms = 0;
        bool s_result_seen = false;
        uint64_t s_request_id = 0; // the request waiting for native_answer
        uint64_t s_answer_ms = 0;  // when native_answer started waiting

        uint64_t Id_Of(const std::string& json) {
            const size_t at = json.find("\"id\":");
            return at == std::string::npos ? 0 : strtoull(json.c_str() + at + 5, nullptr, 10);
        }

        std::vector<std::string> Split_Kinds(const std::string& text) {
            std::vector<std::string> kinds;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find(',', start);
                if (end == std::string::npos) {
                    end = text.size();
                }
                const std::string kind = text.substr(start, end - start);
                if (!kind.empty() && kind != "none") {
                    kinds.push_back(kind);
                }
                start = end + 1;
            }
            return kinds;
        }

        void Note(std::vector<std::string>* notes, const std::string& text) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "input script: %s", text.c_str());
            notes->push_back(text);
        }
    } // namespace

    void Poll(std::vector<std::string>* notes) {
        // Only the control channel's own traffic: under a Godot host the frontend takes these.
        if (!native::Control_Attached()) {
            return;
        }
        std::string json;
        while (native::Take_Result(&json)) {
            if (s_sent_id != 0 && Id_Of(json) == s_sent_id) {
                s_result_seen = true;
            }
            Note(notes, "native result " + json);
        }
        native::s_request request;
        while (native::Take_Request(&request)) {
            s_request_id = request.id;
            Note(notes, "native request " + request.json);
        }
        while (native::Take_Snapshot(&json)) {
            Note(notes, "native snapshot " + json);
        }
    }

    e_step Step(const std::string& command, const std::string& text, int a, int b, uint64_t now_ms, std::vector<std::string>* notes) {
        if (command == "native_claim") {
            native::Attach_Control(true);
            native::Claim_Screen((int16_t)a, b != 0);
            Note(notes, "native_claim " + std::to_string(a) + " " + std::to_string(b));
            return e_step::done;
        }
        if (command == "native_requests") {
            native::Attach_Control(true);
            native::Claim_Requests(Split_Kinds(text));
            Note(notes, "native_requests " + text);
            return e_step::done;
        }
        if (command == "native_send") {
            native::Attach_Control(true);
            Note(notes, "native_send " + std::to_string(native::Send_Command(text)));
            return e_step::done;
        }
        if (command == "native") {
            if (s_sent_id == 0) {
                native::Attach_Control(true);
                s_sent_id = native::Send_Command(text);
                s_sent_ms = now_ms;
                s_result_seen = false;
                return e_step::wait;
            }
            Poll(notes);
            const bool timed_out = now_ms - s_sent_ms > 300000;
            if (!s_result_seen && s_request_id == 0 && !timed_out) {
                return e_step::wait;
            }
            if (timed_out) {
                Note(notes, "native FAILED: no result for command " + std::to_string(s_sent_id));
            }
            s_sent_id = 0;
            return e_step::done;
        }
        if (command == "native_answer") {
            if (s_answer_ms == 0) {
                s_answer_ms = now_ms;
            }
            Poll(notes);
            if (s_request_id == 0 && !native::Request_Pending()) {
                if (now_ms - s_answer_ms <= 60000) {
                    return e_step::wait;
                }
                Note(notes, "native_answer FAILED: no request");
                s_answer_ms = 0;
                return e_step::done;
            }
            if (s_request_id == 0) {
                return e_step::wait; // pending, but not taken yet
            }
            native::Answer_Request(s_request_id, a);
            Note(notes, "native_answer " + std::to_string(s_request_id) + " " + std::to_string(a));
            s_request_id = 0;
            s_answer_ms = 0;
            return e_step::done;
        }
        return e_step::not_mine;
    }

} // namespace antares::native_script
