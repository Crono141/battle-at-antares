#include "pch.h"

#include "antares/gog.h"

#include <cctype>
#include <cstring>
#include <vector>

#include "yyjson.h"

namespace antares::gog {
    namespace {
        std::string Encode_(const std::string& text) {
            static const char* digits = "0123456789ABCDEF";
            std::string encoded;
            for (unsigned char character : text) {
                if (std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
                    encoded += static_cast<char>(character);
                } else {
                    encoded += '%';
                    encoded += digits[character >> 4];
                    encoded += digits[character & 15];
                }
            }
            return encoded;
        }

        std::string Lower_(std::string text) {
            for (char& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return text;
        }

        // "Master of Orion 2", "Master of Orion II", "Master of Orion 1+2".
        bool Names_Orion_2_(const std::string& title) {
            const std::string lower = Lower_(title);
            const size_t at = lower.find("orion");
            if (at == std::string::npos) return false;
            const std::string rest = lower.substr(at + 5);
            return rest.find('2') != std::string::npos || rest.find(" ii") != std::string::npos;
        }

        uint64_t Size_(const char* text) {
            if (text == nullptr) return 0;
            const double value = std::strtod(text, nullptr);
            const std::string lower = Lower_(text);
            const double unit = lower.find("gb") != std::string::npos ? 1073741824.0 : lower.find("mb") != std::string::npos ? 1048576.0
                : lower.find("kb") != std::string::npos                                                                      ? 1024.0
                                                                                                                             : 1.0;
            return value > 0 ? static_cast<uint64_t>(value * unit) : 0;
        }

        std::string Decode_(const std::string& text) {
            std::string decoded;
            for (size_t index = 0; index < text.size(); ++index) {
                if (text[index] == '%' && index + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[index + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(text[index + 2]))) {
                    decoded += static_cast<char>(std::stoi(text.substr(index + 1, 2), nullptr, 16));
                    index += 2;
                } else {
                    decoded += text[index];
                }
            }
            return decoded;
        }
    } // namespace

    std::string Token_Url(const std::string& code) {
        return std::string("https://auth.gog.com/token?client_id=") + CLIENT_ID + "&client_secret=" + CLIENT_SECRET +
            "&grant_type=authorization_code&code=" + Encode_(code) + "&redirect_uri=" + Encode_(REDIRECT_URI);
    }

    bool Parse_Token(const std::string& json, std::string* token, std::string* error) {
        yyjson_doc* document = yyjson_read(json.data(), json.size(), 0);
        yyjson_val* value = document != nullptr ? yyjson_obj_get(yyjson_doc_get_root(document), "access_token") : nullptr;
        const bool ok = yyjson_is_str(value) && yyjson_get_len(value) > 0;
        if (ok) *token = yyjson_get_str(value);
        yyjson_doc_free(document);
        if (!ok) *error = "GOG didn't accept the sign-in";
        return ok;
    }

    bool Choose_Product(const std::string& json, s_product* product, std::string* error) {
        yyjson_doc* document = yyjson_read(json.data(), json.size(), 0);
        yyjson_val* products = document != nullptr ? yyjson_obj_get(yyjson_doc_get_root(document), "products") : nullptr;
        bool found = false;
        size_t index, max;
        yyjson_val* item;
        // The bundle first (it is how GOG sells it), then a single Master of Orion 2.
        for (int pass = 0; pass < 2 && !found && yyjson_is_arr(products); ++pass) {
            yyjson_arr_foreach(products, index, max, item) {
                yyjson_val* title = yyjson_obj_get(item, "title");
                if (!yyjson_is_str(title) || !Names_Orion_2_(yyjson_get_str(title))) continue;
                const bool bundle = std::strstr(yyjson_get_str(title), "1+2") != nullptr;
                if ((pass == 0) != bundle) continue;
                product->id = yyjson_get_uint(yyjson_obj_get(item, "id"));
                product->title = yyjson_get_str(title);
                found = product->id != 0;
                if (found) break;
            }
        }
        if (!yyjson_is_arr(products))
            *error = "GOG's library answer couldn't be read";
        else if (!found)
            *error = "Master of Orion 2 isn't in your GOG library";
        yyjson_doc_free(document);
        return found;
    }

