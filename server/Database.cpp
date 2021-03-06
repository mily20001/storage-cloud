#include "Database.h"

#include <iostream>

using namespace mongocxx;
using namespace std;

using bsoncxx::builder::basic::sub_array;

Database::Database(Logger* l) {
    inst = new mongocxx::instance{};
    client = new mongocxx::client{mongocxx::uri{DB_URI}};
    logger = l;
    db = (*client)[DB_NAME];
    connected = false;

    try {
        db.run_command(make_document(kvp("isMaster", 1)));
        l->info(l_id, "connected to database");
        connected = true;
    } catch (const std::exception& ex) {
        l->err(l_id, "error while connecting to database: " + string(ex.what()));
    } catch (...) {
        l->err(l_id, "error while connecting to database: unknown error");
    }
}

Database::~Database() {
    logger->info(l_id, "closing database connection");
    delete client;
    delete inst;
}

bool Database::getField(string& colName, string& fieldName, bsoncxx::oid id, bsoncxx::document::element& el) {
    mongocxx::options::find opts{};
    opts.projection(make_document(kvp(fieldName, 1), kvp("_id", 0)));

    try {
        auto cursor = db[colName].find(make_document(kvp("_id", id)), opts);

        auto doc_i = cursor.begin();

        if (doc_i == cursor.end() || doc_i->empty()) {
            logger->log(l_id, "getField got empty resultSet");
            return false;
        }

        auto val = doc_i->begin();

        if (bsoncxx::string::to_string(val->key()) == fieldName) {
            el = *val;
            return true;
        }

        logger->log(l_id, "getField got invalid field");
        return false;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting field: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting field: unknown error");
        return false;
    }
}

bool Database::getField(string&& colName, string&& fieldName, bsoncxx::oid id, bsoncxx::document::element& el) {
    return getField(colName, fieldName, id, el);
}

bool Database::getField(string&& colName, string&& fieldName, bsoncxx::oid id, string& res) {
    bsoncxx::document::element el;

    if(getField(colName, fieldName, id, el)) {
        if(el.type() == bsoncxx::type::k_utf8) {
            res = bsoncxx::string::to_string(el.get_utf8().value);
            return true;
        } else {
            logger->log(l_id, "getField got invalid field type (should be k_utf8)");
        }
    }

    return false;
}

bool Database::getField(string&& colName, string&& fieldName, bsoncxx::oid id, int64_t& res) {
    bsoncxx::document::element el;

    if(getField(colName, fieldName, id, el)) {
        if(el.type() == bsoncxx::type::k_int64) {
            res = el.get_int64().value;
            return true;
        } else {
            logger->log(l_id, "getField got invalid field type (should be k_int64)");
        }
    }

    return false;
}

bool Database::getField(string&& colName, string&& fieldName, bsoncxx::oid id, const uint8_t*& res, uint32_t& resSize) {
    bsoncxx::document::element el;

    if(getField(colName, fieldName, id, el)) {
        if(el.type() == bsoncxx::type::k_binary) {
            res = el.get_binary().bytes;
            resSize = el.get_binary().size;
            return true;
        } else {
            logger->log(l_id, "getField got invalid field type (should be k_binary)");
        }
    }

    return false;
}

bool Database::getField(string&& colName, string&& fieldToGetName, string&& idFieldName, bsoncxx::oid& id,
                        string&& fieldName, const string& fieldVal, int64_t& res) {

    mongocxx::options::find opts{};
    opts.projection(make_document(kvp("_id", 0), kvp(fieldToGetName, 1)));

    try {
        auto cursor = db[colName].find(make_document(kvp(idFieldName, id), kvp(fieldName, fieldVal)), opts);

        auto doc_i = cursor.begin();

        if (doc_i == cursor.end() || doc_i->empty()) {
            logger->log(l_id, "getField (2) got empty resultSet");
            return false;
        }

        auto val = doc_i->begin();

        if (bsoncxx::string::to_string(val->key()) == fieldToGetName && val->type() == bsoncxx::type::k_int64) {
            res = val->get_int64().value;
            return true;
        }

        logger->log(l_id, "getField (2) got invalid field");
        return false;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting field (2): " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting field (2): unknown error");
        return false;
    }
}

