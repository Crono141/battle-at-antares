#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct yyjson_doc;
struct yyjson_val;

// Human text for settings (WP5.2): titles, descriptions and categories come from the config schema
// (specs/config/main.json5), which the generated runtime tables don't carry. The launcher embeds
// the schema and reads it with this.
namespace antares::settings {
    struct s_category {
        std::string id;
        std::string title;
        std::string description;
        std::vector<std::string> namespaces;
    };

    struct s_setting_doc {
        std::string title;       // the parameter's title, then each nested field: "A › B › C"
        std::string description; // the most specific description found
        std::string remarks;     // the parameter's remarks
        bool found = false;      // the parameter is in the schema
    };

    class c_schema_docs {
      public:
        c_schema_docs() = default;
        c_schema_docs(const c_schema_docs&) = delete;
        c_schema_docs& operator=(const c_schema_docs&) = delete;
        ~c_schema_docs();

        /** Reads the schema text (JSON5). */
        bool Load(const std::string& text, std::string* error);
        const std::vector<s_category>& Categories() const {
            return categories_;
        }
        /** Docs for a setting by its dotted variable name ("interface.animation.cloak_steps.fast"). */
        s_setting_doc Describe(const std::string& variable_name) const;
        /** The category id for a namespace, or "" when none lists it. */
        std::string Category_Of(const std::string& name_space) const;

      private:
        yyjson_val* Named_Type_(const std::string& name) const;

        yyjson_doc* document_ = nullptr;
        std::vector<s_category> categories_;
        std::unordered_map<std::string, yyjson_val*> parameters_;
        std::unordered_map<std::string, yyjson_val*> types_; // records and types
        std::unordered_map<std::string, std::string> category_by_namespace_;
    };
} // namespace antares::settings
