#include "js_modules.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdio>
#include <direct.h>
#include <fstream>
#include <io.h>
#include <iterator>
#include <string>
#include <vector>

#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
        const int kAccessExists = 0;
        const int kAccessWrite = 2;
        const int kAccessRead = 4;

        std::string filePathFromJs(JSContext *context, JSValueConst value)
        {
            return valueToString(context, value);
        }

        bool propertyBool(JSContext *context, JSValueConst object, const char *name)
        {
            JSValue value = JS_GetPropertyStr(context, object, name);
            const bool result = JS_ToBool(context, value) != 0;
            JS_FreeValue(context, value);
            return result;
        }

        std::string propertyString(JSContext *context, JSValueConst object, const char *name)
        {
            JSValue value = JS_GetPropertyStr(context, object, name);
            const std::string result = JS_IsUndefined(value) ? std::string() : valueToString(context, value);
            JS_FreeValue(context, value);
            return result;
        }

        bool isUtf8Encoding(JSContext *context, JSValueConst value)
        {
            if (JS_IsUndefined(value))
            {
                return false;
            }

            const std::string encoding = JS_IsObject(value)
                                             ? propertyString(context, value, "encoding")
                                             : valueToString(context, value);
            return encoding == "utf8" || encoding == "utf-8";
        }

        bool isRecursiveOption(JSContext *context, JSValueConst value)
        {
            return JS_IsObject(value) && propertyBool(context, value, "recursive");
        }

        bool dataFromJs(JSContext *context, JSValueConst value, std::vector<uint8_t> &bytes)
        {
            if (JS_IsString(value))
            {
                const std::string text = valueToString(context, value);
                bytes.assign(text.begin(), text.end());
                return true;
            }

            std::string error;
            return bytesFromJs(context, value, bytes, error);
        }

        std::string fsDataTypeFromOptions(JSContext *context, JSValueConst options)
        {
            if (JS_IsUndefined(options) || JS_IsNull(options))
            {
                return "json";
            }

            if (JS_IsString(options))
            {
                return valueToString(context, options);
            }

            if (!JS_IsObject(options))
            {
                return "json";
            }

            JSValue typeValue = JS_GetPropertyStr(context, options, "type");
            const std::string type = JS_IsUndefined(typeValue) || JS_IsNull(typeValue)
                ? std::string("json")
                : valueToString(context, typeValue);
            JS_FreeValue(context, typeValue);
            return type.empty() ? std::string("json") : type;
        }

        bool fsAppendFromOptions(JSContext *context, JSValueConst options)
        {
            if (!JS_IsObject(options) || JS_IsNull(options))
            {
                return false;
            }

            JSValue appendValue = JS_GetPropertyStr(context, options, "append");
            const bool append = JS_ToBool(context, appendValue) != 0;
            JS_FreeValue(context, appendValue);
            return append;
        }

        bool readFileBytes(const std::string &path, std::vector<uint8_t> &bytes)
        {
            std::ifstream file(path.c_str(), std::ios::binary);
            if (!file)
            {
                return false;
            }

            bytes.assign(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
            return true;
        }

        JSValue fsEncodeFileData(JSContext *context, JSValueConst data, const std::string &type, std::vector<uint8_t> &bytes)
        {
            if (type == "json")
            {
                JSValue json = JS_JSONStringify(context, data, JS_UNDEFINED, JS_UNDEFINED);
                if (JS_IsException(json))
                {
                    return json;
                }

                if (JS_IsUndefined(json))
                {
                    bytes.assign({'n', 'u', 'l', 'l'});
                }
                else
                {
                    const std::string text = valueToString(context, json);
                    bytes.assign(text.begin(), text.end());
                }
                JS_FreeValue(context, json);
                return JS_UNDEFINED;
            }

            if (type == "text")
            {
                const std::string text = valueToString(context, data);
                bytes.assign(text.begin(), text.end());
                return JS_UNDEFINED;
            }

            if (type == "bin")
            {
                std::string error;
                if (!bytesFromJs(context, data, bytes, error))
                {
                    return JS_ThrowTypeError(context, "bin file data must be Uint8Array, ArrayBuffer, or byte array: %s", error.c_str());
                }
                return JS_UNDEFINED;
            }

            if (type == "msgpack")
            {
                JSValue encoded = encodeMsgpackValue(context, data);
                if (JS_IsException(encoded))
                {
                    return encoded;
                }

                std::string error;
                if (!bytesFromJs(context, encoded, bytes, error))
                {
                    JS_FreeValue(context, encoded);
                    return JS_ThrowTypeError(context, "msgpack encoder did not produce bytes: %s", error.c_str());
                }

                JS_FreeValue(context, encoded);
                return JS_UNDEFINED;
            }

            return JS_ThrowTypeError(context, "file type must be 'json', 'bin', 'msgpack', or 'text'");
        }

        JSValue fsDecodeFileData(JSContext *context, const std::vector<uint8_t> &bytes, const std::string &type, const std::string &path)
        {
            if (type == "json")
            {
                return JS_ParseJSON(context, reinterpret_cast<const char *>(bytes.data()), bytes.size(), path.c_str());
            }

            if (type == "text")
            {
                return JS_NewStringLen(context, reinterpret_cast<const char *>(bytes.data()), bytes.size());
            }

            if (type == "bin")
            {
                return JS_NewUint8ArrayCopy(context, bytes.data(), bytes.size());
            }

            if (type == "msgpack")
            {
                return decodeMsgpackBytes(context, bytes.data(), bytes.size());
            }

            return JS_ThrowTypeError(context, "file type must be 'json', 'bin', 'msgpack', or 'text'");
        }

        bool makeDirectoryRecursive(const std::string &path)
        {
            if (path.empty())
            {
                return false;
            }

            std::string partial;
            for (size_t i = 0; i < path.size(); ++i)
            {
                partial += path[i];
                if ((path[i] == '\\' || path[i] == '/') && partial.size() > 1)
                {
                    if (partial.size() == 3 && partial[1] == ':')
                    {
                        continue;
                    }
                    _mkdir(partial.c_str());
                }
            }

            if (_mkdir(path.c_str()) == 0)
            {
                return true;
            }

            const DWORD attributes = GetFileAttributesA(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        bool removeRecursive(const std::string &path)
        {
            const DWORD attributes = GetFileAttributesA(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                return false;
            }

            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                return DeleteFileA(path.c_str()) != 0;
            }

            std::string search = path;
            if (!search.empty() && search.back() != '\\' && search.back() != '/')
            {
                search += "\\";
            }
            search += "*";

            WIN32_FIND_DATAA data = {};
            HANDLE handle = FindFirstFileA(search.c_str(), &data);
            if (handle != INVALID_HANDLE_VALUE)
            {
                do
                {
                    const std::string name = data.cFileName;
                    if (name == "." || name == "..")
                    {
                        continue;
                    }

                    std::string child = path;
                    if (!child.empty() && child.back() != '\\' && child.back() != '/')
                    {
                        child += "\\";
                    }
                    child += name;

                    if (!removeRecursive(child))
                    {
                        FindClose(handle);
                        return false;
                    }
                } while (FindNextFileA(handle, &data) != 0);

                FindClose(handle);
            }

            return RemoveDirectoryA(path.c_str()) != 0;
        }

        double fileTimeToUnixMs(const FILETIME &time)
        {
            ULARGE_INTEGER value = {};
            value.LowPart = time.dwLowDateTime;
            value.HighPart = time.dwHighDateTime;
            return static_cast<double>(value.QuadPart / 10000ULL) - 11644473600000.0;
        }

        JSValue jsStatIsFile(JSContext *context, JSValueConst thisValue, int, JSValueConst *)
        {
            JSValue value = JS_GetPropertyStr(context, thisValue, "_isFile");
            const bool result = JS_ToBool(context, value) != 0;
            JS_FreeValue(context, value);
            return JS_NewBool(context, result);
        }

        JSValue jsStatIsDirectory(JSContext *context, JSValueConst thisValue, int, JSValueConst *)
        {
            JSValue value = JS_GetPropertyStr(context, thisValue, "_isDirectory");
            const bool result = JS_ToBool(context, value) != 0;
            JS_FreeValue(context, value);
            return JS_NewBool(context, result);
        }

        JSValue jsFsReadFileSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.readFileSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            std::vector<uint8_t> bytes;
            if (!readFileBytes(path, bytes))
            {
                return JS_ThrowInternalError(context, "failed to open file for reading: %s", path.c_str());
            }

            if (argc >= 2 && isUtf8Encoding(context, argv[1]))
            {
                return JS_NewStringLen(context, reinterpret_cast<const char *>(bytes.data()), bytes.size());
            }

            return JS_NewUint8ArrayCopy(context, bytes.data(), bytes.size());
        }

        JSValue writeFileLike(JSContext *context, int argc, JSValueConst *argv, bool append)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, append ? "fs.appendFileSync expects a path and data" : "fs.writeFileSync expects a path and data");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            std::vector<uint8_t> bytes;
            if (!dataFromJs(context, argv[1], bytes))
            {
                return JS_ThrowTypeError(context, "file data must be a string, Uint8Array, ArrayBuffer, or byte array");
            }

            std::ofstream file(path.c_str(), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!file)
            {
                return JS_ThrowInternalError(context, "failed to open file for writing: %s", path.c_str());
            }

            if (!bytes.empty())
            {
                file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }

            if (!file)
            {
                return JS_ThrowInternalError(context, "failed to write file: %s", path.c_str());
            }

            return JS_UNDEFINED;
        }

        JSValue jsFsWriteFileSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return writeFileLike(context, argc, argv, false);
        }

        JSValue jsFsAppendFileSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return writeFileLike(context, argc, argv, true);
        }

        JSValue jsFsReadFileValue(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.readFile expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            const std::string type = fsDataTypeFromOptions(context, argc >= 2 ? argv[1] : JS_UNDEFINED);

            std::vector<uint8_t> bytes;
            if (!readFileBytes(path, bytes))
            {
                return JS_ThrowInternalError(context, "failed to open file for reading: %s", path.c_str());
            }

            return fsDecodeFileData(context, bytes, type, path);
        }

        JSValue jsJsonLoad(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
            {
                return JS_ThrowTypeError(context, "JSON.load expects a file path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            std::vector<uint8_t> bytes;
            if (!readFileBytes(path, bytes))
            {
                return JS_ThrowInternalError(context, "failed to open JSON file for reading: %s", path.c_str());
            }

            return fsDecodeFileData(context, bytes, "json", path);
        }

        JSValue jsFsWriteFileValue(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "fs.writeFile expects a path and data");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            JSValueConst options = argc >= 3 ? argv[2] : JS_UNDEFINED;
            const std::string type = fsDataTypeFromOptions(context, options);
            const bool append = fsAppendFromOptions(context, options);

            std::vector<uint8_t> bytes;
            JSValue encoded = fsEncodeFileData(context, argv[1], type, bytes);
            if (JS_IsException(encoded))
            {
                return encoded;
            }
            JS_FreeValue(context, encoded);

            std::ofstream file(path.c_str(), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!file)
            {
                return JS_ThrowInternalError(context, "failed to open file for writing: %s", path.c_str());
            }

            if (!bytes.empty())
            {
                file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }

            if (!file)
            {
                return JS_ThrowInternalError(context, "failed to write file: %s", path.c_str());
            }

            return JS_UNDEFINED;
        }

        JSValue jsFsExistsSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.existsSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            return JS_NewBool(context, GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES);
        }

        JSValue jsFsAccessSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.accessSync expects a path");
            }

            int32_t mode = kAccessExists;
            if (argc >= 2 && !JS_IsUndefined(argv[1]))
            {
                JS_ToInt32(context, &mode, argv[1]);
            }

            const std::string path = filePathFromJs(context, argv[0]);
            if (_access(path.c_str(), mode) != 0)
            {
                return JS_ThrowInternalError(context, "file is not accessible: %s", path.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsMkdirSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.mkdirSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            const bool ok = argc >= 2 && isRecursiveOption(context, argv[1])
                                ? makeDirectoryRecursive(path)
                                : _mkdir(path.c_str()) == 0;

            if (!ok)
            {
                return JS_ThrowInternalError(context, "failed to create directory: %s", path.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsRmdirSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.rmdirSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            const bool ok = argc >= 2 && isRecursiveOption(context, argv[1])
                                ? removeRecursive(path)
                                : RemoveDirectoryA(path.c_str()) != 0;
            if (!ok)
            {
                return JS_ThrowInternalError(context, "failed to remove directory: %s", path.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsRmSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.rmSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            const DWORD attributes = GetFileAttributesA(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                return JS_ThrowInternalError(context, "path does not exist: %s", path.c_str());
            }

            const bool recursive = argc >= 2 && isRecursiveOption(context, argv[1]);
            const bool ok = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                                ? (recursive ? removeRecursive(path) : RemoveDirectoryA(path.c_str()) != 0)
                                : DeleteFileA(path.c_str()) != 0;

            if (!ok)
            {
                return JS_ThrowInternalError(context, "failed to remove path: %s", path.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsUnlinkSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.unlinkSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            if (DeleteFileA(path.c_str()) == 0)
            {
                return JS_ThrowInternalError(context, "failed to unlink file: %s", path.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsRenameSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "fs.renameSync expects oldPath and newPath");
            }

            const std::string oldPath = filePathFromJs(context, argv[0]);
            const std::string newPath = filePathFromJs(context, argv[1]);
            if (MoveFileExA(oldPath.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
            {
                return JS_ThrowInternalError(context, "failed to rename path: %s", oldPath.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsCopyFileSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "fs.copyFileSync expects src and dest");
            }

            const std::string src = filePathFromJs(context, argv[0]);
            const std::string dest = filePathFromJs(context, argv[1]);
            if (CopyFileA(src.c_str(), dest.c_str(), FALSE) == 0)
            {
                return JS_ThrowInternalError(context, "failed to copy file: %s", src.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsReaddirSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.readdirSync expects a path");
            }

            std::string search = filePathFromJs(context, argv[0]);
            if (!search.empty() && search.back() != '\\' && search.back() != '/')
            {
                search += "\\";
            }
            search += "*";

            JSValue array = JS_NewArray(context);
            WIN32_FIND_DATAA data = {};
            HANDLE handle = FindFirstFileA(search.c_str(), &data);
            if (handle == INVALID_HANDLE_VALUE)
            {
                JS_FreeValue(context, array);
                return JS_ThrowInternalError(context, "failed to read directory");
            }

            uint32_t index = 0;
            do
            {
                const std::string name = data.cFileName;
                if (name != "." && name != "..")
                {
                    JS_SetPropertyUint32(context, array, index, JS_NewString(context, name.c_str()));
                    ++index;
                }
            } while (FindNextFileA(handle, &data) != 0);

            FindClose(handle);
            return array;
        }

        JSValue jsFsStatSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.statSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            WIN32_FILE_ATTRIBUTE_DATA data = {};
            if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data) == 0)
            {
                return JS_ThrowInternalError(context, "failed to stat path: %s", path.c_str());
            }

            ULARGE_INTEGER size = {};
            size.LowPart = data.nFileSizeLow;
            size.HighPart = data.nFileSizeHigh;

            const bool isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            JSValue stat = JS_NewObject(context);
            JS_SetPropertyStr(context, stat, "size", JS_NewFloat64(context, static_cast<double>(size.QuadPart)));
            JS_SetPropertyStr(context, stat, "atimeMs", JS_NewFloat64(context, fileTimeToUnixMs(data.ftLastAccessTime)));
            JS_SetPropertyStr(context, stat, "mtimeMs", JS_NewFloat64(context, fileTimeToUnixMs(data.ftLastWriteTime)));
            JS_SetPropertyStr(context, stat, "ctimeMs", JS_NewFloat64(context, fileTimeToUnixMs(data.ftCreationTime)));
            JS_SetPropertyStr(context, stat, "birthtimeMs", JS_NewFloat64(context, fileTimeToUnixMs(data.ftCreationTime)));
            JS_SetPropertyStr(context, stat, "_isFile", JS_NewBool(context, !isDirectory));
            JS_SetPropertyStr(context, stat, "_isDirectory", JS_NewBool(context, isDirectory));
            JS_SetPropertyStr(context, stat, "isFile", JS_NewCFunction(context, jsStatIsFile, "isFile", 0));
            JS_SetPropertyStr(context, stat, "isDirectory", JS_NewCFunction(context, jsStatIsDirectory, "isDirectory", 0));
            return stat;
        }

        JSValue jsFsTruncateSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.truncateSync expects a path");
            }

            int64_t length = 0;
            if (argc >= 2 && !JS_IsUndefined(argv[1]))
            {
                JS_ToInt64(context, &length, argv[1]);
            }

            const std::string path = filePathFromJs(context, argv[0]);
            FILE *file = nullptr;
            if (fopen_s(&file, path.c_str(), "r+b") != 0 || file == nullptr)
            {
                return JS_ThrowInternalError(context, "failed to open file for truncating: %s", path.c_str());
            }

            const int result = _chsize_s(_fileno(file), length);
            fclose(file);
            if (result != 0)
            {
                return JS_ThrowInternalError(context, "failed to truncate file: %s", path.c_str());
            }
            return JS_UNDEFINED;
        }

        JSValue jsFsRealpathSync(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "fs.realpathSync expects a path");
            }

            const std::string path = filePathFromJs(context, argv[0]);
            char buffer[MAX_PATH] = {};
            const DWORD length = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
            if (length == 0 || length >= MAX_PATH)
            {
                return JS_ThrowInternalError(context, "failed to resolve path: %s", path.c_str());
            }
            return JS_NewString(context, buffer);
        }

        typedef JSValue (*FsSyncFunction)(JSContext *, JSValueConst, int, JSValueConst *);

        JSValue callSyncForCallback(JSContext *context, FsSyncFunction function, int argc, JSValueConst *argv)
        {
            if (argc < 1 || !JS_IsFunction(context, argv[argc - 1]))
            {
                return JS_ThrowTypeError(context, "callback function is required");
            }

            JSValue callback = JS_DupValue(context, argv[argc - 1]);
            JSValue result = function(context, JS_UNDEFINED, argc - 1, argv);
            if (JS_IsException(result))
            {
                JSValue error = JS_GetException(context);
                JSValue args[1] = {error};
                JSValue callResult = JS_Call(context, callback, JS_UNDEFINED, 1, args);
                JS_FreeValue(context, callResult);
                JS_FreeValue(context, error);
                JS_FreeValue(context, callback);
                return JS_UNDEFINED;
            }

            JSValue args[2] = {JS_NULL, result};
            JSValue callResult = JS_Call(context, callback, JS_UNDEFINED, 2, args);
            JS_FreeValue(context, callResult);
            JS_FreeValue(context, result);
            JS_FreeValue(context, callback);
            return JS_UNDEFINED;
        }

        JSValue callSyncForPromise(JSContext *context, FsSyncFunction function, int argc, JSValueConst *argv)
        {
            JSValue resolvingFunctions[2] = {};
            JSValue promise = JS_NewPromiseCapability(context, resolvingFunctions);
            if (JS_IsException(promise))
            {
                return promise;
            }

            JSValue result = function(context, JS_UNDEFINED, argc, argv);
            if (JS_IsException(result))
            {
                JSValue error = JS_GetException(context);
                JSValue rejectResult = JS_Call(context, resolvingFunctions[1], JS_UNDEFINED, 1, &error);
                JS_FreeValue(context, rejectResult);
                JS_FreeValue(context, error);
            }
            else
            {
                JSValue resolveResult = JS_Call(context, resolvingFunctions[0], JS_UNDEFINED, 1, &result);
                JS_FreeValue(context, resolveResult);
                JS_FreeValue(context, result);
            }

            JS_FreeValue(context, resolvingFunctions[0]);
            JS_FreeValue(context, resolvingFunctions[1]);
            return promise;
        }

        JSValue jsFsReadFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc >= 2 && JS_IsFunction(context, argv[argc - 1]))
            {
                return callSyncForCallback(context, jsFsReadFileSync, argc, argv);
            }

            return jsFsReadFileValue(context, JS_UNDEFINED, argc, argv);
        }

        JSValue jsFsReadFileCallback(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsReadFileSync, argc, argv);
        }

        JSValue jsFsWriteFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc >= 3 && JS_IsFunction(context, argv[argc - 1]))
            {
                return callSyncForCallback(context, jsFsWriteFileSync, argc, argv);
            }

            return jsFsWriteFileValue(context, JS_UNDEFINED, argc, argv);
        }

        JSValue jsFsWriteFileCallback(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsWriteFileSync, argc, argv);
        }

        JSValue jsFsAppendFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsAppendFileSync, argc, argv);
        }

        JSValue jsFsMkdir(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsMkdirSync, argc, argv);
        }

        JSValue jsFsRmdir(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsRmdirSync, argc, argv);
        }

        JSValue jsFsRm(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsRmSync, argc, argv);
        }

        JSValue jsFsReaddir(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsReaddirSync, argc, argv);
        }

        JSValue jsFsStat(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsStatSync, argc, argv);
        }

        JSValue jsFsLstat(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsStatSync, argc, argv);
        }

        JSValue jsFsRename(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsRenameSync, argc, argv);
        }

        JSValue jsFsUnlink(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsUnlinkSync, argc, argv);
        }

        JSValue jsFsCopyFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsCopyFileSync, argc, argv);
        }

        JSValue jsFsAccess(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForCallback(context, jsFsAccessSync, argc, argv);
        }

        JSValue jsFsPromisesReadFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsReadFileSync, argc, argv);
        }

        JSValue jsFsPromisesWriteFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsWriteFileSync, argc, argv);
        }

        JSValue jsFsPromisesAppendFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsAppendFileSync, argc, argv);
        }

        JSValue jsFsPromisesMkdir(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsMkdirSync, argc, argv);
        }

        JSValue jsFsPromisesRmdir(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsRmdirSync, argc, argv);
        }

        JSValue jsFsPromisesRm(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsRmSync, argc, argv);
        }

        JSValue jsFsPromisesReaddir(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsReaddirSync, argc, argv);
        }

        JSValue jsFsPromisesStat(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsStatSync, argc, argv);
        }

        JSValue jsFsPromisesLstat(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsStatSync, argc, argv);
        }

        JSValue jsFsPromisesRename(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsRenameSync, argc, argv);
        }

        JSValue jsFsPromisesUnlink(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsUnlinkSync, argc, argv);
        }

        JSValue jsFsPromisesCopyFile(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsCopyFileSync, argc, argv);
        }

        JSValue jsFsPromisesAccess(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return callSyncForPromise(context, jsFsAccessSync, argc, argv);
        }

        JSValue jsWatcherClose(JSContext *, JSValueConst, int, JSValueConst *)
        {
            return JS_UNDEFINED;
        }

        JSValue jsFsWatch(JSContext *context, JSValueConst, int, JSValueConst *)
        {
            JSValue watcher = JS_NewObject(context);
            JS_SetPropertyStr(context, watcher, "close", JS_NewCFunction(context, jsWatcherClose, "close", 0));
            JS_SetPropertyStr(context, watcher, "ref", JS_NewCFunction(context, jsWatcherClose, "ref", 0));
            JS_SetPropertyStr(context, watcher, "unref", JS_NewCFunction(context, jsWatcherClose, "unref", 0));
            return watcher;
        }

        JSValue jsFsWatchFile(JSContext *, JSValueConst, int, JSValueConst *)
        {
            return JS_UNDEFINED;
        }

        JSValue jsFsUnwatchFile(JSContext *, JSValueConst, int, JSValueConst *)
        {
            return JS_UNDEFINED;
        }

        JSValue createFsPromisesModule(JSContext *context)
        {
            JSValue promises = JS_NewObject(context);
            JS_SetPropertyStr(context, promises, "readFile", JS_NewCFunction(context, jsFsPromisesReadFile, "readFile", 2));
            JS_SetPropertyStr(context, promises, "writeFile", JS_NewCFunction(context, jsFsPromisesWriteFile, "writeFile", 2));
            JS_SetPropertyStr(context, promises, "appendFile", JS_NewCFunction(context, jsFsPromisesAppendFile, "appendFile", 2));
            JS_SetPropertyStr(context, promises, "mkdir", JS_NewCFunction(context, jsFsPromisesMkdir, "mkdir", 2));
            JS_SetPropertyStr(context, promises, "rmdir", JS_NewCFunction(context, jsFsPromisesRmdir, "rmdir", 2));
            JS_SetPropertyStr(context, promises, "rm", JS_NewCFunction(context, jsFsPromisesRm, "rm", 2));
            JS_SetPropertyStr(context, promises, "readdir", JS_NewCFunction(context, jsFsPromisesReaddir, "readdir", 1));
            JS_SetPropertyStr(context, promises, "stat", JS_NewCFunction(context, jsFsPromisesStat, "stat", 1));
            JS_SetPropertyStr(context, promises, "lstat", JS_NewCFunction(context, jsFsPromisesLstat, "lstat", 1));
            JS_SetPropertyStr(context, promises, "rename", JS_NewCFunction(context, jsFsPromisesRename, "rename", 2));
            JS_SetPropertyStr(context, promises, "unlink", JS_NewCFunction(context, jsFsPromisesUnlink, "unlink", 1));
            JS_SetPropertyStr(context, promises, "copyFile", JS_NewCFunction(context, jsFsPromisesCopyFile, "copyFile", 2));
            JS_SetPropertyStr(context, promises, "access", JS_NewCFunction(context, jsFsPromisesAccess, "access", 2));
            return promises;
        }

        JSValue createFsModule(JSContext *context)
        {
            JSValue fs = JS_NewObject(context);
            JS_SetPropertyStr(context, fs, "readFile", JS_NewCFunction(context, jsFsReadFile, "readFile", 3));
            JS_SetPropertyStr(context, fs, "writeFile", JS_NewCFunction(context, jsFsWriteFile, "writeFile", 4));
            JS_SetPropertyStr(context, fs, "appendFile", JS_NewCFunction(context, jsFsAppendFile, "appendFile", 4));
            JS_SetPropertyStr(context, fs, "mkdir", JS_NewCFunction(context, jsFsMkdir, "mkdir", 3));
            JS_SetPropertyStr(context, fs, "rmdir", JS_NewCFunction(context, jsFsRmdir, "rmdir", 3));
            JS_SetPropertyStr(context, fs, "rm", JS_NewCFunction(context, jsFsRm, "rm", 3));
            JS_SetPropertyStr(context, fs, "readdir", JS_NewCFunction(context, jsFsReaddir, "readdir", 3));
            JS_SetPropertyStr(context, fs, "stat", JS_NewCFunction(context, jsFsStat, "stat", 2));
            JS_SetPropertyStr(context, fs, "lstat", JS_NewCFunction(context, jsFsLstat, "lstat", 2));
            JS_SetPropertyStr(context, fs, "rename", JS_NewCFunction(context, jsFsRename, "rename", 3));
            JS_SetPropertyStr(context, fs, "unlink", JS_NewCFunction(context, jsFsUnlink, "unlink", 2));
            JS_SetPropertyStr(context, fs, "copyFile", JS_NewCFunction(context, jsFsCopyFile, "copyFile", 3));
            JS_SetPropertyStr(context, fs, "access", JS_NewCFunction(context, jsFsAccess, "access", 3));
            JS_SetPropertyStr(context, fs, "watch", JS_NewCFunction(context, jsFsWatch, "watch", 3));
            JS_SetPropertyStr(context, fs, "watchFile", JS_NewCFunction(context, jsFsWatchFile, "watchFile", 3));
            JS_SetPropertyStr(context, fs, "unwatchFile", JS_NewCFunction(context, jsFsUnwatchFile, "unwatchFile", 2));
            JS_SetPropertyStr(context, fs, "readFileSync", JS_NewCFunction(context, jsFsReadFileSync, "readFileSync", 2));
            JS_SetPropertyStr(context, fs, "writeFileSync", JS_NewCFunction(context, jsFsWriteFileSync, "writeFileSync", 2));
            JS_SetPropertyStr(context, fs, "appendFileSync", JS_NewCFunction(context, jsFsAppendFileSync, "appendFileSync", 2));
            JS_SetPropertyStr(context, fs, "existsSync", JS_NewCFunction(context, jsFsExistsSync, "existsSync", 1));
            JS_SetPropertyStr(context, fs, "accessSync", JS_NewCFunction(context, jsFsAccessSync, "accessSync", 2));
            JS_SetPropertyStr(context, fs, "mkdirSync", JS_NewCFunction(context, jsFsMkdirSync, "mkdirSync", 2));
            JS_SetPropertyStr(context, fs, "rmdirSync", JS_NewCFunction(context, jsFsRmdirSync, "rmdirSync", 2));
            JS_SetPropertyStr(context, fs, "rmSync", JS_NewCFunction(context, jsFsRmSync, "rmSync", 2));
            JS_SetPropertyStr(context, fs, "unlinkSync", JS_NewCFunction(context, jsFsUnlinkSync, "unlinkSync", 1));
            JS_SetPropertyStr(context, fs, "renameSync", JS_NewCFunction(context, jsFsRenameSync, "renameSync", 2));
            JS_SetPropertyStr(context, fs, "copyFileSync", JS_NewCFunction(context, jsFsCopyFileSync, "copyFileSync", 2));
            JS_SetPropertyStr(context, fs, "readdirSync", JS_NewCFunction(context, jsFsReaddirSync, "readdirSync", 1));
            JS_SetPropertyStr(context, fs, "statSync", JS_NewCFunction(context, jsFsStatSync, "statSync", 1));
            JS_SetPropertyStr(context, fs, "lstatSync", JS_NewCFunction(context, jsFsStatSync, "lstatSync", 1));
            JS_SetPropertyStr(context, fs, "truncateSync", JS_NewCFunction(context, jsFsTruncateSync, "truncateSync", 2));
            JS_SetPropertyStr(context, fs, "realpathSync", JS_NewCFunction(context, jsFsRealpathSync, "realpathSync", 1));
            JS_SetPropertyStr(context, fs, "F_OK", JS_NewInt32(context, kAccessExists));
            JS_SetPropertyStr(context, fs, "R_OK", JS_NewInt32(context, kAccessRead));
            JS_SetPropertyStr(context, fs, "W_OK", JS_NewInt32(context, kAccessWrite));
            JS_SetPropertyStr(context, fs, "promises", createFsPromisesModule(context));
            return fs;
        }

        JSValue jsRequire(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "require expects a module name");
            }

            const std::string moduleName = valueToString(context, argv[0]);
            if (moduleName == "fs" || moduleName == "node:fs" ||
                moduleName == "fs/promises" || moduleName == "node:fs/promises" ||
                moduleName == "path" || moduleName == "node:path" || moduleName == "pathUtilities" ||
                moduleName == "sqlite" || moduleName == "node:sqlite" ||
                moduleName == "excel" ||
                moduleName == "dialog" ||
                moduleName == "color" ||
                moduleName == "layer" ||
                moduleName == "entity" ||
                moduleName == "net" || moduleName == "node:net")
            {
                JSValue global = JS_GetGlobalObject(context);
                JSValue cache = JS_GetPropertyStr(context, global, "__nativeModules");
                const char *cacheName =
                    moduleName == "node:net" ? "net" : moduleName == "net" ? "net"
                    : moduleName == "entity" ? "entity" : moduleName == "layer" ? "layer" : moduleName == "color" ? "color" : moduleName == "dialog" ? "dialog" : moduleName == "excel" ? "excel" : moduleName == "sqlite" || moduleName == "node:sqlite" ? "sqlite"
                        : moduleName == "path" || moduleName == "node:path" || moduleName == "pathUtilities" ? "path"
                        : moduleName == "fs/promises" || moduleName == "node:fs/promises" ? "fs/promises"
                                                                                                     : "fs";
                JSValue module = JS_GetPropertyStr(context, cache, cacheName);
                JS_FreeValue(context, cache);
                JS_FreeValue(context, global);
                return module;
            }

            return JS_ThrowReferenceError(context, "module not found: %s", moduleName.c_str());
        }
    }

    void installRequire(JSContext *context)
    {
        JSValue global = JS_GetGlobalObject(context);
        JSValue nativeModules = JS_NewObject(context);
        JSValue pathModule = createPathModule(context);
        JSValue fsModule = createFsModule(context);
        JSValue sqliteModule = createSqliteModule(context);
        JSValue processModule = createProcessModule(context);
        JSValue excelModule = createExcelModule(context);
        JSValue dialogModule = createDialogModule(context);
        JSValue colorModule = createColorModule(context);
        JSValue layerModule = createLayerModule(context);
        JSValue entityModule = createEntityModule(context);
        JSValue netModule = createNetModule(context);
        JSValue fsPromises = JS_GetPropertyStr(context, fsModule, "promises");
        JSValue jsonObject = JS_GetPropertyStr(context, global, "JSON");
        if (JS_IsObject(jsonObject))
        {
            JS_SetPropertyStr(context, jsonObject, "load", JS_NewCFunction(context, jsJsonLoad, "load", 1));
        }
        JS_FreeValue(context, jsonObject);
        JS_SetPropertyStr(context, nativeModules, "fs", fsModule);
        JS_SetPropertyStr(context, nativeModules, "process", processModule);
        JS_SetPropertyStr(context, nativeModules, "fs/promises", fsPromises);
        JS_SetPropertyStr(context, nativeModules, "path", JS_DupValue(context, pathModule));
        JS_SetPropertyStr(context, nativeModules, "sqlite", sqliteModule);
        JS_SetPropertyStr(context, nativeModules, "excel", JS_DupValue(context, excelModule));
        JS_SetPropertyStr(context, nativeModules, "dialog", JS_DupValue(context, dialogModule));
        JS_SetPropertyStr(context, nativeModules, "color", JS_DupValue(context, colorModule));
        JS_SetPropertyStr(context, nativeModules, "layer", JS_DupValue(context, layerModule));
        JS_SetPropertyStr(context, nativeModules, "entity", JS_DupValue(context, entityModule));
        JS_SetPropertyStr(context, nativeModules, "net", JS_DupValue(context, netModule));
        // expose convenient globals at startup
        JS_SetPropertyStr(context, global, "fs", JS_DupValue(context, fsModule));
        JS_SetPropertyStr(context, global, "path", JS_DupValue(context, pathModule));
        JS_SetPropertyStr(context, global, "sqlite", JS_DupValue(context, sqliteModule));
        JS_SetPropertyStr(context, global, "excel", JS_DupValue(context, excelModule));
        JS_SetPropertyStr(context, global, "dialog", JS_DupValue(context, dialogModule));
        JS_SetPropertyStr(context, global, "color", JS_DupValue(context, colorModule));
        JS_SetPropertyStr(context, global, "layer", JS_DupValue(context, layerModule));
        JS_SetPropertyStr(context, global, "entity", JS_DupValue(context, entityModule));
        JS_SetPropertyStr(context, global, "net", JS_DupValue(context, netModule));
        JS_SetPropertyStr(context, global, "pathUtilities", pathModule);
        JS_SetPropertyStr(context, global, "process", JS_DupValue(context, processModule));
        JS_SetPropertyStr(context, global, "__nativeModules", nativeModules);
        JS_SetPropertyStr(context, global, "require", JS_NewCFunction(context, jsRequire, "require", 1));
        JS_FreeValue(context, entityModule);
        JS_FreeValue(context, dialogModule);
        JS_FreeValue(context, layerModule);
        JS_FreeValue(context, netModule);
        JS_FreeValue(context, global);
    }
}
