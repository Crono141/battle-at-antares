#include "pch.h"

#include "antares/settings_docs.h"

#include "yyjson.h"

namespace antares::settings {
    namespace {
        std::string String_(yyjson_val* object, const char* key) {
            yyjson_val* value = yyjson_obj_get(object, key);
            return yyjson_is_str(value) ? std::string(yyjson_get_str(value), yyjson_get_len(value)) : std::string();
        }

        std::vector<std::string> Split_(const std::string& name) {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= name.size()) {
                const size_t dot = name.find('.', start);
                parts.push_back(name.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
                if (dot == std::string::npos) break;
                start = dot + 1;
            }
            return parts;
        }

        // "adv_biology" -> "adv biology"; numbers stay as they are.
        std::string Readable_(std::string segment) {
            for (char& character : segment) {
                if (character == '_') character = ' ';
            }
            return segment;
        }

        void Index_Object_(yyjson_val* object, std::unordered_map<std::string, yyjson_val*>* index) {
            yyjson_obj_iter iterator = yyjson_obj_iter_with(object);
            yyjson_val* key = nullptr;
            while ((key = yyjson_obj_iter_next(&iterator)) != nullptr) {
                (*index)[std::string(yyjson_get_str(key), yyjson_get_len(key))] = yyjson_obj_iter_get_val(key);
            }
        }
    } // namespace

    c_schema_docs::~c_schema_docs() {
        if (document_ != nullptr) yyjson_doc_free(document_);
    }

    bool c_schema_docs::Load(const std::string& text, std::string* error) {
        const yyjson_read_flag flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS |
            YYJSON_READ_ALLOW_EXT_NUMBER | YYJSON_READ_ALLOW_SINGLE_QUOTED_STR | YYJSON_READ_ALLOW_UNQUOTED_KEY;
        yyjson_read_err read_error{};
        document_ = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), flags, nullptr, &read_error);
        if (document_ == nullptr) {
            if (error != nullptr) *error = std::string("config schema: ") + read_error.msg;
            return false;
        }
        yyjson_val* root = yyjson_doc_get_root(document_);
        Index_Object_(yyjson_obj_get(root, "parameters"), &parameters_);
        Index_Object_(yyjson_obj_get(root, "types"), &types_);
        Index_Object_(yyjson_obj_get(root, "records"), &types_);

        yyjson_obj_iter iterator = yyjson_obj_iter_with(yyjson_obj_get(root, "documentation_categories"));
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iterator)) != nullptr) {
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            s_category category;
            category.id = yyjson_get_str(key);
            category.title = String_(value, "title");
            category.description = String_(value, "description");
            size_t index = 0;
            size_t count = 0;
            yyjson_val* name_space = nullptr;
            yyjson_arr_foreach(yyjson_obj_get(value, "namespaces"), index, count, name_space) {
                if (yyjson_is_str(name_space)) {
                    category.namespaces.push_back(yyjson_get_str(name_space));
                    category_by_namespace_.emplace(category.namespaces.back(), category.id);
                }
            }
            categories_.push_back(std::move(category));
        }
        return true;
    }

    std::string c_schema_docs::Category_Of(const std::string& name_space) const {
        const auto found = category_by_namespace_.find(name_space);
        return found == category_by_namespace_.end() ? std::string() : found->second;
    }

    yyjson_val* c_schema_docs::Named_Type_(const std::string& name) const {
        const auto found = types_.find(name);
        return found == types_.end() ? nullptr : found->second;
    }

    s_setting_doc c_schema_docs::Describe(const std::string& variable_name) const {
        s_setting_doc doc;
        const std::vector<std::string> parts = Split_(variable_name);
        if (parts.size() < 2) {
            doc.title = variable_name;
            return doc;
        }
        const auto parameter = parameters_.find(parts[1]);
        if (parameter == parameters_.end() || String_(parameter->second, "namespace") != parts[0]) {
            doc.title = Readable_(parts[1]);
            for (size_t index = 2; index < parts.size(); ++index) doc.title += " › " + Readable_(parts[index]);
            return doc;
        }
        doc.found = true;
        doc.title = String_(parameter->second, "title");
        doc.description = String_(parameter->second, "description");
        doc.remarks = String_(parameter->second, "remarks");

        // Walk the remaining parts through records (fields) and arrays and maps (one index each).
        yyjson_val* type = yyjson_obj_get(parameter->second, "type");
        for (size_t index = 2; index < parts.size(); ++index) {
            for (int depth = 0; depth < 8 && yyjson_is_str(type); ++depth) {
                type = Named_Type_(yyjson_get_str(type)); // named type: its definition, or null for a primitive
            }
            yyjson_val* fields = yyjson_is_obj(type) ? yyjson_obj_get(type, "fields") : nullptr;
            const std::string kind = yyjson_is_obj(type) ? String_(type, "kind") : "";
            if (yyjson_is_arr(fields)) {
                yyjson_val* match = nullptr;
                size_t field_index = 0;
                size_t field_count = 0;
                yyjson_val* field = nullptr;
                yyjson_arr_foreach(fields, field_index, field_count, field) {
                    if (String_(field, "id") == parts[index]) match = field;
                }
                if (match != nullptr) {
                    const std::string title = String_(match, "title");
                    doc.title += " › " + (title.empty() ? Readable_(parts[index]) : title);
                    const std::string description = String_(match, "description");
                    if (!description.empty()) doc.description = description;
                    type = yyjson_obj_get(match, "type");
                    continue;
                }
            } else if (kind == "array" || kind == "map") {
                doc.title += " › " + Readable_(parts[index]);
                yyjson_val* element = yyjson_obj_get(type, "element");
                type = element != nullptr ? yyjson_obj_get(element, "type") : nullptr;
                continue;
            }
            doc.title += " › " + Readable_(parts[index]);
            type = nullptr;
        }
        return doc;
    }
} // namespace antares::settings
