#include "pch.h"

#include "antares/updates.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "antares/utf8_files.h"
#include "yyjson.h"

namespace antares::updates {
    namespace {
        std::string Str_(yyjson_val* object, const char* key) {
            yyjson_val* value = yyjson_obj_get(object, key);
            return yyjson_is_str(value) ? yyjson_get_str(value) : "";
        }

        bool Ends_With_(const std::string& text, const std::string& end) {
            return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
        }

        // The notes' first paragraph as plain text: up to the first blank line, markdown headings,
        // emphasis and links reduced to their words.
        std::string Intro_(const std::string& body) {
            std::string text;
            size_t at = 0;
            while (at < body.size()) {
                size_t end = body.find('\n', at);
                if (end == std::string::npos) end = body.size();
                std::string line = body.substr(at, end - at);
                at = end + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) {
                    if (!text.empty()) break;
                    continue;
                }
                if (line[0] == '#') continue; // the title
                text += (text.empty() ? "" : " ") + line;
            }
            std::string out;
            for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (c == '*' || c == '`') continue;
                if (c == '[') {
                    const size_t close = text.find("](", i);
                    const size_t paren = close == std::string::npos ? std::string::npos : text.find(')', close);
                    if (close != std::string::npos && paren != std::string::npos) {
                        out += text.substr(i + 1, close - i - 1);
                        i = paren;
                        continue;
                    }
                }
                out += c;
            }
            return out;
        }

        // A label's parts: "0.8.0-test12" -> {0, 8, 0} and {"test12"}.
        void Split_(const std::string& label, std::vector<long>* numbers, std::string* pre) {
            std::string version = label;
            if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
            const size_t dash = version.find('-');
            *pre = dash == std::string::npos ? "" : version.substr(dash + 1);
            const std::string core = version.substr(0, dash);
            size_t at = 0;
            while (at <= core.size()) {
                const size_t dot = core.find('.', at);
                numbers->push_back(std::strtol(core.substr(at, dot == std::string::npos ? std::string::npos : dot - at).c_str(), nullptr, 10));
                if (dot == std::string::npos) break;
                at = dot + 1;
            }
        }

        // Pre-release parts compared as text, but runs of digits as numbers ("test9" < "test10").
        int Compare_Pre_(const std::string& a, const std::string& b) {
            size_t i = 0, j = 0;
            while (i < a.size() && j < b.size()) {
                if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
                    size_t ie = i, je = j;
                    while (ie < a.size() && std::isdigit((unsigned char)a[ie])) ++ie;
                    while (je < b.size() && std::isdigit((unsigned char)b[je])) ++je;
                    const long x = std::strtol(a.substr(i, ie - i).c_str(), nullptr, 10);
                    const long y = std::strtol(b.substr(j, je - j).c_str(), nullptr, 10);
                    if (x != y) return x < y ? -1 : 1;
                    i = ie;
                    j = je;
                    continue;
                }
                if (a[i] != b[j]) return (unsigned char)a[i] < (unsigned char)b[j] ? -1 : 1;
                ++i;
                ++j;
            }
            if (i < a.size()) return 1;
            if (j < b.size()) return -1;
            return 0;
        }
    } // namespace

    bool Parse_Release(const std::string& json, s_release* release, std::string* error) {
        yyjson_doc* document = yyjson_read(json.data(), json.size(), 0);
        if (document == nullptr) {
            *error = "the release information isn't JSON";
            return false;
        }
        yyjson_val* root = yyjson_doc_get_root(document);
        *release = {};
        release->version = Str_(root, "tag_name");
        if (!release->version.empty() && (release->version[0] == 'v' || release->version[0] == 'V')) release->version.erase(0, 1);
        release->page_url = Str_(root, "html_url");
        release->intro = Intro_(Str_(root, "body"));
        yyjson_val* assets = yyjson_obj_get(root, "assets");
        size_t index, count;
        yyjson_val* asset;
        yyjson_arr_foreach(assets, index, count, asset) {
            s_asset item;
            item.name = Str_(asset, "name");
            item.url = Str_(asset, "browser_download_url");
            item.size = yyjson_get_uint(yyjson_obj_get(asset, "size"));
            const std::string digest = Str_(asset, "digest");
            if (digest.rfind("sha256:", 0) == 0) item.sha256 = digest.substr(7);
            if (!item.name.empty() && !item.url.empty()) release->assets.push_back(item);
        }
        yyjson_doc_free(document);
        if (release->version.empty()) {
            *error = "the release has no version";
            return false;
        }
        return true;
    }

    int Compare_Versions(const std::string& a, const std::string& b) {
        std::vector<long> x, y;
        std::string xp, yp;
        Split_(a, &x, &xp);
        Split_(b, &y, &yp);
        for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
            const long p = i < x.size() ? x[i] : 0;
            const long q = i < y.size() ? y[i] : 0;
            if (p != q) return p < q ? -1 : 1;
        }
        if (xp.empty() != yp.empty()) return xp.empty() ? 1 : -1; // a release after its pre-releases
        return Compare_Pre_(xp, yp);
    }

    e_kind Package_Kind(const std::string& program_dir) {
#if defined(_WIN32)
        std::error_code error;
        return std::filesystem::exists(utf8::Path(program_dir + "unins000.exe"), error) ? KIND_WINDOWS_INSTALLER : KIND_WINDOWS_ZIP;
#elif defined(__APPLE__)
        return program_dir.find(".app/Contents/Resources") != std::string::npos ? KIND_MAC_APP : KIND_MAC_ZIP;
#else
        (void)program_dir;
        if (std::getenv("FLATPAK_ID") != nullptr) return KIND_FLATPAK;
        if (std::getenv("APPIMAGE") != nullptr) return KIND_APPIMAGE;
        return KIND_LINUX_ARCHIVE;
#endif
    }

    bool Can_Install(e_kind kind) {
        return kind == KIND_WINDOWS_INSTALLER || kind == KIND_WINDOWS_ZIP || kind == KIND_LINUX_ARCHIVE || kind == KIND_APPIMAGE;
    }

    const s_asset* Asset_For(const s_release& release, e_kind kind) {
        const char* ending = nullptr;
        switch (kind) {
            case KIND_WINDOWS_INSTALLER: ending = "-windows-x86_64-setup.exe"; break;
            case KIND_WINDOWS_ZIP: ending = "-windows-x86_64.zip"; break;
            case KIND_LINUX_ARCHIVE: ending = "-linux-x86_64.tar.gz"; break;
            case KIND_APPIMAGE: ending = "-x86_64.AppImage"; break;
            case KIND_FLATPAK: ending = "-x86_64.flatpak"; break;
            case KIND_MAC_APP: ending = "-macos-arm64.dmg"; break;
            case KIND_MAC_ZIP: ending = "-macos-arm64.zip"; break;
            case KIND_NONE: return nullptr;
        }
        for (const s_asset& asset : release.assets) {
            if (Ends_With_(asset.name, ending)) return &asset;
        }
        return nullptr;
    }

    std::string Installed_Version(const std::string& program_dir) {
        std::ifstream file(utf8::Path(program_dir + VERSION_FILE), std::ios::binary);
        std::string line;
        if (!file || !std::getline(file, line)) return "";
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\n')) line.pop_back();
        return line;
    }

    bool Players_File(const std::string& relative) {
        return relative == "data/antares.cfg";
    }
} // namespace antares::updates