bool Database::getFieldM(string&& colName, string&& fieldName, bsoncxx::document::value&& fDoc, std::vector<string>& res) {
    auto doc = bsoncxx::builder::basic::document{};
    doc.append(kvp("_id", 0));
    doc.append(kvp(fieldName, 1));

    mongocxx::options::find opts{};
    opts.projection(doc.view());

    try {
        auto cursor = db[colName].find(fDoc.view(), opts);

        bool notEmpty = false;

        for (auto doc_v: cursor) {
            notEmpty = true;

            auto obj = doc_v.begin();

            if (distance(obj, doc_v.end()) != 1) {
                logger->log(l_id, "getFieldM got too much fields");
                return false;
            }

            res.emplace_back(bsoncxx::string::to_string(obj->get_utf8().value));
        }

        if(!notEmpty) {
            logger->log(l_id, "getFieldM got empty result");
        }

        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting field multiple times: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting field multiple times: unknown error");
        return false;
    }
}

bool Database::getFieldMAdvanced(string&& colName, string&& fieldName, mongocxx::pipeline& stages, std::vector<string>& res) {
    stages.project(make_document(kvp("_id", 0), kvp(fieldName, 1)));

    try {
        auto cursor = db[colName].aggregate(stages);

        bool notEmpty = false;

        for (auto doc_v: cursor) {
            notEmpty = true;

            auto obj = doc_v.begin();

            if (distance(obj, doc_v.end()) != 1) {
                logger->log(l_id, "getFieldMAdvanced got too much fields");
                return false;
            }

            res.emplace_back(bsoncxx::string::to_string(obj->get_utf8().value));
        }

        if(!notEmpty) {
            logger->log(l_id, "getFieldMAdvanced got empty result");
        }

        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting field multiple times advanced: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting field multiple times advanced: unknown error");
        return false;
    }
}

bool Database::getId(string&& colName, string&& fieldName, const string& fieldValue, bsoncxx::oid& id) {
    mongocxx::options::find opts{};
    opts.projection(make_document(kvp("_id", 1)));

    try {
        auto cursor = db[colName].find(make_document(kvp(fieldName, fieldValue)), opts);

        auto doc_i = cursor.begin();

        if (doc_i == cursor.end() || doc_i->empty()) {
            logger->log(l_id, "getId got empty resultSet");
            return false;
        }

        auto val = doc_i->begin();

        if (val->type() == bsoncxx::type::k_oid) {
            id = val->get_oid().value;
            return true;
        }

        logger->log(l_id, "getId got invalid field type (should be k_oid)");
        return false;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting id: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting id: unknown error");
        return false;
    }
}

bool Database::getIdById(string&& colName, string&& fieldName, const string& fieldValue, string&& idFieldName, bsoncxx::oid& id) {
    mongocxx::options::find opts{};
    opts.projection(make_document(kvp("_id", 1)));

    try {
        auto cursor = db[colName].find(make_document(kvp(idFieldName, id), kvp(fieldName, fieldValue)), opts);

        auto doc_i = cursor.begin();

        if (doc_i == cursor.end() || doc_i->empty()) {
            logger->log(l_id, "getIdById got empty resultSet");
            return false;
        }

        auto val = doc_i->begin();

        if (val->type() == bsoncxx::type::k_oid) {
            id = val->get_oid().value;
            return true;
        }

        logger->log(l_id, "getIdById got invalid field type (should be k_oid)");
        return false;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting id by id: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting id by id: unknown error");
        return false;
    }
}

bool Database::getIdByDoc(string&& colName, bsoncxx::document::value&& doc, bsoncxx::oid& res) {
    mongocxx::options::find opts{};
    opts.projection(make_document(kvp("_id", 1)));

    try {
        auto cursor = db[colName].find(doc.view(), opts);

        auto doc_i = cursor.begin();

        if (doc_i == cursor.end() || doc_i->empty()) {
            logger->log(l_id, "getIdByDoc got empty resultSet");
            return false;
        }

        auto val = doc_i->begin();

        if (val->type() == bsoncxx::type::k_oid) {
            res = val->get_oid().value;
            return true;
        }

        logger->log(l_id, "getIdByDoc got invalid field type (should be k_oid)");
        return false;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting id by doc: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting id by doc: unknown error");
        return false;
    }
}