    std::string Details_Url(uint64_t product_id) {
        return "https://embed.gog.com/account/gameDetails/" + std::to_string(product_id) + ".json";
    }

    bool Choose_Download(const std::string& json, e_system system, s_download* download, std::string* error) {
        yyjson_doc* document = yyjson_read(json.data(), json.size(), 0);
        yyjson_val* downloads = document != nullptr ? yyjson_obj_get(yyjson_doc_get_root(document), "downloads") : nullptr;
        if (!yyjson_is_arr(downloads) || yyjson_arr_size(downloads) == 0) {
            *error = "GOG lists no downloads for it in your library";
            yyjson_doc_free(document);
            return false;
        }
        // [["English", {"windows": [...], "mac": [...], "linux": [...]}], ...]
        yyjson_val* chosen = nullptr;
        size_t index, max;
        yyjson_val* language;
        yyjson_arr_foreach(downloads, index, max, language) {
            yyjson_val* name = yyjson_arr_get(language, 0);
            if (chosen == nullptr || (yyjson_is_str(name) && Lower_(yyjson_get_str(name)) == "english")) chosen = yyjson_arr_get(language, 1);
        }
        const char* keys[] = {"windows", "mac", "linux"};
        bool found = false;
        for (int attempt = 0; attempt < 2 && !found; ++attempt) {
            const e_system wanted = attempt == 0 ? system : SYSTEM_WINDOWS;
            yyjson_val* list = yyjson_obj_get(chosen, keys[wanted]);
            if (!yyjson_is_arr(list) || yyjson_arr_size(list) == 0) continue;
            // The bundle lists both games' installers: take Master of Orion 2's, else the only one.
            yyjson_val* pick = nullptr;
            size_t item_index, item_max;
            yyjson_val* item;
            yyjson_arr_foreach(list, item_index, item_max, item) {
                yyjson_val* name = yyjson_obj_get(item, "name");
                if (yyjson_is_str(name) && Names_Orion_2_(yyjson_get_str(name)) && std::strstr(yyjson_get_str(name), "1+2") == nullptr) {
                    pick = item;
                    break;
                }
            }
            if (pick == nullptr && yyjson_arr_size(list) == 1) pick = yyjson_arr_get(list, 0);
            yyjson_val* path = yyjson_obj_get(pick, "manualUrl");
            if (!yyjson_is_str(path)) continue;
            download->path = yyjson_get_str(path);
            yyjson_val* name = yyjson_obj_get(pick, "name");
            download->name = yyjson_is_str(name) ? yyjson_get_str(name) : "";
            download->size = Size_(yyjson_get_str(yyjson_obj_get(pick, "size")));
            download->system = wanted;
            found = true;
        }
        if (!found) *error = "GOG lists no installer for Master of Orion 2 that Battle at Antares recognises";
        yyjson_doc_free(document);
        return found;
    }

    std::string Installer_File_Name(const std::string& final_url, e_system system) {
        std::string path = final_url.substr(0, final_url.find_first_of("?#"));
        std::string name = Decode_(path.substr(path.rfind('/') + 1));
        // Nothing that could step out of Downloads.
        for (char& character : name) {
            if (character == '/' || character == '\\' || character == ':' || static_cast<unsigned char>(character) < 32) character = '_';
        }
        const char* extension = system == SYSTEM_WINDOWS ? ".exe" : system == SYSTEM_MAC ? ".pkg"
                                                                                         : ".sh";
        const std::string lower = Lower_(name);
        const bool known = lower.find("master_of_orion_2") != std::string::npos && lower.size() > 4 &&
            (lower.compare(lower.size() - 4, 4, ".exe") == 0 || lower.compare(lower.size() - 4, 4, ".pkg") == 0 || lower.compare(lower.size() - 3, 3, ".sh") == 0);
        if (known && name[0] != '.') return name;
        return std::string(system == SYSTEM_WINDOWS ? "setup_master_of_orion_2_gog" : "gog_master_of_orion_2") + extension;
    }
} // namespace antares::gog
