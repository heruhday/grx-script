#include "js_modules.h"

#include <cstdint>
#include <string>
#include <vector>

#include "js_module_utils.h"
#include "sqlite3.h"

namespace js_modules
{
    namespace
    {
        JSClassID g_databaseClassId = 0;
        JSClassID g_statementClassId = 0;

        struct DatabaseData
        {
            sqlite3 *db = nullptr;
            std::string location;
        };

        struct StatementData
        {
            sqlite3_stmt *statement = nullptr;
            sqlite3 *db = nullptr;
            std::string sourceSql;
            bool allowBareNamedParameters = false;
            bool readBigInts = false;
        };

        DatabaseData *databaseData(JSContext *context, JSValueConst value)
        {
            DatabaseData *data = static_cast<DatabaseData *>(JS_GetOpaque2(context, value, g_databaseClassId));
            if (data == nullptr)
            {
                JS_ThrowTypeError(context, "expected DatabaseSync");
            }
            return data;
        }

        StatementData *statementData(JSContext *context, JSValueConst value)
        {
            StatementData *data = static_cast<StatementData *>(JS_GetOpaque2(context, value, g_statementClassId));
            if (data == nullptr)
            {
                JS_ThrowTypeError(context, "expected StatementSync");
            }
            return data;
        }

        JSValue sqliteError(JSContext *context, sqlite3 *db, const char *fallback)
        {
            const char *message = db != nullptr ? sqlite3_errmsg(db) : fallback;
            return JS_ThrowInternalError(context, "%s", message != nullptr ? message : fallback);
        }

        void databaseFinalizer(JSRuntime *, JSValue value)
        {
            DatabaseData *data = static_cast<DatabaseData *>(JS_GetOpaque(value, g_databaseClassId));
            if (data != nullptr)
            {
                if (data->db != nullptr)
                {
                    sqlite3_close(data->db);
                }
                delete data;
            }
        }

        void statementFinalizer(JSRuntime *, JSValue value)
        {
            StatementData *data = static_cast<StatementData *>(JS_GetOpaque(value, g_statementClassId));
            if (data != nullptr)
            {
                if (data->statement != nullptr)
                {
                    sqlite3_finalize(data->statement);
                }
                delete data;
            }
        }

        bool openDatabase(JSContext *context, DatabaseData *data)
        {
            if (data->db != nullptr)
            {
                return true;
            }

            if (sqlite3_open(data->location.c_str(), &data->db) != SQLITE_OK)
            {
                sqliteError(context, data->db, "failed to open database");
                if (data->db != nullptr)
                {
                    sqlite3_close(data->db);
                    data->db = nullptr;
                }
                return false;
            }
            return true;
        }

        JSValue columnToJs(JSContext *context, sqlite3_stmt *statement, int index, bool readBigInts)
        {
            switch (sqlite3_column_type(statement, index))
            {
            case SQLITE_INTEGER:
            {
                const sqlite3_int64 value = sqlite3_column_int64(statement, index);
                if (readBigInts)
                {
                    return JS_NewBigInt64(context, value);
                }
                return JS_NewFloat64(context, static_cast<double>(value));
            }
            case SQLITE_FLOAT:
                return JS_NewFloat64(context, sqlite3_column_double(statement, index));
            case SQLITE_TEXT:
                return JS_NewString(context, reinterpret_cast<const char *>(sqlite3_column_text(statement, index)));
            case SQLITE_BLOB:
            {
                const uint8_t *bytes = static_cast<const uint8_t *>(sqlite3_column_blob(statement, index));
                const int size = sqlite3_column_bytes(statement, index);
                return JS_NewUint8ArrayCopy(context, bytes, static_cast<size_t>(size));
            }
            case SQLITE_NULL:
            default:
                return JS_NULL;
            }
        }

