#include "json.hpp"

namespace banjo {

JSONValue::JSONValue(JSONObject value) : value(value) {}

JSONValue::JSONValue(JSONArray value) : value(value) {}

JSONObject::JSONObject() {}

JSONObject::JSONObject(std::initializer_list<std::pair<std::string, JSONValue>> values) {
    for (const auto &value : values) {
        add(value.first, value.second);
    }
}

const JSONValue &JSONObject::get(std::string key) const {
    return values.at(key);
}

const JSONString &JSONObject::get_string(std::string key) const {
    return get(key).as_string();
}

JSONInt JSONObject::get_int(std::string key) const {
    return get(key).as_int();
}

JSONFloat JSONObject::get_float(std::string key) const {
    return get(key).as_float();
}

JSONBool JSONObject::get_bool(std::string key) const {
    return get(key).as_bool();
}

const JSONObject &JSONObject::get_object(std::string key) const {
    return get(key).as_object();
}

const JSONArray &JSONObject::get_array(std::string key) const {
    return get(key).as_array();
}

bool JSONObject::contains(std::string key) const {
    return values.count(key);
}

void JSONObject::add(std::string key, JSONValue value) {
    values.insert({key, value});
}

JSONArray::JSONArray() {}

JSONArray::JSONArray(std::initializer_list<JSONValue> values) {
    for (const auto &value : values) {
        add(value);
    }
}

const JSONValue &JSONArray::get(int index) const {
    return values.at(index);
}

const JSONString &JSONArray::get_string(int index) const {
    return get(index).as_string();
}

JSONInt JSONArray::get_int(int index) const {
    return get(index).as_int();
}

JSONFloat JSONArray::get_float(int index) const {
    return get(index).as_float();
}

JSONBool JSONArray::get_bool(int index) const {
    return get(index).as_bool();
}

const JSONObject &JSONArray::get_object(int index) const {
    return get(index).as_object();
}

const JSONArray &JSONArray::get_array(int index) const {
    return get(index).as_array();
}

int JSONArray::length() const {
    return values.size();
}

void JSONArray::add(JSONValue value) {
    values.push_back(value);
}

} // namespace banjo
