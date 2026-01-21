#include <jansson.h>

static bool json_get_number(json_t *obj, const char *key, double &out)
{
    json_t *v = json_object_get(obj, key);
    if (!v || !json_is_number(v))
        return false;
    out = json_number_value(v);
    return true;
}
static bool json_get_bool(json_t *obj, const char *key, bool &out)
{
    json_t *v = json_object_get(obj, key);
    if (!v || !json_is_boolean(v))
        return false;
    out = json_is_true(v);
    return true;
}