        JSValue rowToObject(JSContext *context, StatementData *data)
        {
            JSValue row = JS_NewObject(context);
            const int columns = sqlite3_column_count(data->statement);
            for (int i = 0; i < columns; ++i)
            {
                const char *name = sqlite3_column_name(data->statement, i);
                JS_SetPropertyStr(context, row, name != nullptr ? name : "", columnToJs(context, data->statement, i, data->readBigInts));
            }
            return row;
        }

        bool bindOne(JSContext *context, StatementData *data, int index, JSValueConst value)
        {
            int status = SQLITE_OK;
            if (JS_IsNull(value) || JS_IsUndefined(value))
            {
                status = sqlite3_bind_null(data->statement, index);
            }
            else if (JS_IsBool(value))
            {
                status = sqlite3_bind_int(data->statement, index, JS_ToBool(context, value) != 0 ? 1 : 0);
            }
            else if (JS_IsNumber(value))
            {
                double number = 0.0;
                JS_ToFloat64(context, &number, value);
                sqlite3_int64 integer = static_cast<sqlite3_int64>(number);
                status = static_cast<double>(integer) == number
                             ? sqlite3_bind_int64(data->statement, index, integer)
                             : sqlite3_bind_double(data->statement, index, number);
            }
            else if (JS_IsString(value))
            {
                const std::string text = valueToString(context, value);
                status = sqlite3_bind_text(data->statement, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
            }
            else
            {
                std::vector<uint8_t> bytes;
                std::string error;
                if (bytesFromJs(context, value, bytes, error))
                {
                    status = sqlite3_bind_blob(data->statement, index, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT);
                }
                else
                {
                    JS_ThrowTypeError(context, "unsupported SQLite bind value");
                    return false;
                }
            }

            if (status != SQLITE_OK)
            {
                sqliteError(context, data->db, "failed to bind parameter");
                return false;
            }
            return true;
        }

        bool bindObject(JSContext *context, StatementData *data, JSValueConst object)
        {
            JSPropertyEnum *properties = nullptr;
            uint32_t count = 0;
            if (JS_GetOwnPropertyNames(context, &properties, &count, object, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
            {
                return false;
            }

            for (uint32_t i = 0; i < count; ++i)
            {
                JSValue keyValue = JS_AtomToString(context, properties[i].atom);
                std::string key = valueToString(context, keyValue);
                JS_FreeValue(context, keyValue);

                int index = sqlite3_bind_parameter_index(data->statement, key.c_str());
                if (index == 0 && data->allowBareNamedParameters && !key.empty() && key[0] != ':' && key[0] != '@' && key[0] != '$')
                {
                    const std::string colon = ":" + key;
                    const std::string at = "@" + key;
                    const std::string dollar = "$" + key;
                    index = sqlite3_bind_parameter_index(data->statement, colon.c_str());
                    if (index == 0)
                    {
                        index = sqlite3_bind_parameter_index(data->statement, at.c_str());
                    }
                    if (index == 0)
                    {
                        index = sqlite3_bind_parameter_index(data->statement, dollar.c_str());
                    }
                }

                if (index != 0)
                {
                    JSValue propertyValue = JS_GetProperty(context, object, properties[i].atom);
                    const bool ok = bindOne(context, data, index, propertyValue);
                    JS_FreeValue(context, propertyValue);
                    if (!ok)
                    {
                        JS_FreePropertyEnum(context, properties, count);
                        return false;
                    }
                }
            }

            JS_FreePropertyEnum(context, properties, count);
            return true;
        }

        bool bindParameters(JSContext *context, StatementData *data, int argc, JSValueConst *argv)
        {
            sqlite3_clear_bindings(data->statement);
            sqlite3_reset(data->statement);

            int positionalIndex = 1;
            for (int i = 0; i < argc; ++i)
            {
                if (i == 0 && JS_IsObject(argv[i]) && !JS_IsArray(argv[i]))
                {
                    if (!bindObject(context, data, argv[i]))
                    {
                        return false;
                    }
                }
                else if (JS_IsArray(argv[i]))
                {
                    const uint32_t length = arrayLength(context, argv[i]);
                    for (uint32_t j = 0; j < length; ++j)
                    {
                        JSValue item = JS_GetPropertyUint32(context, argv[i], j);
                        const bool ok = bindOne(context, data, positionalIndex, item);
                        JS_FreeValue(context, item);
                        if (!ok)
                        {
                            return false;
                        }
                        ++positionalIndex;
                    }
                }
                else
                {
                    if (!bindOne(context, data, positionalIndex, argv[i]))
                    {
                        return false;
                    }
                    ++positionalIndex;
                }
            }

            return true;
        }

        JSValue jsDatabaseConstructor(JSContext *context, JSValueConst newTarget, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "DatabaseSync expects a location");
            }

            JSValue prototype = JS_GetPropertyStr(context, newTarget, "prototype");
            JSValue object = JS_NewObjectProtoClass(context, prototype, g_databaseClassId);
            JS_FreeValue(context, prototype);
            if (JS_IsException(object))
            {
                return object;
            }

            DatabaseData *data = new DatabaseData();
            data->location = valueToString(context, argv[0]);
            JS_SetOpaque(object, data);
            if (!openDatabase(context, data))
            {
                JS_FreeValue(context, object);
                return JS_EXCEPTION;
            }
            return object;
        }

        JSValue jsDatabaseOpen(JSContext *context, JSValueConst thisValue, int, JSValueConst *)
        {
            DatabaseData *data = databaseData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }
            return openDatabase(context, data) ? JS_UNDEFINED : JS_EXCEPTION;
        }