// get fields values described in map
bool Database::getFields(string&& colName, bsoncxx::oid id, map<string, bsoncxx::types::value>& elements) {
    auto doc = bsoncxx::builder::basic::document{};
    doc.append(kvp("_id", 0));

    for(const auto &name: elements) {
        doc.append(kvp(name.first, 1));
    }

    mongocxx::options::find opts{};
    opts.projection(doc.view());

    try {
        auto cursor = db[colName].find(make_document(kvp("_id", id)), opts);

        auto doc_i = cursor.begin();

        if (doc_i == cursor.end() || doc_i->empty()) {
            logger->log(l_id, "getFields got empty resultSet");
            return false;
        }

        if (distance(doc_i->begin(), doc_i->end()) != elements.size()) {
            logger->log(l_id, "getFields got invalid element count");
            return false;
        }

        elements.clear();

        for(auto val: (*doc_i)) {
            string key = bsoncxx::string::to_string(val.key());
            if (key != "_id") {
                elements.emplace(key, val.get_value());
            }
        }
        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting fields (1): " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting fields (1): unknown error");
        return false;
    }
}

bool Database::getFields(string&& colName, vector<string>& fields, map<bsoncxx::oid, map<string, bsoncxx::types::value> >& elements) {
    auto doc = bsoncxx::builder::basic::document{};

    for(const auto &name: fields) {
        doc.append(kvp(name, 1));
    }

    mongocxx::options::find opts{};
    opts.projection(doc.view());

    try {
        auto cursor = db[colName].find(make_document(), opts);

        bool notEmpty = false;

        for (auto doc_v: cursor) {
            notEmpty = true;

            //TODO check if id was passed as field
            if ((distance(doc_v.begin(), doc_v.end()) != fields.size() + 1) && (distance(doc_v.begin(), doc_v.end()) != fields.size()) && (distance(doc_v.begin(), doc_v.end()) != fields.size() - 1)) {
                logger->log(l_id, std::to_string(distance(doc_v.begin(), doc_v.end())) + " != " + std::to_string(fields.size()));
                logger->log(l_id, "getFields got invalid fields count");
                return false;
            }

            bsoncxx::document::element t_id = doc_v["_id"];

            if (!t_id || t_id.type() != bsoncxx::type::k_oid) {
                logger->log(l_id, "getFields got invalid _id field");
                return false;
            }

            bsoncxx::oid id = t_id.get_oid().value;

            elements.emplace(id, map<string, bsoncxx::types::value>{});

            for (auto val: doc_v) {
                string key = bsoncxx::string::to_string(val.key());
                if (key != "_id") {
                    elements[id].emplace(key, val.get_value());
                }
            }
        }

        if(!notEmpty) {
            logger->log(l_id, "getFields got empty result");
        }

        return notEmpty;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting fields (2): " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting fields (2): unknown error");
        return false;
    }
}

bool Database::getFields(string&& colName, bsoncxx::document::value&& doc, const vector<string>& fields, map<string, map<string, bsoncxx::types::value> >& elements) {
    auto odoc = bsoncxx::builder::basic::document{};

    for(const auto &name: fields) {
        odoc.append(kvp(name, 1));
    }

    mongocxx::options::find opts{};
    opts.projection(odoc.view());

    try {
        auto cursor = db[colName].find(doc.view(), opts);

        bool notEmpty = false;

        for (auto doc_v: cursor) {
            notEmpty = true;

            //TODO check if id was passed as field
            if (distance(doc_v.begin(), doc_v.end()) != fields.size() + 1) {
                logger->log(l_id, "getFields got invalid fields count");
                return false;
            }

            bsoncxx::document::element t_id = doc_v["filename"];

            if (!t_id || t_id.type() != bsoncxx::type::k_utf8) {
                logger->log(l_id, "getFields got invalid filename field");
                return false;
            }

            string id = bsoncxx::string::to_string(t_id.get_utf8().value);

            elements.emplace(id, map<string, bsoncxx::types::value>{});

            for (auto val: doc_v) {
                string key = bsoncxx::string::to_string(val.key());
                elements[id].emplace(key, val.get_value());
            }
        }

        if(!notEmpty) {
            logger->log(l_id, "getFields got empty result");
        }

        return notEmpty;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting fields (3): " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting fields (3): unknown error");
        return false;
    }
}

