// Copyright 2014 RethinkDB, all rights reserved.
#include "rdb_protocol/datum_builder.hpp"

namespace ql {

const datum_string_t errors_field("errors");
const datum_string_t first_error_field("first_error");
const datum_string_t warnings_field("warnings");

datum_object_builder_t::datum_object_builder_t(const datum_t &copy_from) {
    const size_t copy_from_sz = copy_from.obj_size();
    for (size_t i = 0; i < copy_from_sz; ++i) {
        map.insert(copy_from.get_pair(i));
    }
}

bool datum_object_builder_t::add(const datum_string_t &key, datum_t val) {
    datum_t::check_str_validity(key);
    r_sanity_check(val.has());
    auto res = map.insert(std::make_pair(key, std::move(val)));
    // Return _false_ if the insertion actually happened.  Because we are being
    // backwards to the C++ convention.
    return !res.second;
}

bool datum_object_builder_t::add(const char *key, datum_t val) {
    return add(datum_string_t(key), val);
}

void datum_object_builder_t::overwrite(const datum_string_t &key,
                                       datum_t val) {
    datum_t::check_str_validity(key);
    r_sanity_check(val.has());
    map[key] = std::move(val);
}

void datum_object_builder_t::overwrite(const char *key,
                                       datum_t val) {
    return overwrite(datum_string_t(key), val);
}

void datum_object_builder_t::add_warning(const char *msg, const configured_limits_t &limits) {
    datum_t *warnings_entry = &map[warnings_field];
    if (warnings_entry->has()) {
        // assume here that the warnings array will "always" be small.
        const size_t warnings_entry_sz = warnings_entry->arr_size();
        for (size_t i = 0; i < warnings_entry_sz; ++i) {
            if (warnings_entry->get(i).as_str() == msg) return;
        }
        rcheck_datum(warnings_entry_sz + 1 <= limits.array_size_limit(),
            base_exc_t::GENERIC,
            strprintf("Warnings would exceed array size limit %zu; increase it to see warnings", limits.array_size_limit()));
        datum_array_builder_t out(*warnings_entry, limits);
        out.add(datum_t(msg));
        *warnings_entry = std::move(out).to_datum();
    } else {
        datum_array_builder_t out(limits);
        out.add(datum_t(msg));
        *warnings_entry = std::move(out).to_datum();
    }
}

void datum_object_builder_t::add_warnings(const std::set<std::string> &msgs, const configured_limits_t &limits) {
    if (msgs.empty()) return;
    datum_t *warnings_entry = &map[warnings_field];
    if (warnings_entry->has()) {
        rcheck_datum(warnings_entry->arr_size() + msgs.size() <= limits.array_size_limit(),
            base_exc_t::GENERIC,
            strprintf("Warnings would exceed array size limit %zu; increase it to see warnings", limits.array_size_limit()));
        datum_array_builder_t out(*warnings_entry, limits);
        for (auto const & msg : msgs) {
            bool seen = false;
            // assume here that the warnings array will "always" be small.
            const size_t warnings_entry_sz = warnings_entry->arr_size();
            for (size_t i = 0; i < warnings_entry_sz; ++i) {
                if (warnings_entry->get(i).as_str() == msg.c_str()) {
                    seen = true;
                    break;
                }
            }
            if (!seen) out.add(datum_t(msg.c_str()));
        }
        *warnings_entry = std::move(out).to_datum();
    } else {
        datum_array_builder_t out(limits);
        for (auto const & msg : msgs) {
            out.add(datum_t(msg.c_str()));
        }
        *warnings_entry = std::move(out).to_datum();
    }
}

void datum_object_builder_t::add_error(const char *msg) {
    // Insert or update the "errors" entry.
    {
        datum_t *errors_entry = &map[errors_field];
        double ecount = (errors_entry->has() ? (*errors_entry).as_num() : 0) + 1;
        *errors_entry = datum_t(ecount);
    }

    // If first_error already exists, nothing gets inserted.
    map.insert(std::make_pair(first_error_field, datum_t(msg)));
}

MUST_USE bool datum_object_builder_t::delete_field(const datum_string_t &key) {
    return 0 != map.erase(key);
}

MUST_USE bool datum_object_builder_t::delete_field(const char *key) {
    return delete_field(datum_string_t(key));
}


datum_t datum_object_builder_t::at(const datum_string_t &key) const {
    return map.at(key);
}

datum_t datum_object_builder_t::try_get(const datum_string_t &key) const {
    auto it = map.find(key);
    return it == map.end() ? datum_t() : it->second;
}

datum_t datum_object_builder_t::to_datum() RVALUE_THIS {
    return datum_t(std::move(map));
}

datum_t datum_object_builder_t::to_datum(
        const std::set<std::string> &permissible_ptypes) RVALUE_THIS {
    return datum_t(std::move(map), permissible_ptypes);
}

datum_array_builder_t::datum_array_builder_t(const datum_t &copy_from,
                                             const configured_limits_t &_limits)
    : limits(_limits) {
    const size_t copy_from_sz = copy_from.arr_size();
    vector.reserve(copy_from_sz);
    for (size_t i = 0; i < copy_from_sz; ++i) {
        vector.push_back(copy_from.get(i));
    }
    rcheck_array_size_datum(vector, limits, base_exc_t::GENERIC);
}

void datum_array_builder_t::reserve(size_t n) { vector.reserve(n); }

void datum_array_builder_t::add(datum_t val) {
    vector.push_back(std::move(val));
    rcheck_array_size_datum(vector, limits, base_exc_t::GENERIC);
}

void datum_array_builder_t::change(size_t index, datum_t val) {
    rcheck_datum(index < vector.size(),
                 base_exc_t::NON_EXISTENCE,
                 strprintf("Index `%zu` out of bounds for array of size: `%zu`.",
                           index, vector.size()));
    vector[index] = std::move(val);
}

void datum_array_builder_t::insert(reql_version_t reql_version, size_t index,
                                   datum_t val) {
    rcheck_datum(index <= vector.size(),
                 base_exc_t::NON_EXISTENCE,
                 strprintf("Index `%zu` out of bounds for array of size: `%zu`.",
                           index, vector.size()));
    vector.insert(vector.begin() + index, std::move(val));

    switch (reql_version) {
    case reql_version_t::v1_13:
        break;
    case reql_version_t::v1_14: // v1_15 is the same as v1_14
    case reql_version_t::v1_16_is_latest:
        rcheck_array_size_datum(vector, limits, base_exc_t::GENERIC);
        break;
    default:
        unreachable();
    }
}

void datum_array_builder_t::splice(reql_version_t reql_version, size_t index,
                                   datum_t values) {
    rcheck_datum(index <= vector.size(),
                 base_exc_t::NON_EXISTENCE,
                 strprintf("Index `%zu` out of bounds for array of size: `%zu`.",
                           index, vector.size()));

    // First copy the values into a vector so vector.insert() can know the number
    // of elements being inserted.
    std::vector<datum_t> arr;
    const size_t values_sz = values.arr_size();
    arr.reserve(values_sz);
    for (size_t i = 0; i < values_sz; ++i) {
        arr.push_back(values.get(i));
    }
    vector.insert(vector.begin() + index,
                  std::make_move_iterator(arr.begin()),
                  std::make_move_iterator(arr.end()));

    switch (reql_version) {
    case reql_version_t::v1_13:
        break;
    case reql_version_t::v1_14: // v1_15 is the same as v1_14
    case reql_version_t::v1_16_is_latest:
        rcheck_array_size_datum(vector, limits, base_exc_t::GENERIC);
        break;
    default:
        unreachable();
    }
}

void datum_array_builder_t::erase_range(reql_version_t reql_version,
                                        size_t start, size_t end) {

    // See https://github.com/rethinkdb/rethinkdb/issues/2696 about the backwards
    // compatible implementation for v1_13.

    switch (reql_version) {
    case reql_version_t::v1_13:
        rcheck_datum(start < vector.size(),
                     base_exc_t::NON_EXISTENCE,
                     strprintf("Index `%zu` out of bounds for array of size: `%zu`.",
                               start, vector.size()));
        break;
    case reql_version_t::v1_14: // v1_15 is the same as v1_14
    case reql_version_t::v1_16_is_latest:
        rcheck_datum(start <= vector.size(),
                     base_exc_t::NON_EXISTENCE,
                     strprintf("Index `%zu` out of bounds for array of size: `%zu`.",
                               start, vector.size()));
        break;
    default:
        unreachable();
    }


    rcheck_datum(end <= vector.size(),
                 base_exc_t::NON_EXISTENCE,
                 strprintf("Index `%zu` out of bounds for array of size: `%zu`.",
                           end, vector.size()));
    rcheck_datum(start <= end,
                 base_exc_t::GENERIC,
                 strprintf("Start index `%zu` is greater than end index `%zu`.",
                           start, end));
    vector.erase(vector.begin() + start, vector.begin() + end);
}

void datum_array_builder_t::erase(size_t index) {
    rcheck_datum(index < vector.size(),
                 base_exc_t::NON_EXISTENCE,
                 strprintf("Index `%zu` out of bounds for array of size: `%zu`.",
                           index, vector.size()));
    vector.erase(vector.begin() + index);
}

datum_t datum_array_builder_t::to_datum() RVALUE_THIS {
    // We call the non-checking constructor.  See
    // https://github.com/rethinkdb/rethinkdb/issues/2697 for more information --
    // insert and splice don't check the array size limit, because of a bug (as
    // reported in the issue).  This maintains that broken ReQL behavior because of
    // the generic reasons you would do so: secondary index compatibility after an
    // upgrade.
    return datum_t(std::move(vector), datum_t::no_array_size_limit_check_t());
}

} // namespace ql
