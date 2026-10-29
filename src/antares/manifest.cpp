#include "antares/manifest.h"

#include "antares/identity.h"
#include "config/mod_manifest.h"

namespace antares {
    bool Check_Requires_Antares(yyjson_val* value, std::string* range, std::string* error) {
        range->clear();
        if (value == nullptr) {
            return true;
        }
        if (!yyjson_is_str(value) || yyjson_get_len(value) == 0) {
            *error = "manifest property 'requires.antares' must be a non-empty string";
            return false;
        }
        range->assign(yyjson_get_str(value), yyjson_get_len(value));
        const config::s_semantic_version product_version = {
            PRODUCT_VERSION_MAJOR,
            PRODUCT_VERSION_MINOR,
            PRODUCT_VERSION_PATCH,
        };
        if (!config::Validate_Semantic_Version_Range(*range)) {
            *error = "invalid Battle at Antares version range '" + *range + "'";
            return false;
        }
        if (!config::Semantic_Version_Matches(product_version, *range)) {
            *error = "package requires Battle at Antares '" + *range + "' but this is " + PRODUCT_VERSION;
            return false;
        }
        return true;
    }
} // namespace antares