bool Database::getFieldsAdvanced(string&& colName, mongocxx::pipeline& stages, vector<string>& fields, map<string, map<string, bsoncxx::types::value> >& elements) {
    auto odoc = bsoncxx::builder::basic::document{};

    for(const auto &name: fields) {
        odoc.append(kvp(name, 1));
    }

    stages.project(odoc.view());

    try {
        auto cursor = db[colName].aggregate(stages);

        bool notEmpty = false;

        for (auto doc_v: cursor) {
            notEmpty = true;

            //TODO check if id was passed as field
            if ((distance(doc_v.begin(), doc_v.end()) != fields.size() - 1) && (distance(doc_v.begin(), doc_v.end()) != fields.size())) {
                logger->log(l_id, "getFieldsAdvanced got invalid fields count");
                logger->log(l_id, std::to_string(distance(doc_v.begin(), doc_v.end())) + " != " + std::to_string(fields.size()));
                return false;
            }

            bsoncxx::document::element t_id = doc_v["filename"];

            if (!t_id || t_id.type() != bsoncxx::type::k_utf8) {
                logger->log(l_id, "getFieldsAdvanced got invalid filename field");
                return false;
            }

            string id = bsoncxx::string::to_string(t_id.get_utf8().value);

            elements.emplace(id, map<string, bsoncxx::types::value>{});

            for (bsoncxx::v_noabi::document::element val: doc_v) {
                string key = bsoncxx::string::to_string(val.key());
                elements[id].emplace(key, val.get_value());
            }
        }

        if(!notEmpty) {
            logger->log(l_id, "getFieldsAdvanced got empty result");
        }

        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while getting fields (4): " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while getting fields (4): unknown error");
        return false;
    }
}

bool Database::setField(string& colName, string& fieldName, bsoncxx::oid id, bsoncxx::types::value& val) {
    try {
        db[colName].update_one(make_document(kvp("_id", id)),
                               make_document(kvp("$set", make_document(kvp(fieldName, val)))));
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while setting field: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while setting field: unknown error");
        return false;
    }

    return true;
}

bool Database::setField(string& colName, string& fieldName, bsoncxx::oid id, bsoncxx::types::value&& val) {
    return setField(colName, fieldName, id, val);
}

bool Database::setField(string&& colName, string&& fieldName, bsoncxx::oid id, bsoncxx::types::value&& val) {
    return setField(colName, fieldName, id, val);
}

bool Database::setField(string&& colName, string&& fieldName, bsoncxx::oid id, string& newVal) {
    return setField(colName, fieldName, id, bsoncxx::types::value{bsoncxx::types::b_utf8{newVal}});
}

bool Database::setField(string&& colName, string&& fieldName, bsoncxx::oid id, const uint8_t* newVal, uint32_t newValSize) {
    bsoncxx::types::b_binary b_val{};
    b_val.bytes = newVal;
    b_val.size = newValSize;
    return setField(colName, fieldName, id, bsoncxx::types::value{b_val});
}

bool Database::setField(string&& colName, string&& fieldName, bsoncxx::oid id, int64_t& newVal) {
    bsoncxx::types::b_int64 tmp{};
    tmp.value = newVal;
    return setField(colName, fieldName, id, bsoncxx::types::value{tmp});
}

bool Database::setFieldCurrentDate(string&& colName, string&& fieldName, bsoncxx::oid id, std::chrono::system_clock::time_point& returnVal) {
    bsoncxx::types::b_date tmp{std::chrono::system_clock::now()};
    if(setField(colName, fieldName, id, bsoncxx::types::value{tmp})) {
        returnVal = tmp;
        return true;
    }

    return false;
}

bool Database::setField(string&& colName, string&& fieldName, bsoncxx::oid id, bool newVal) {
    bsoncxx::types::b_bool tmp{};
    tmp.value = newVal;
    return setField(colName, fieldName, id, bsoncxx::types::value{tmp});
}

bool Database::incField(string&& colName, string&& fieldName, string&& idFieldName, bsoncxx::oid& id,
                        string&& matchFieldName, string& matchFieldVal, int64_t diff) {
    try {
        db[colName].update_one(make_document(kvp(idFieldName, id), kvp(matchFieldName, matchFieldVal)),
                               make_document(kvp("$inc", make_document(kvp(fieldName, diff)))));
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while incrementing field: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while incrementing field: unknown error");
        return false;
    }

    return true;
}

bool Database::incField(string&& colName, bsoncxx::oid& id, string&& incField, int64_t incVal = 1) {
    try {
        db[colName].update_one(make_document(kvp("_id", id)),
                               make_document(kvp("$inc", make_document(kvp(incField, incVal)))));
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while incrementing field (2): " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while incrementing field (2): unknown error");
        return false;
    }

    return true;
}

bool Database::countField(string&& colName, string&& fieldName, bsoncxx::oid id, const uint8_t* valToCheck, uint32_t valSize, uint64_t& res) {
    bsoncxx::types::b_binary b_val{};
    b_val.bytes = valToCheck;
    b_val.size = valSize;

    try {
        res = (uint64_t) db[colName].count(make_document(kvp("_id", id), kvp(fieldName, b_val)));
        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while counting binary fields: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while counting binary fields: unknown error");
        return false;
    }
}