        JSValue jsDatabaseClose(JSContext *context, JSValueConst thisValue, int, JSValueConst *)
        {
            DatabaseData *data = databaseData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }

            if (data->db != nullptr)
            {
                if (sqlite3_close(data->db) != SQLITE_OK)
                {
                    return sqliteError(context, data->db, "failed to close database");
                }
                data->db = nullptr;
            }
            return JS_UNDEFINED;
        }

        JSValue jsDatabaseExec(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            DatabaseData *data = databaseData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "database.exec expects SQL");
            }
            if (!openDatabase(context, data))
            {
                return JS_EXCEPTION;
            }

            const std::string sql = valueToString(context, argv[0]);
            char *error = nullptr;
            const int status = sqlite3_exec(data->db, sql.c_str(), nullptr, nullptr, &error);
            if (status != SQLITE_OK)
            {
                std::string message = error != nullptr ? error : sqlite3_errmsg(data->db);
                sqlite3_free(error);
                return JS_ThrowInternalError(context, "%s", message.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsDatabasePrepare(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            DatabaseData *database = databaseData(context, thisValue);
            if (database == nullptr)
            {
                return JS_EXCEPTION;
            }
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "database.prepare expects SQL");
            }
            if (!openDatabase(context, database))
            {
                return JS_EXCEPTION;
            }

            const std::string sql = valueToString(context, argv[0]);
            sqlite3_stmt *statement = nullptr;
            if (sqlite3_prepare_v2(database->db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
            {
                return sqliteError(context, database->db, "failed to prepare SQL");
            }

            JSValue object = JS_NewObjectClass(context, g_statementClassId);
            if (JS_IsException(object))
            {
                sqlite3_finalize(statement);
                return object;
            }

            StatementData *data = new StatementData();
            data->statement = statement;
            data->db = database->db;
            data->sourceSql = sql;
            JS_SetOpaque(object, data);
            return object;
        }

        JSValue jsNotImplemented(JSContext *context, JSValueConst, int, JSValueConst *)
        {
            return JS_ThrowTypeError(context, "this node:sqlite API is not implemented in this embedded runtime");
        }

        JSValue jsStatementAll(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            StatementData *data = statementData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }
            if (!bindParameters(context, data, argc, argv))
            {
                return JS_EXCEPTION;
            }

            JSValue array = JS_NewArray(context);
            uint32_t index = 0;
            for (;;)
            {
                const int status = sqlite3_step(data->statement);
                if (status == SQLITE_ROW)
                {
                    JS_SetPropertyUint32(context, array, index, rowToObject(context, data));
                    ++index;
                }
                else if (status == SQLITE_DONE)
                {
                    break;
                }
                else
                {
                    JS_FreeValue(context, array);
                    return sqliteError(context, data->db, "failed to execute statement");
                }
            }

            sqlite3_reset(data->statement);
            return array;
        }

        JSValue jsStatementGet(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            StatementData *data = statementData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }
            if (!bindParameters(context, data, argc, argv))
            {
                return JS_EXCEPTION;
            }

            const int status = sqlite3_step(data->statement);
            if (status == SQLITE_ROW)
            {
                JSValue row = rowToObject(context, data);
                sqlite3_reset(data->statement);
                return row;
            }
            if (status == SQLITE_DONE)
            {
                sqlite3_reset(data->statement);
                return JS_UNDEFINED;
            }
            return sqliteError(context, data->db, "failed to execute statement");
        }

        JSValue jsStatementRun(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            StatementData *data = statementData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }
            if (!bindParameters(context, data, argc, argv))
            {
                return JS_EXCEPTION;
            }

            int status = SQLITE_OK;
            do
            {
                status = sqlite3_step(data->statement);
            } while (status == SQLITE_ROW);

            if (status != SQLITE_DONE)
            {
                return sqliteError(context, data->db, "failed to execute statement");
            }

            JSValue result = JS_NewObject(context);
            JS_SetPropertyStr(context, result, "changes", JS_NewInt32(context, sqlite3_changes(data->db)));
            JS_SetPropertyStr(context, result, "lastInsertRowid", JS_NewBigInt64(context, sqlite3_last_insert_rowid(data->db)));
            sqlite3_reset(data->statement);
            return result;
        }

        JSValue jsStatementIterate(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            return jsStatementAll(context, thisValue, argc, argv);
        }

        JSValue jsStatementSetAllowBareNamedParameters(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            StatementData *data = statementData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }
            data->allowBareNamedParameters = argc > 0 && JS_ToBool(context, argv[0]) != 0;
            return JS_UNDEFINED;
        }

        JSValue jsStatementSetReadBigInts(JSContext *context, JSValueConst thisValue, int argc, JSValueConst *argv)
        {
            StatementData *data = statementData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }
            data->readBigInts = argc > 0 && JS_ToBool(context, argv[0]) != 0;
            return JS_UNDEFINED;
        }

        JSValue jsStatementSourceSql(JSContext *context, JSValueConst thisValue)
        {
            StatementData *data = statementData(context, thisValue);
            return data == nullptr ? JS_EXCEPTION : JS_NewString(context, data->sourceSql.c_str());
        }

        JSValue jsStatementSourceSqlGetter(JSContext *context, JSValueConst thisValue, int, JSValueConst *)
        {
            return jsStatementSourceSql(context, thisValue);
        }

        JSValue jsStatementExpandedSql(JSContext *context, JSValueConst thisValue)
        {
            StatementData *data = statementData(context, thisValue);
            if (data == nullptr)
            {
                return JS_EXCEPTION;
            }

            char *expanded = sqlite3_expanded_sql(data->statement);
            if (expanded == nullptr)
            {
                return JS_NULL;
            }
            JSValue result = JS_NewString(context, expanded);
            sqlite3_free(expanded);
            return result;
        }

        JSValue jsStatementExpandedSqlGetter(JSContext *context, JSValueConst thisValue, int, JSValueConst *)
        {
            return jsStatementExpandedSql(context, thisValue);
        }

        void ensureClasses(JSContext *context)
        {
            JSRuntime *runtime = JS_GetRuntime(context);
            if (g_databaseClassId == 0)
            {
                JS_NewClassID(runtime, &g_databaseClassId);
                JSClassDef classDef = {};
                classDef.class_name = "DatabaseSync";
                classDef.finalizer = databaseFinalizer;
                JS_NewClass(runtime, g_databaseClassId, &classDef);
            }

            if (g_statementClassId == 0)
            {
                JS_NewClassID(runtime, &g_statementClassId);
                JSClassDef classDef = {};
                classDef.class_name = "StatementSync";
                classDef.finalizer = statementFinalizer;
                JS_NewClass(runtime, g_statementClassId, &classDef);
            }
        }

        void setPrototypeFunction(JSContext *context, JSValueConst prototype, const char *name, JSCFunction *function, int length)
        {
            JS_SetPropertyStr(context, prototype, name, JS_NewCFunction(context, function, name, length));
        }

        void setPrototypeGetter(JSContext *context, JSValueConst prototype, const char *name, JSCFunction *getter)
        {
            JSAtom atom = JS_NewAtom(context, name);
            JS_DefinePropertyGetSet(
                context,
                prototype,
                atom,
                JS_NewCFunction(context, getter, name, 0),
                JS_UNDEFINED,
                JS_PROP_CONFIGURABLE);
            JS_FreeAtom(context, atom);
        }

        void setupDatabasePrototype(JSContext *context, JSValueConst prototype)
        {
            setPrototypeFunction(context, prototype, "open", jsDatabaseOpen, 0);
            setPrototypeFunction(context, prototype, "close", jsDatabaseClose, 0);
            setPrototypeFunction(context, prototype, "exec", jsDatabaseExec, 1);
            setPrototypeFunction(context, prototype, "prepare", jsDatabasePrepare, 1);
            setPrototypeFunction(context, prototype, "function", jsNotImplemented, 2);
            setPrototypeFunction(context, prototype, "aggregate", jsNotImplemented, 2);
            setPrototypeFunction(context, prototype, "loadExtension", jsNotImplemented, 1);
            setPrototypeFunction(context, prototype, "enableLoadExtension", jsNotImplemented, 1);
            setPrototypeFunction(context, prototype, "createSession", jsNotImplemented, 0);
            setPrototypeFunction(context, prototype, "applyChangeset", jsNotImplemented, 1);
            setPrototypeFunction(context, prototype, "serialize", jsNotImplemented, 0);
            setPrototypeFunction(context, prototype, "deserialize", jsNotImplemented, 1);
        }

        void setupStatementPrototype(JSContext *context, JSValueConst prototype)
        {
            setPrototypeFunction(context, prototype, "all", jsStatementAll, 0);
            setPrototypeFunction(context, prototype, "get", jsStatementGet, 0);
            setPrototypeFunction(context, prototype, "iterate", jsStatementIterate, 0);
            setPrototypeFunction(context, prototype, "run", jsStatementRun, 0);
            setPrototypeFunction(context, prototype, "setAllowBareNamedParameters", jsStatementSetAllowBareNamedParameters, 1);
            setPrototypeFunction(context, prototype, "setReadBigInts", jsStatementSetReadBigInts, 1);
            setPrototypeGetter(context, prototype, "sourceSQL", jsStatementSourceSqlGetter);
            setPrototypeGetter(context, prototype, "expandedSQL", jsStatementExpandedSqlGetter);
        }
    }

    JSValue createSqliteModule(JSContext *context)
    {
        ensureClasses(context);

        JSValue databasePrototype = JS_NewObject(context);
        setupDatabasePrototype(context, databasePrototype);
        JS_SetClassProto(context, g_databaseClassId, JS_DupValue(context, databasePrototype));

        JSValue statementPrototype = JS_NewObject(context);
        setupStatementPrototype(context, statementPrototype);
        JS_SetClassProto(context, g_statementClassId, JS_DupValue(context, statementPrototype));

        JSValue module = JS_NewObject(context);
        JSValue constructor = JS_NewCFunction2(context, jsDatabaseConstructor, "DatabaseSync", 1, JS_CFUNC_constructor, 0);
        JS_SetConstructor(context, constructor, databasePrototype);
        JS_SetPropertyStr(context, module, "DatabaseSync", constructor);
        JS_FreeValue(context, databasePrototype);
        JS_FreeValue(context, statementPrototype);
        return module;
    }
}
