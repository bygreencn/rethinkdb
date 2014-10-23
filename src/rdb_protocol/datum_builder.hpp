// Copyright 2014 RethinkDB, all rights reserved.
#ifndef RDB_PROTOCOL_DATUM_BUILDER_HPP_
#define RDB_PROTOCOL_DATUM_BUILDER_HPP_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "rdb_protocol/datum.hpp"
#include "rdb_protocol/datum_string.hpp"
#include "rdb_protocol/configured_limits.hpp"
#include "version.hpp"

namespace ql {

// Useful for building an object datum and doing mutation operations -- otherwise,
// you'll have to do check_str_validity checks yourself.
class datum_object_builder_t {
public:
    datum_object_builder_t() { }
    explicit datum_object_builder_t(const datum_t &copy_from);

    // Returns true if the insertion did _not_ happen because the key was already in
    // the object.
    MUST_USE bool add(const datum_string_t &key, datum_t val);
    MUST_USE bool add(const char *key, datum_t val);
    // Inserts a new key or overwrites the existing key's value.
    void overwrite(const datum_string_t &key, datum_t val);
    void overwrite(const char *key, datum_t val);
    void add_warning(const char *msg, const configured_limits_t &limits);
    void add_warnings(const std::set<std::string> &msgs, const configured_limits_t &limits);
    void add_error(const char *msg);

    MUST_USE bool delete_field(const datum_string_t &key);
    MUST_USE bool delete_field(const char *key);

    datum_t at(const datum_string_t &key) const;

    // Returns null if the key doesn't exist.
    datum_t try_get(const datum_string_t &key) const;

    MUST_USE datum_t to_datum() RVALUE_THIS;

    MUST_USE datum_t to_datum(
            const std::set<std::string> &permissible_ptypes) RVALUE_THIS;

private:
    std::map<datum_string_t, datum_t> map;
    DISABLE_COPYING(datum_object_builder_t);
};

// Useful for building an array datum and doing mutation operations -- while having
// array-size checks on the fly.
class datum_array_builder_t {
public:
    explicit datum_array_builder_t(const configured_limits_t &_limits) : limits(_limits) {}
    explicit datum_array_builder_t(const datum_t &copy_from, const configured_limits_t &);

    size_t size() const { return vector.size(); }

    void reserve(size_t n);

    // Note that these methods produce behavior that is actually specific to the
    // definition of certain ReQL terms.
    void add(datum_t val);
    void change(size_t i, datum_t val);

    // On v1_13, insert and splice don't enforce the array size limit.
    void insert(reql_version_t reql_version, size_t index,
                datum_t val);
    void splice(reql_version_t reql_version, size_t index,
                datum_t values);

    // On v1_13, erase_range doesn't allow start and end to equal array_size.
    void erase_range(reql_version_t reql_version, size_t start, size_t end);

    void erase(size_t index);

    datum_t to_datum() RVALUE_THIS;

private:
    std::vector<datum_t> vector;
    configured_limits_t limits;

    DISABLE_COPYING(datum_array_builder_t);
};

} // namespace ql

#endif // RDB_PROTOCOL_DATUM_BUILDER_HPP_
