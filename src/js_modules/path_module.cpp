#include "js_modules.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
        bool isSeparator(char ch)
        {
            return ch == '\\' || ch == '/';
        }

        bool isAlpha(char ch)
        {
            return std::isalpha(static_cast<unsigned char>(ch)) != 0;
        }

        std::string toBackslashes(std::string path)
        {
            std::replace(path.begin(), path.end(), '/', '\\');
            return path;
        }

        std::string trimTrailingSeparators(std::string path)
        {
            while (path.size() > 1 && isSeparator(path.back()))
            {
                if (path.size() == 3 && path[1] == ':')
                {
                    break;
                }
                path.pop_back();
            }
            return path;
        }

        std::string lowerAscii(std::string text)
        {
            for (char &ch : text)
            {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return text;
        }

        bool isAbsolutePath(const std::string &path)
        {
            if (path.size() >= 3 && isAlpha(path[0]) && path[1] == ':' && isSeparator(path[2]))
            {
                return true;
            }
            return path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]);
        }

        std::string currentDirectory()
        {
            char buffer[MAX_PATH] = {};
            const DWORD length = GetCurrentDirectoryA(MAX_PATH, buffer);
            return length == 0 || length >= MAX_PATH ? std::string(".") : std::string(buffer);
        }

        std::string rootOf(const std::string &rawPath, size_t &start)
        {
            const std::string path = toBackslashes(rawPath);
            start = 0;

            if (path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]))
            {
                size_t first = path.find('\\', 2);
                if (first == std::string::npos)
                {
                    start = path.size();
                    return path;
                }

                size_t second = path.find('\\', first + 1);
                if (second == std::string::npos)
                {
                    start = path.size();
                    return path;
                }

                start = second + 1;
                return path.substr(0, start);
            }

            if (path.size() >= 2 && isAlpha(path[0]) && path[1] == ':')
            {
                if (path.size() >= 3 && isSeparator(path[2]))
                {
                    start = 3;
                    return path.substr(0, 3);
                }
                start = 2;
                return path.substr(0, 2);
            }

            if (!path.empty() && isSeparator(path[0]))
            {
                start = 1;
                return "\\";
            }

            return "";
        }

        std::vector<std::string> splitParts(const std::string &path, size_t start)
        {
            std::vector<std::string> parts;
            std::string part;
            for (size_t i = start; i < path.size(); ++i)
            {
                if (isSeparator(path[i]))
                {
                    if (!part.empty())
                    {
                        parts.push_back(part);
                        part.clear();
                    }
                }
                else
                {
                    part += path[i];
                }
            }

            if (!part.empty())
            {
                parts.push_back(part);
            }
            return parts;
        }

        std::string joinParts(const std::string &root, const std::vector<std::string> &parts)
        {
            std::string result = root;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (!result.empty() && !isSeparator(result.back()))
                {
                    result += "\\";
                }
                result += parts[i];
            }

            if (result.empty())
            {
                return ".";
            }
            return result;
        }

        std::string normalizePath(const std::string &rawPath)
        {
            if (rawPath.empty())
            {
                return ".";
            }

            const std::string path = toBackslashes(rawPath);
            size_t start = 0;
            const std::string root = rootOf(path, start);
            const bool absolute = isAbsolutePath(path);
            const std::vector<std::string> input = splitParts(path, start);
            std::vector<std::string> output;

            for (const std::string &part : input)
            {
                if (part.empty() || part == ".")
                {
                    continue;
                }

                if (part == "..")
                {
                    if (!output.empty() && output.back() != "..")
                    {
                        output.pop_back();
                    }
                    else if (!absolute)
                    {
                        output.push_back(part);
                    }
                }
                else
                {
                    output.push_back(part);
                }
            }

            return trimTrailingSeparators(joinParts(root, output));
        }

        std::string directoryName(const std::string &rawPath)
        {
            const std::string path = trimTrailingSeparators(toBackslashes(rawPath));
            size_t start = 0;
            const std::string root = rootOf(path, start);
            const size_t slash = path.find_last_of("\\/");
            if (slash == std::string::npos || slash < start)
            {
                return root.empty() ? "." : trimTrailingSeparators(root);
            }
            if (slash == 0)
            {
                return "\\";
            }
            return trimTrailingSeparators(path.substr(0, slash));
        }

        std::string baseName(const std::string &rawPath)
        {
            const std::string path = trimTrailingSeparators(toBackslashes(rawPath));
            size_t start = 0;
            rootOf(path, start);
            const size_t slash = path.find_last_of("\\/");
            if (slash == std::string::npos || slash < start)
            {
                return path.substr(start);
            }
            return path.substr(slash + 1);
        }

        std::string extensionName(const std::string &rawPath)
        {
            const std::string base = baseName(rawPath);
            const size_t dot = base.find_last_of('.');
            if (dot == std::string::npos || dot == 0)
            {
                return "";
            }
            return base.substr(dot);
        }

        std::string resolvePath(const std::vector<std::string> &paths)
        {
            std::string resolved;
            for (auto it = paths.rbegin(); it != paths.rend(); ++it)
            {
                if (it->empty())
                {
                    continue;
                }
                resolved = resolved.empty() ? *it : *it + "\\" + resolved;
                if (isAbsolutePath(*it))
                {
                    break;
                }
            }

            if (!isAbsolutePath(resolved))
            {
                resolved = currentDirectory() + "\\" + resolved;
            }
            return normalizePath(resolved);
        }

        std::string toForwardSlashes(std::string s)
        {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        }

        std::string toBackslashesCopy(const std::string &s)
        {
            std::string out = s;
            std::replace(out.begin(), out.end(), '/', '\\');
            return out;
        }

        std::vector<std::string> pathArgs(JSContext *context, int argc, JSValueConst *argv)
        {
            std::vector<std::string> paths;
            for (int i = 0; i < argc; ++i)
            {
                paths.push_back(valueToString(context, argv[i]));
            }
            return paths;
        }

        std::string objectString(JSContext *context, JSValueConst object, const char *name)
        {
            JSValue value = JS_GetPropertyStr(context, object, name);
            const std::string result = JS_IsUndefined(value) ? std::string() : valueToString(context, value);
            JS_FreeValue(context, value);
            return result;
        }

        JSValue jsPathJoin(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string joined;
            for (const std::string &path : pathArgs(context, argc, argv))
            {
                if (path.empty())
                {
                    continue;
                }
                if (!joined.empty())
                {
                    joined += "\\";
                }
                joined += path;
            }
            return JS_NewString(context, normalizePath(joined).c_str());
        }

        JSValue jsPathResolve(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            return JS_NewString(context, resolvePath(pathArgs(context, argc, argv)).c_str());
        }

        JSValue jsPathBasename(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "path.basename expects a path");
            }

            std::string base = baseName(valueToString(context, argv[0]));
            if (argc >= 2)
            {
                const std::string ext = valueToString(context, argv[1]);
                if (!ext.empty() && base.size() >= ext.size() && base.substr(base.size() - ext.size()) == ext)
                {
                    base.erase(base.size() - ext.size());
                }
            }
            return JS_NewString(context, base.c_str());
        }

        JSValue jsPathDirname(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "path.dirname expects a path");
            }
            return JS_NewString(context, directoryName(valueToString(context, argv[0])).c_str());
        }

        JSValue jsPathExtname(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "path.extname expects a path");
            }
            return JS_NewString(context, extensionName(valueToString(context, argv[0])).c_str());
        }

        JSValue jsPathNormalize(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "path.normalize expects a path");
            }
            return JS_NewString(context, normalizePath(valueToString(context, argv[0])).c_str());
        }

        JSValue jsPathIsAbsolute(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "path.isAbsolute expects a path");
            }
            return JS_NewBool(context, isAbsolutePath(valueToString(context, argv[0])));
        }

        JSValue jsPathParse(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1)
            {
                return JS_ThrowTypeError(context, "path.parse expects a path");
            }

            const std::string path = trimTrailingSeparators(toBackslashes(valueToString(context, argv[0])));
            size_t start = 0;
            const std::string root = rootOf(path, start);
            const std::string dir = directoryName(path);
            const std::string base = baseName(path);
            const std::string ext = extensionName(path);
            const std::string name = ext.empty() ? base : base.substr(0, base.size() - ext.size());

            JSValue object = JS_NewObject(context);
            JS_SetPropertyStr(context, object, "root", JS_NewString(context, root.c_str()));
            JS_SetPropertyStr(context, object, "dir", JS_NewString(context, dir.c_str()));
            JS_SetPropertyStr(context, object, "base", JS_NewString(context, base.c_str()));
            JS_SetPropertyStr(context, object, "ext", JS_NewString(context, ext.c_str()));
            JS_SetPropertyStr(context, object, "name", JS_NewString(context, name.c_str()));
            return object;
        }

        JSValue jsPathFormat(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || !JS_IsObject(argv[0]))
            {
                return JS_ThrowTypeError(context, "path.format expects a path object");
            }

            const std::string dir = objectString(context, argv[0], "dir");
            const std::string root = objectString(context, argv[0], "root");
            std::string base = objectString(context, argv[0], "base");
            if (base.empty())
            {
                base = objectString(context, argv[0], "name") + objectString(context, argv[0], "ext");
            }

            std::string result = dir.empty() ? root : dir;
            if (!result.empty() && !isSeparator(result.back()))
            {
                result += "\\";
            }
            result += base;
            return JS_NewString(context, result.c_str());
        }

        JSValue jsPathRelative(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2)
            {
                return JS_ThrowTypeError(context, "path.relative expects from and to");
            }

            const std::string from = resolvePath({valueToString(context, argv[0])});
            const std::string to = resolvePath({valueToString(context, argv[1])});

            size_t fromStart = 0;
            size_t toStart = 0;
            const std::string fromRoot = lowerAscii(rootOf(from, fromStart));
            const std::string toRoot = lowerAscii(rootOf(to, toStart));
            if (fromRoot != toRoot)
            {
                return JS_NewString(context, to.c_str());
            }

            const std::vector<std::string> fromParts = splitParts(from, fromStart);
            const std::vector<std::string> toParts = splitParts(to, toStart);
            size_t same = 0;
            while (same < fromParts.size() && same < toParts.size() && lowerAscii(fromParts[same]) == lowerAscii(toParts[same]))
            {
                ++same;
            }

            std::vector<std::string> resultParts;
            for (size_t i = same; i < fromParts.size(); ++i)
            {
                resultParts.push_back("..");
            }
            for (size_t i = same; i < toParts.size(); ++i)
            {
                resultParts.push_back(toParts[i]);
            }

            const std::string result = joinParts("", resultParts);
            return JS_NewString(context, result == "." ? "" : result.c_str());
        }

        /* POSIX variants (thin wrappers that reuse Windows logic with slash conversions) */
        JSValue jsPathJoinPosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::string joined;
            for (const std::string &path : pathArgs(context, argc, argv))
            {
                if (path.empty()) continue;
                if (!joined.empty()) joined += '/';
                joined += path;
            }
            const std::string winified = toBackslashesCopy(joined);
            return JS_NewString(context, toForwardSlashes(normalizePath(winified)).c_str());
        }

        JSValue jsPathResolvePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            std::vector<std::string> args = pathArgs(context, argc, argv);
            for (auto &p : args) p = toBackslashesCopy(p);
            const std::string resolved = resolvePath(args);
            return JS_NewString(context, toForwardSlashes(resolved).c_str());
        }

        JSValue jsPathBasenamePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1) return JS_ThrowTypeError(context, "path.basename expects a path");
            std::string base = baseName(toBackslashesCopy(valueToString(context, argv[0])));
            if (argc >= 2)
            {
                const std::string ext = valueToString(context, argv[1]);
                if (!ext.empty() && base.size() >= ext.size() && base.substr(base.size() - ext.size()) == ext)
                {
                    base.erase(base.size() - ext.size());
                }
            }
            return JS_NewString(context, base.c_str());
        }

        JSValue jsPathDirnamePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1) return JS_ThrowTypeError(context, "path.dirname expects a path");
            std::string dir = directoryName(toBackslashesCopy(valueToString(context, argv[0])));
            return JS_NewString(context, toForwardSlashes(dir).c_str());
        }

        JSValue jsPathExtnamePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1) return JS_ThrowTypeError(context, "path.extname expects a path");
            return JS_NewString(context, extensionName(toBackslashesCopy(valueToString(context, argv[0]))).c_str());
        }

        JSValue jsPathNormalizePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1) return JS_ThrowTypeError(context, "path.normalize expects a path");
            return JS_NewString(context, toForwardSlashes(normalizePath(toBackslashesCopy(valueToString(context, argv[0])))).c_str());
        }

        JSValue jsPathIsAbsolutePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1) return JS_ThrowTypeError(context, "path.isAbsolute expects a path");
            const std::string p = valueToString(context, argv[0]);
            return JS_NewBool(context, !p.empty() && p[0] == '/');
        }

        JSValue jsPathParsePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1) return JS_ThrowTypeError(context, "path.parse expects a path");
            const std::string path = valueToString(context, argv[0]);
            const std::string win = toBackslashesCopy(path);
            size_t start = 0;
            const std::string root = rootOf(win, start);
            const std::string dir = directoryName(win);
            const std::string base = baseName(win);
            const std::string ext = extensionName(win);
            const std::string name = ext.empty() ? base : base.substr(0, base.size() - ext.size());

            JSValue object = JS_NewObject(context);
            JS_SetPropertyStr(context, object, "root", JS_NewString(context, toForwardSlashes(root).c_str()));
            JS_SetPropertyStr(context, object, "dir", JS_NewString(context, toForwardSlashes(dir).c_str()));
            JS_SetPropertyStr(context, object, "base", JS_NewString(context, base.c_str()));
            JS_SetPropertyStr(context, object, "ext", JS_NewString(context, ext.c_str()));
            JS_SetPropertyStr(context, object, "name", JS_NewString(context, name.c_str()));
            return object;
        }

        JSValue jsPathFormatPosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || !JS_IsObject(argv[0])) return JS_ThrowTypeError(context, "path.format expects a path object");
            const std::string dir = toForwardSlashes(objectString(context, argv[0], "dir"));
            const std::string root = toForwardSlashes(objectString(context, argv[0], "root"));
            std::string base = objectString(context, argv[0], "base");
            if (base.empty()) base = objectString(context, argv[0], "name") + objectString(context, argv[0], "ext");
            std::string result = dir.empty() ? root : dir;
            if (!result.empty() && result.back() != '/') result += '/';
            result += base;
            return JS_NewString(context, result.c_str());
        }

        JSValue jsPathRelativePosix(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 2) return JS_ThrowTypeError(context, "path.relative expects from and to");
            std::string from = toBackslashesCopy(valueToString(context, argv[0]));
            std::string to = toBackslashesCopy(valueToString(context, argv[1]));
            size_t fromStart = 0, toStart = 0;
            const std::string fromRoot = lowerAscii(rootOf(from, fromStart));
            const std::string toRoot = lowerAscii(rootOf(to, toStart));
            if (fromRoot != toRoot) return JS_NewString(context, toForwardSlashes(to).c_str());
            const std::vector<std::string> fromParts = splitParts(from, fromStart);
            const std::vector<std::string> toParts = splitParts(to, toStart);
            size_t same = 0;
            while (same < fromParts.size() && same < toParts.size() && lowerAscii(fromParts[same]) == lowerAscii(toParts[same])) ++same;
            std::vector<std::string> resultParts;
            for (size_t i = same; i < fromParts.size(); ++i) resultParts.push_back("..");
            for (size_t i = same; i < toParts.size(); ++i) resultParts.push_back(toParts[i]);
            const std::string result = joinParts("", resultParts);
            return JS_NewString(context, result == "." ? "" : toForwardSlashes(result).c_str());
        }

        JSValue jsToNamespacedPath(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1) return JS_ThrowTypeError(context, "toNamespacedPath expects a path");
            std::string p = valueToString(context, argv[0]);
            if (p.empty()) return JS_NewString(context, "");
            std::string win = toBackslashesCopy(p);
            if (win.rfind("\\\\?\\", 0) == 0) return JS_NewString(context, win.c_str());
            if (win.rfind("\\\\", 0) == 0)
            {
                // UNC path: \\server\share -> \\?\UNC\server\share
                return JS_NewString(context, ("\\\\?\\UNC" + win.substr(1)).c_str());
            }
            return JS_NewString(context, ("\\\\?\\" + win).c_str());
        }
    }

    JSValue createPathModule(JSContext *context)
    {
        JSValue path = JS_NewObject(context);
        JS_SetPropertyStr(context, path, "join", JS_NewCFunction(context, jsPathJoin, "join", 1));
        JS_SetPropertyStr(context, path, "resolve", JS_NewCFunction(context, jsPathResolve, "resolve", 1));
        JS_SetPropertyStr(context, path, "basename", JS_NewCFunction(context, jsPathBasename, "basename", 2));
        JS_SetPropertyStr(context, path, "dirname", JS_NewCFunction(context, jsPathDirname, "dirname", 1));
        JS_SetPropertyStr(context, path, "extname", JS_NewCFunction(context, jsPathExtname, "extname", 1));
        JS_SetPropertyStr(context, path, "parse", JS_NewCFunction(context, jsPathParse, "parse", 1));
        JS_SetPropertyStr(context, path, "format", JS_NewCFunction(context, jsPathFormat, "format", 1));
        JS_SetPropertyStr(context, path, "normalize", JS_NewCFunction(context, jsPathNormalize, "normalize", 1));
        JS_SetPropertyStr(context, path, "isAbsolute", JS_NewCFunction(context, jsPathIsAbsolute, "isAbsolute", 1));
        JS_SetPropertyStr(context, path, "relative", JS_NewCFunction(context, jsPathRelative, "relative", 2));
        JS_SetPropertyStr(context, path, "sep", JS_NewString(context, "\\"));
        JS_SetPropertyStr(context, path, "delimiter", JS_NewString(context, ";"));
        JS_SetPropertyStr(context, path, "toNamespacedPath", JS_NewCFunction(context, jsToNamespacedPath, "toNamespacedPath", 1));

        // win32 namespace duplicates the main implementation
        JS_SetPropertyStr(context, path, "win32", JS_DupValue(context, path));

        // posix namespace: create object with posix variants
        JSValue posix = JS_NewObject(context);
        JS_SetPropertyStr(context, posix, "join", JS_NewCFunction(context, jsPathJoinPosix, "join", 1));
        JS_SetPropertyStr(context, posix, "resolve", JS_NewCFunction(context, jsPathResolvePosix, "resolve", 1));
        JS_SetPropertyStr(context, posix, "basename", JS_NewCFunction(context, jsPathBasenamePosix, "basename", 2));
        JS_SetPropertyStr(context, posix, "dirname", JS_NewCFunction(context, jsPathDirnamePosix, "dirname", 1));
        JS_SetPropertyStr(context, posix, "extname", JS_NewCFunction(context, jsPathExtnamePosix, "extname", 1));
        JS_SetPropertyStr(context, posix, "parse", JS_NewCFunction(context, jsPathParsePosix, "parse", 1));
        JS_SetPropertyStr(context, posix, "format", JS_NewCFunction(context, jsPathFormatPosix, "format", 1));
        JS_SetPropertyStr(context, posix, "normalize", JS_NewCFunction(context, jsPathNormalizePosix, "normalize", 1));
        JS_SetPropertyStr(context, posix, "isAbsolute", JS_NewCFunction(context, jsPathIsAbsolutePosix, "isAbsolute", 1));
        JS_SetPropertyStr(context, posix, "relative", JS_NewCFunction(context, jsPathRelativePosix, "relative", 2));
        JS_SetPropertyStr(context, posix, "sep", JS_NewString(context, "/"));
        JS_SetPropertyStr(context, posix, "delimiter", JS_NewString(context, ":"));
        JS_SetPropertyStr(context, path, "posix", posix);

        return path;
    }
}