bool Database::countField(string&& colName, string&& fieldName, const string& fieldVal, string&& idFieldName, bsoncxx::oid id, uint64_t& res) {
    try {
        res = (uint64_t) db[colName].count(make_document(kvp(idFieldName, id), kvp(fieldName, fieldVal)));
        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while counting string fields: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while counting string fields: unknown error");
        return false;
    }
}

bool Database::countField(string&& colName, string&& fieldName, const string& fieldVal, uint64_t& res) {
    try {
        res = (uint64_t) db[colName].count(make_document(kvp(fieldName, fieldVal)));
        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while simple counting string fields: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while simple counting string fields: unknown error");
        return false;
    }
}

bool Database::removeFieldFromArray(string&& colName, string&& arrayName, bsoncxx::oid id, bsoncxx::document::value&& val) {
    try {
        db[colName].update_one(make_document(kvp("_id", id)),
                               make_document(kvp("$pull", make_document(kvp(arrayName, val)))));
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while removing field from array: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while removing field from array: unknown error");
        return false;
    }

    return true;
}

bool Database::removeFieldFromArrays(string&& colName, string&& arrayName, string&& fieldName, bsoncxx::types::value&& val) {
    string fullName = arrayName + "." + fieldName;
    try {
        db[colName].update_many(make_document(kvp(fullName, val)),
                               make_document(kvp("$pull", make_document(kvp(arrayName, make_document(kvp(fieldName, val)))))));
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while removing field from arrays: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while removing field from arrays: unknown error");
        return false;
    }

    return true;
}

bool Database::pushValToArr(string&& colName, string&& arrayName, bsoncxx::oid id, bsoncxx::document::value&& val) {
    try {
        db[colName].update_one(make_document(kvp("_id", id)),
                               make_document(kvp("$push", make_document(kvp(arrayName, val)))));
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while pushing to array: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while pushing to array: unknown error");
        return false;
    }

    return true;
}

bool Database::insertDoc(string&& colName, bsoncxx::oid& id, bsoncxx::builder::basic::document& doc) {
    try {
        auto res = db[colName].insert_one(doc.view());

        if(!res) {
            logger->log(l_id, "insertDoc failed while inserting");
            return false;
        }

        if (res->inserted_id().type() != bsoncxx::type::k_oid) {
            logger->log(l_id, "insertDoc hasn't got inserted id");
            return false;
        }

        id = res->inserted_id().get_oid().value;
        return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while inserting doc: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while inserting doc: unknown error");
        return false;
    }
}

bsoncxx::types::b_binary Database::stringToBinary(const string& str) {
    bsoncxx::types::b_binary b_sid{};
    b_sid.bytes = (const uint8_t*) str.c_str();
    b_sid.size = (uint32_t) str.size();
    return b_sid;
}

bool Database::removeByOid(string&& colName, string&& fieldName, bsoncxx::oid& fieldValue) {
    try {
        db[colName].delete_many(make_document(kvp(fieldName, fieldValue)));
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while deleting by oid: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while deleting by oid: unknown error");
        return false;
    }

    return true;
}

bool Database::sumFieldAdvanced(string&& colName, string&& resFieldName, mongocxx::pipeline& stages, uint64_t& res) {
    stages.project(make_document(kvp(resFieldName, 1)));

    try {
        auto cursor = db[colName].aggregate(stages);

        auto doc_i = cursor.begin();

        if (doc_i == cursor.end() || doc_i->empty()) {
            logger->log(l_id, "sumFieldAdvanced got empty resultSet");
            res = 0;
            return true; // !!
        }

        auto val = doc_i->begin();

        if (bsoncxx::string::to_string(val->key()) == resFieldName && val->type() == bsoncxx::type::k_int64) {
            res = (uint64_t) val->get_int64().value;
            return true;
        }

        return false;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while summing field: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while summing field: unknown error");
        return false;
    }
}

bool Database::deleteDocs(string&& colName, bsoncxx::document::value&& doc) {
    try {
       db[colName].delete_many(doc.view());
       return true;
    } catch (const std::exception& ex) {
        logger->err(l_id, "error while summing field: " + string(ex.what()));
        return false;
    } catch (...) {
        logger->err(l_id, "error while summing field: unknown error");
        return false;
    }
}