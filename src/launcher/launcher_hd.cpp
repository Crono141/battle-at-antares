#include "pch.h"

#include "launcher/launcher_app.h"

#include <imgui.h>

#include <algorithm>

#include "launcher/launcher_chrome.h"

// The HD art packs in the launcher (WP5.14, owner 2026-11-24: offered with their download sizes,
// and reachable from the launcher after the wizard). The file work is antares::hd_packs.
namespace launcher {
    namespace hd = antares::hd_packs;

    namespace {
        std::string Size_(uint64_t bytes) {
            char text[32];
            if (bytes >= (uint64_t(1) << 30)) {
                snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / static_cast<double>(uint64_t(1) << 30));
            } else {
                snprintf(text, sizeof(text), "%llu MB", static_cast<unsigned long long>((bytes + (1 << 20) - 1) >> 20));
            }
            return text;
        }
    } // namespace

    void c_launcher::Hd_List_() {
        if (hd_.listing || hd_.listed) return;
        if (hd_.list_worker.joinable()) hd_.list_worker.join();
        hd_.listing = true;
        hd_.list_done = false;
        hd_.list_error.clear();
        hd_.mods_folder = hd::Mods_Folder(game_dir_);
        hd_.list_worker = std::thread([this]() {
            std::string text, error, tag;
            std::vector<hd::s_pack> packs;
            const bool ok = hd::Fetch_Text(hd::RELEASE_API_URL, &text, &error) && hd::Parse_Release(text, &packs, &tag, &error);
            std::lock_guard<std::mutex> lock(hd_.mutex);
            if (ok) {
                hd_.packs = packs;
                hd_.tag = tag;
                hd_.ticked.assign(packs.size(), 0);
            } else {
                hd_.list_error = "Couldn't get the list of HD art from GitHub: " + error + ".";
            }
            hd_.list_done = true;
        });
    }

    bool c_launcher::Hd_Any_Ticked_() const {
        for (uint8_t ticked : hd_.ticked) {
            if (ticked) return true;
        }
        return false;
    }

    void c_launcher::Hd_Start_() {
        {
            std::lock_guard<std::mutex> lock(hd_.mutex);
            for (size_t index = 0; index < hd_.ticked.size(); ++index) {
                if (hd_.ticked[index] && std::find(hd_.queue.begin(), hd_.queue.end(), index) != hd_.queue.end()) continue;
                if (hd_.ticked[index] && static_cast<int>(index) != hd_.current) hd_.queue.push_back(index);
                hd_.ticked[index] = 0;
            }
            hd_.error.clear();
        }
        if (hd_.busy || hd_.queue.empty()) return;
        if (hd_.worker.joinable()) hd_.worker.join();
        hd_.busy = true;
        hd_.progress.cancel = false;
        hd_.worker = std::thread([this]() {
            for (;;) {
                hd::s_pack pack;
                {
                    std::lock_guard<std::mutex> lock(hd_.mutex);
                    if (hd_.queue.empty() || hd_.progress.cancel) {
                        hd_.queue.clear();
                        hd_.current = -1;
                        break;
                    }
                    hd_.current = static_cast<int>(hd_.queue.front());
                    hd_.queue.erase(hd_.queue.begin());
                    pack = hd_.packs[static_cast<size_t>(hd_.current)];
                }
                hd_.progress.done = 0;
                std::string error;
                const bool ok = hd::Install(pack, hd_.mods_folder, &hd_.progress, &hd_.phase, &hd_.phase_mutex, &error);
                SDL_Log("launcher: HD art %s: %s", pack.file.c_str(), ok ? "installed" : error.c_str());
                std::lock_guard<std::mutex> lock(hd_.mutex);
                if (ok) {
                    hd_.finished.push_back(pack.id);
                } else {
                    hd_.error = error == "cancelled" ? "Stopped. What was downloaded is kept: Download carries on from there."
                                                     : pack.title + ": " + error + ".";
                    hd_.queue.clear();
                    hd_.current = -1;
                    break;
                }
            }
            hd_.busy = false;
        });
    }

    bool c_launcher::Download_Hd_Pack(const std::string& id) {
        Hd_List_();
        while (!hd_.list_done) SDL_Delay(50);
        Hd_Poll_();
        for (size_t index = 0; index < hd_.packs.size(); ++index) {
            if (hd_.packs[index].id == id) {
                hd_.ticked[index] = 1;
                Hd_Start_();
                return true;
            }
        }
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "launcher: no HD pack '%s' (%s)", id.c_str(), hd_.list_error.c_str());
        return false;
    }

    void c_launcher::Hd_Poll_() {
        if (hd_.listing && hd_.list_done) {
            if (hd_.list_worker.joinable()) hd_.list_worker.join();
            hd_.listing = false;
            hd_.listed = hd_.list_error.empty();
        }
        std::vector<std::string> finished;
        {
            std::lock_guard<std::mutex> lock(hd_.mutex);
            finished.swap(hd_.finished);
        }
        if (!finished.empty()) {
            // A new pack is a new package: list it, and switch it on in this profile.
            Refresh_Packages_();
            for (const std::string& id : finished) (void)Set_Package_Enabled(id, true, true);
            std::string names;
            for (const std::string& id : finished) names += (names.empty() ? "" : ", ") + Package_Name_(id);
            message_ = "HD art ready and switched on: " + names + ".";
            message_is_error_ = false;
        }
        if (!hd_.busy && hd_.worker.joinable()) hd_.worker.join();
    }

    void c_launcher::Hd_Stop_() {
        hd_.progress.cancel = true;
        if (hd_.worker.joinable()) hd_.worker.join();
        if (hd_.list_worker.joinable()) hd_.list_worker.join();
    }

    std::string c_launcher::Hd_Status_() {
        if (!hd_.busy) return "";
        std::lock_guard<std::mutex> lock(hd_.mutex);
        if (hd_.current < 0) return "";
        const hd::s_pack& pack = hd_.packs[static_cast<size_t>(hd_.current)];
        std::string phase;
        {
            std::lock_guard<std::mutex> phase_lock(hd_.phase_mutex);
            phase = hd_.phase;
        }
        const uint64_t total = std::max<uint64_t>(pack.size, 1);
        const int percent = static_cast<int>(std::min<uint64_t>(100, hd_.progress.done * 100 / total));
        std::string text = (phase.empty() ? std::string("Downloading") : phase) + " " + pack.title + ": " + std::to_string(percent) + " % of " + Size_(pack.size);
        if (!hd_.queue.empty()) text += ", then " + std::to_string(hd_.queue.size()) + " more";
        return text;
    }

    // The packs, one row each: a tick, the name and size, what it is, and its state.
    void c_launcher::Draw_Hd_List_() {
        Hd_List_();
        if (hd_.listing) {
            ImGui::TextDisabled("Asking GitHub for the HD art...");
            return;
        }
        if (!hd_.list_error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(chrome::RED, "%s", hd_.list_error.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Try again")) {
                hd_.listed = false;
                Hd_List_();
            }
            return;
        }
        std::lock_guard<std::mutex> lock(hd_.mutex);
        for (size_t index = 0; index < hd_.packs.size(); ++index) {
            const hd::s_pack& pack = hd_.packs[index];
            const hd::e_state state = hd::State(pack, hd_.mods_folder);
            const bool queued = std::find(hd_.queue.begin(), hd_.queue.end(), index) != hd_.queue.end();
            const bool current = static_cast<int>(index) == hd_.current;
            const bool can_tick = !queued && !current && (state == hd::NOT_INSTALLED || state == hd::UPDATE);
            ImGui::PushID(static_cast<int>(index));
            bool ticked = hd_.ticked[index] != 0;
            ImGui::BeginDisabled(!can_tick);
            if (ImGui::Checkbox("##pack", &ticked)) hd_.ticked[index] = ticked ? 1 : 0;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::PushFont(chrome::Ready() ? chrome::Bold_Font() : nullptr, 0.0f);
            ImGui::TextUnformatted(pack.title.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", Size_(pack.size).c_str());
            if (current) {
                std::string phase;
                {
                    std::lock_guard<std::mutex> phase_lock(hd_.phase_mutex);
                    phase = hd_.phase;
                }
                const float fraction = static_cast<float>(static_cast<double>(hd_.progress.done) / static_cast<double>(std::max<uint64_t>(pack.size, 1)));
                const std::string label = phase + " " + std::to_string(static_cast<int>(fraction * 100.0f)) + " %";
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, chrome::AMBER);
                ImGui::ProgressBar(std::min(fraction, 1.0f), ImVec2(ImGui::GetFontSize() * 12.0f, 0.0f), label.c_str());
                ImGui::PopStyleColor();
            } else {
                // The state, after the size on the same line.
                const char* note = queued     ? "waiting"
                    : state == hd::INSTALLED  ? "installed"
                    : state == hd::UPDATE     ? "update available"
                    : state == hd::OTHER_COPY ? "installed (your own copy)"
                    : pack.recommended        ? "start with this one"
                                              : nullptr;
                const ImVec4 colour = state == hd::INSTALLED ? chrome::GREEN : state == hd::UPDATE ? chrome::AMBER
                    : pack.recommended && state == hd::NOT_INSTALLED && !queued                    ? chrome::GOLD
                                                                                                   : chrome::DIM;
                if (note != nullptr) {
                    ImGui::SameLine();
                    ImGui::TextColored(colour, "%s", note);
                }
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(chrome::DIM, "%s", pack.description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (!hd_.error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(chrome::RED, "%s", hd_.error.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    void c_launcher::Draw_Hd_Window_() {
        if (!hd_.window) return;
        const char* title = "HD art";
        ImGui::OpenPopup(title);
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 36.0f, ImGui::GetFontSize() * 22.0f), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("Free packs of upscaled art for the whole game. They download from GitHub and switch on in this "
                               "profile by themselves when they're ready. They never affect your saves.");
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        Draw_Hd_List_();
        ImGui::Separator();
        ImGui::BeginDisabled(!Hd_Any_Ticked_());
        if (ImGui::Button("Download")) Hd_Start_();
        ImGui::EndDisabled();
        if (hd_.busy) {
            ImGui::SameLine();
            if (ImGui::Button("Stop")) hd_.progress.cancel = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            hd_.window = false;
            ImGui::CloseCurrentPopup();
        }
        if (hd_.busy) {
            ImGui::SameLine();
            ImGui::TextDisabled("Downloads carry on when you close this.");
        }
        ImGui::EndPopup();
    }
} // namespace launcher
