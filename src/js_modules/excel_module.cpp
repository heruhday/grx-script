#include "js_modules.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <oleauto.h>

#include <string>
#include <vector>

#include "js_module_utils.h"

namespace js_modules
{
    namespace
    {
        const int xlSrcRange = 1;
        const int xlYes = 1;

        class ComApartment
        {
        public:
            ComApartment()
                : initialized_(false), hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
            {
                initialized_ = hr_ == S_OK || hr_ == S_FALSE;
                if (hr_ == RPC_E_CHANGED_MODE)
                {
                    initialized_ = false;
                    hr_ = S_OK;
                }
            }

            ~ComApartment()
            {
                if (initialized_)
                {
                    CoUninitialize();
                }
            }

            HRESULT status() const { return hr_; }

        private:
            bool initialized_;
            HRESULT hr_;
        };

        template <typename T>
        class ComPtr
        {
        public:
            ComPtr() : ptr_(nullptr) {}
            explicit ComPtr(T *ptr) : ptr_(ptr) {}
            ~ComPtr() { reset(); }

            ComPtr(const ComPtr &) = delete;
            ComPtr &operator=(const ComPtr &) = delete;

            T *get() const { return ptr_; }
            T **put()
            {
                reset();
                return &ptr_;
            }
            T *detach()
            {
                T *ptr = ptr_;
                ptr_ = nullptr;
                return ptr;
            }
            void reset(T *ptr = nullptr)
            {
                if (ptr_ != nullptr)
                {
                    ptr_->Release();
                }
                ptr_ = ptr;
            }

        private:
            T *ptr_;
        };

        std::wstring utf8ToWide(const std::string &text)
        {
            if (text.empty())
            {
                return std::wstring();
            }

            const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0)
            {
                return std::wstring(text.begin(), text.end());
            }

            std::wstring result(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], length);
            return result;
        }

        std::string wideToUtf8(const wchar_t *text, int length)
        {
            if (text == nullptr || length <= 0)
            {
                return std::string();
            }

            const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return std::string();
            }

            std::string result(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, length, &result[0], size, nullptr, nullptr);
            return result;
        }

        VARIANT makeIntVariant(int value)
        {
            VARIANT variant;
            VariantInit(&variant);
            variant.vt = VT_I4;
            variant.lVal = value;
            return variant;
        }

        VARIANT makeDispatchVariant(IDispatch *dispatch)
        {
            VARIANT variant;
            VariantInit(&variant);
            variant.vt = VT_DISPATCH;
            variant.pdispVal = dispatch;
            return variant;
        }

        VARIANT makeMissingVariant()
        {
            VARIANT variant;
            VariantInit(&variant);
            variant.vt = VT_ERROR;
            variant.scode = DISP_E_PARAMNOTFOUND;
            return variant;
        }

        JSValue throwComError(JSContext *context, const char *message, HRESULT hr)
        {
            return JS_ThrowInternalError(context, "%s (HRESULT 0x%08X)", message, static_cast<unsigned int>(hr));
        }

        HRESULT getDispId(IDispatch *dispatch, const wchar_t *name, DISPID &dispId)
        {
            LPOLESTR names[] = {const_cast<LPOLESTR>(name)};
            return dispatch->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispId);
        }

        HRESULT invokeDispatch(IDispatch *dispatch, const wchar_t *name, WORD flags, const std::vector<VARIANT> &args, VARIANT *result)
        {
            DISPID dispId = 0;
            HRESULT hr = getDispId(dispatch, name, dispId);
            if (FAILED(hr))
            {
                return hr;
            }

            std::vector<VARIANT> reversed(args.size());
            for (size_t i = 0; i < args.size(); ++i)
            {
                VariantInit(&reversed[i]);
                hr = VariantCopy(&reversed[i], const_cast<VARIANT *>(&args[args.size() - 1 - i]));
                if (FAILED(hr))
                {
                    for (size_t j = 0; j < i; ++j)
                    {
                        VariantClear(&reversed[j]);
                    }
                    return hr;
                }
            }

            DISPPARAMS params;
            params.rgvarg = reversed.empty() ? nullptr : &reversed[0];
            params.rgdispidNamedArgs = nullptr;
            params.cArgs = static_cast<UINT>(reversed.size());
            params.cNamedArgs = 0;

            DISPID propertyPut = DISPID_PROPERTYPUT;
            if ((flags & DISPATCH_PROPERTYPUT) != 0)
            {
                params.rgdispidNamedArgs = &propertyPut;
                params.cNamedArgs = 1;
            }

            EXCEPINFO excepInfo = {};
            UINT argErr = 0;
            hr = dispatch->Invoke(dispId, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result, &excepInfo, &argErr);

            if (excepInfo.bstrSource != nullptr)
            {
                SysFreeString(excepInfo.bstrSource);
            }
            if (excepInfo.bstrDescription != nullptr)
            {
                SysFreeString(excepInfo.bstrDescription);
            }
            if (excepInfo.bstrHelpFile != nullptr)
            {
                SysFreeString(excepInfo.bstrHelpFile);
            }

            for (size_t i = 0; i < reversed.size(); ++i)
            {
                VariantClear(&reversed[i]);
            }
            return hr;
        }

        HRESULT getDispatchProperty(IDispatch *dispatch, const wchar_t *name, const std::vector<VARIANT> &args, ComPtr<IDispatch> &result)
        {
            VARIANT value;
            VariantInit(&value);
            HRESULT hr = invokeDispatch(dispatch, name, DISPATCH_PROPERTYGET, args, &value);
            if (SUCCEEDED(hr) && value.vt == VT_DISPATCH && value.pdispVal != nullptr)
            {
                result.reset(value.pdispVal);
                value.vt = VT_EMPTY;
            }
            else if (SUCCEEDED(hr))
            {
                hr = DISP_E_TYPEMISMATCH;
            }
            VariantClear(&value);
            return hr;
        }

        HRESULT getVariantProperty(IDispatch *dispatch, const wchar_t *name, const std::vector<VARIANT> &args, VARIANT &result)
        {
            VariantInit(&result);
            return invokeDispatch(dispatch, name, DISPATCH_PROPERTYGET, args, &result);
        }

        HRESULT callDispatchMethod(IDispatch *dispatch, const wchar_t *name, const std::vector<VARIANT> &args, VARIANT *result)
        {
            return invokeDispatch(dispatch, name, DISPATCH_METHOD, args, result);
        }

        HRESULT setDispatchProperty(IDispatch *dispatch, const wchar_t *name, VARIANT &value)
        {
            std::vector<VARIANT> args;
            args.push_back(value);
            return invokeDispatch(dispatch, name, DISPATCH_PROPERTYPUT, args, nullptr);
        }

        HRESULT variantToLong(VARIANT &variant, LONG &value)
        {
            VARIANT converted;
            VariantInit(&converted);
            HRESULT hr = VariantChangeType(&converted, &variant, 0, VT_I4);
            if (FAILED(hr))
            {
                return hr;
            }

            value = converted.lVal;
            VariantClear(&converted);
            return S_OK;
        }

        HRESULT getCountProperty(IDispatch *dispatch, LONG &value)
        {
            VARIANT count;
            VariantInit(&count);
            HRESULT hr = getVariantProperty(dispatch, L"Count", std::vector<VARIANT>(), count);
            if (SUCCEEDED(hr))
            {
                hr = variantToLong(count, value);
            }
            VariantClear(&count);
            return hr;
        }

        HRESULT getActiveExcel(ComPtr<IDispatch> &excel)
        {
            CLSID clsid;
            HRESULT hr = CLSIDFromProgID(L"Excel.Application", &clsid);
            if (FAILED(hr))
            {
                return hr;
            }

            ComPtr<IUnknown> unknown;
            hr = GetActiveObject(clsid, nullptr, unknown.put());
            if (FAILED(hr))
            {
                return hr;
            }

            return unknown.get()->QueryInterface(IID_IDispatch, reinterpret_cast<void **>(excel.put()));
        }

        bool headerExists(const std::vector<std::string> &headers, const std::string &name)
        {
            for (size_t i = 0; i < headers.size(); ++i)
            {
                if (headers[i] == name)
                {
                    return true;
                }
            }
            return false;
        }

        JSValue collectHeaders(JSContext *context, JSValueConst rows, uint32_t rowCount, std::vector<std::string> &headers)
        {
            for (uint32_t rowIndex = 0; rowIndex < rowCount; ++rowIndex)
            {
                JSValue row = JS_GetPropertyUint32(context, rows, rowIndex);
                if (JS_IsException(row))
                {
                    return row;
                }

                if (!JS_IsObject(row) || JS_IsArray(row))
                {
                    JS_FreeValue(context, row);
                    return JS_ThrowTypeError(context, "excel.writeTable rows must be an array of objects");
                }

                JSPropertyEnum *properties = nullptr;
                uint32_t propertyCount = 0;
                if (JS_GetOwnPropertyNames(context, &properties, &propertyCount, row, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
                {
                    JS_FreeValue(context, row);
                    return JS_EXCEPTION;
                }

                for (uint32_t i = 0; i < propertyCount; ++i)
                {
                    JSValue keyValue = JS_AtomToString(context, properties[i].atom);
                    const std::string key = valueToString(context, keyValue);
                    JS_FreeValue(context, keyValue);
                    if (!headerExists(headers, key))
                    {
                        headers.push_back(key);
                    }
                }

                JS_FreePropertyEnum(context, properties, propertyCount);
                JS_FreeValue(context, row);
            }

            return JS_UNDEFINED;
        }

        JSValue collectProvidedHeaders(JSContext *context, JSValueConst value, std::vector<std::string> &headers)
        {
            if (!JS_IsArray(value))
            {
                return JS_ThrowTypeError(context, "excel.writeTable header must be an array of strings");
            }

            const uint32_t headerCount = arrayLength(context, value);
            if (headerCount == 0)
            {
                return JS_ThrowTypeError(context, "excel.writeTable header must contain at least one key");
            }

            headers.reserve(headerCount);
            for (uint32_t i = 0; i < headerCount; ++i)
            {
                JSValue header = JS_GetPropertyUint32(context, value, i);
                if (JS_IsException(header))
                {
                    return header;
                }

                if (!JS_IsString(header))
                {
                    JS_FreeValue(context, header);
                    return JS_ThrowTypeError(context, "excel.writeTable header values must be strings");
                }

                headers.push_back(valueToString(context, header));
                JS_FreeValue(context, header);
            }

            return JS_UNDEFINED;
        }

        bool jsValueToVariant(JSContext *context, JSValueConst value, VARIANT &variant)
        {
            VariantInit(&variant);

            if (JS_IsNull(value) || JS_IsUndefined(value))
            {
                variant.vt = VT_EMPTY;
                return true;
            }

            if (JS_IsBool(value))
            {
                const int boolValue = JS_ToBool(context, value);
                if (boolValue < 0)
                {
                    return false;
                }
                variant.vt = VT_BOOL;
                variant.boolVal = boolValue ? VARIANT_TRUE : VARIANT_FALSE;
                return true;
            }

            if (JS_IsNumber(value))
            {
                double number = 0.0;
                if (JS_ToFloat64(context, &number, value) < 0)
                {
                    return false;
                }
                variant.vt = VT_R8;
                variant.dblVal = number;
                return true;
            }

            const std::string text = JS_IsString(value) ? valueToString(context, value) : valueToInspectString(context, value);
            const std::wstring wide = utf8ToWide(text);
            variant.vt = VT_BSTR;
            variant.bstrVal = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
            return variant.bstrVal != nullptr || wide.empty();
        }

        std::string variantToString(VARIANT &value)
        {
            if (value.vt == VT_BSTR)
            {
                return wideToUtf8(value.bstrVal, static_cast<int>(SysStringLen(value.bstrVal)));
            }

            if (value.vt == VT_EMPTY || value.vt == VT_NULL)
            {
                return std::string();
            }

            VARIANT converted;
            VariantInit(&converted);
            if (SUCCEEDED(VariantChangeType(&converted, &value, 0, VT_BSTR)))
            {
                std::string text = wideToUtf8(converted.bstrVal, static_cast<int>(SysStringLen(converted.bstrVal)));
                VariantClear(&converted);
                return text;
            }

            VariantClear(&converted);
            return std::string();
        }

        JSValue variantToJsValue(JSContext *context, VARIANT &value)
        {
            switch (value.vt)
            {
            case VT_EMPTY:
            case VT_NULL:
                return JS_NULL;

            case VT_BOOL:
                return JS_NewBool(context, value.boolVal == VARIANT_TRUE);

            case VT_I1:
                return JS_NewInt32(context, value.cVal);
            case VT_UI1:
                return JS_NewInt32(context, value.bVal);
            case VT_I2:
                return JS_NewInt32(context, value.iVal);
            case VT_UI2:
                return JS_NewInt32(context, value.uiVal);
            case VT_I4:
            case VT_INT:
                return JS_NewInt32(context, value.lVal);
            case VT_UI4:
            case VT_UINT:
                return JS_NewUint32(context, value.ulVal);
            case VT_I8:
                return JS_NewInt64(context, value.llVal);
            case VT_UI8:
                return JS_NewFloat64(context, static_cast<double>(value.ullVal));
            case VT_R4:
                return JS_NewFloat64(context, value.fltVal);
            case VT_R8:
            case VT_DATE:
                return JS_NewFloat64(context, value.dblVal);

            case VT_BSTR:
            {
                const std::string text = wideToUtf8(value.bstrVal, static_cast<int>(SysStringLen(value.bstrVal)));
                return JS_NewStringLen(context, text.data(), text.size());
            }

            default:
            {
                VARIANT converted;
                VariantInit(&converted);
                if (SUCCEEDED(VariantChangeType(&converted, &value, 0, VT_BSTR)))
                {
                    const std::string text = wideToUtf8(converted.bstrVal, static_cast<int>(SysStringLen(converted.bstrVal)));
                    JSValue result = JS_NewStringLen(context, text.data(), text.size());
                    VariantClear(&converted);
                    return result;
                }
                VariantClear(&converted);
                return JS_NULL;
            }
            }
        }

        JSValue fillArrayFromRows(JSContext *context, JSValueConst rows, uint32_t rowCount, const std::vector<std::string> &headers, SAFEARRAY *safeArray)
        {
            for (LONG col = 0; col < static_cast<LONG>(headers.size()); ++col)
            {
                VARIANT cell;
                VariantInit(&cell);
                const std::wstring wide = utf8ToWide(headers[static_cast<size_t>(col)]);
                cell.vt = VT_BSTR;
                cell.bstrVal = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
                LONG index[2] = {1, col + 1};
                HRESULT hr = SafeArrayPutElement(safeArray, index, &cell);
                VariantClear(&cell);
                if (FAILED(hr))
                {
                    return throwComError(context, "failed to write Excel header array", hr);
                }
            }

            for (uint32_t rowIndex = 0; rowIndex < rowCount; ++rowIndex)
            {
                JSValue row = JS_GetPropertyUint32(context, rows, rowIndex);
                if (JS_IsException(row))
                {
                    return row;
                }

                for (LONG col = 0; col < static_cast<LONG>(headers.size()); ++col)
                {
                    JSValue value = JS_GetPropertyStr(context, row, headers[static_cast<size_t>(col)].c_str());
                    if (JS_IsException(value))
                    {
                        JS_FreeValue(context, row);
                        return value;
                    }

                    VARIANT cell;
                    if (!jsValueToVariant(context, value, cell))
                    {
                        JS_FreeValue(context, value);
                        JS_FreeValue(context, row);
                        return JS_EXCEPTION;
                    }
                    JS_FreeValue(context, value);

                    LONG index[2] = {static_cast<LONG>(rowIndex) + 2, col + 1};
                    HRESULT hr = SafeArrayPutElement(safeArray, index, &cell);
                    VariantClear(&cell);
                    if (FAILED(hr))
                    {
                        JS_FreeValue(context, row);
                        return throwComError(context, "failed to write Excel data array", hr);
                    }
                }

                JS_FreeValue(context, row);
            }

            return JS_UNDEFINED;
        }

        JSValue jsExcelWriteTable(JSContext *context, JSValueConst, int argc, JSValueConst *argv)
        {
            if (argc < 1 || !JS_IsArray(argv[0]))
            {
                return JS_ThrowTypeError(context, "excel.writeTable expects an array of objects");
            }

            const uint32_t rowCount = arrayLength(context, argv[0]);
            if (rowCount == 0)
            {
                return JS_ThrowTypeError(context, "excel.writeTable needs at least one row");
            }

            std::vector<std::string> headers;
            JSValue headerError = argc >= 2 && !JS_IsUndefined(argv[1])
                                      ? collectProvidedHeaders(context, argv[1], headers)
                                      : collectHeaders(context, argv[0], rowCount, headers);
            if (JS_IsException(headerError))
            {
                return headerError;
            }
            JS_FreeValue(context, headerError);

            if (headers.empty())
            {
                return JS_ThrowTypeError(context, "excel.writeTable rows must have enumerable properties");
            }

            ComApartment apartment;
            if (FAILED(apartment.status()))
            {
                return throwComError(context, "failed to initialize COM", apartment.status());
            }

            ComPtr<IDispatch> excel;
            HRESULT hr = getActiveExcel(excel);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to connect to a running Excel.Application instance", hr);
            }

            ComPtr<IDispatch> selection;
            hr = getDispatchProperty(excel.get(), L"Selection", std::vector<VARIANT>(), selection);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to read Excel active selection", hr);
            }

            VARIANT one = makeIntVariant(1);
            std::vector<VARIANT> cellArgs;
            cellArgs.push_back(one);
            cellArgs.push_back(one);
            ComPtr<IDispatch> anchor;
            hr = getDispatchProperty(selection.get(), L"Cells", cellArgs, anchor);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to get the first cell of Excel selection", hr);
            }

            VARIANT totalRows = makeIntVariant(static_cast<int>(rowCount + 1));
            VARIANT totalColumns = makeIntVariant(static_cast<int>(headers.size()));
            std::vector<VARIANT> resizeArgs;
            resizeArgs.push_back(totalRows);
            resizeArgs.push_back(totalColumns);
            ComPtr<IDispatch> targetRange;
            hr = getDispatchProperty(anchor.get(), L"Resize", resizeArgs, targetRange);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to resize Excel target range", hr);
            }

            SAFEARRAYBOUND bounds[2];
            bounds[0].lLbound = 1;
            bounds[0].cElements = rowCount + 1;
            bounds[1].lLbound = 1;
            bounds[1].cElements = static_cast<ULONG>(headers.size());

            SAFEARRAY *safeArray = SafeArrayCreate(VT_VARIANT, 2, bounds);
            if (safeArray == nullptr)
            {
                return JS_ThrowInternalError(context, "failed to allocate Excel data array");
            }

            JSValue fillError = fillArrayFromRows(context, argv[0], rowCount, headers, safeArray);
            if (JS_IsException(fillError))
            {
                SafeArrayDestroy(safeArray);
                return fillError;
            }
            JS_FreeValue(context, fillError);

            VARIANT values;
            VariantInit(&values);
            values.vt = VT_ARRAY | VT_VARIANT;
            values.parray = safeArray;
            hr = setDispatchProperty(targetRange.get(), L"Value2", values);
            VariantClear(&values);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to write values to Excel selection", hr);
            }

            ComPtr<IDispatch> worksheet;
            hr = getDispatchProperty(targetRange.get(), L"Worksheet", std::vector<VARIANT>(), worksheet);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to read Excel worksheet", hr);
            }

            ComPtr<IDispatch> listObjects;
            hr = getDispatchProperty(worksheet.get(), L"ListObjects", std::vector<VARIANT>(), listObjects);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to read Excel ListObjects collection", hr);
            }

            VARIANT sourceType = makeIntVariant(xlSrcRange);
            VARIANT sourceRange = makeDispatchVariant(targetRange.get());
            VARIANT linkSource = makeMissingVariant();
            VARIANT hasHeaders = makeIntVariant(xlYes);
            std::vector<VARIANT> tableArgs;
            tableArgs.push_back(sourceType);
            tableArgs.push_back(sourceRange);
            tableArgs.push_back(linkSource);
            tableArgs.push_back(hasHeaders);

            VARIANT table;
            VariantInit(&table);
            hr = callDispatchMethod(listObjects.get(), L"Add", tableArgs, &table);
            VariantClear(&table);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to create Excel table from selection", hr);
            }

            JSValue result = JS_NewObject(context);
            JS_SetPropertyStr(context, result, "rows", JS_NewUint32(context, rowCount));
            JS_SetPropertyStr(context, result, "columns", JS_NewUint32(context, static_cast<uint32_t>(headers.size())));
            return result;
        }

        HRESULT getRangeSize(IDispatch *range, LONG &rows, LONG &columns)
        {
            ComPtr<IDispatch> rowsDispatch;
            HRESULT hr = getDispatchProperty(range, L"Rows", std::vector<VARIANT>(), rowsDispatch);
            if (FAILED(hr))
            {
                return hr;
            }

            hr = getCountProperty(rowsDispatch.get(), rows);
            if (FAILED(hr))
            {
                return hr;
            }

            ComPtr<IDispatch> columnsDispatch;
            hr = getDispatchProperty(range, L"Columns", std::vector<VARIANT>(), columnsDispatch);
            if (FAILED(hr))
            {
                return hr;
            }

            return getCountProperty(columnsDispatch.get(), columns);
        }

        HRESULT getRangeCellValue(VARIANT &rangeValue, LONG row, LONG column, VARIANT &cellValue)
        {
            VariantInit(&cellValue);
            if ((rangeValue.vt & VT_ARRAY) != 0 && rangeValue.parray != nullptr)
            {
                LONG index[2] = {row, column};
                return SafeArrayGetElement(rangeValue.parray, index, &cellValue);
            }

            if (row == 1 && column == 1)
            {
                return VariantCopy(&cellValue, &rangeValue);
            }

            cellValue.vt = VT_EMPTY;
            return S_OK;
        }

        JSValue jsExcelReadTable(JSContext *context, JSValueConst, int, JSValueConst *)
        {
            ComApartment apartment;
            if (FAILED(apartment.status()))
            {
                return throwComError(context, "failed to initialize COM", apartment.status());
            }

            ComPtr<IDispatch> excel;
            HRESULT hr = getActiveExcel(excel);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to connect to a running Excel.Application instance", hr);
            }

            ComPtr<IDispatch> selection;
            hr = getDispatchProperty(excel.get(), L"Selection", std::vector<VARIANT>(), selection);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to read Excel active selection", hr);
            }

            LONG rowCount = 0;
            LONG columnCount = 0;
            hr = getRangeSize(selection.get(), rowCount, columnCount);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to inspect Excel selection size", hr);
            }

            ComPtr<IDispatch> targetRange;
            if (rowCount == 1 && columnCount == 1)
            {
                hr = getDispatchProperty(selection.get(), L"CurrentRegion", std::vector<VARIANT>(), targetRange);
                if (FAILED(hr))
                {
                    targetRange.reset(selection.get());
                    selection.get()->AddRef();
                }
                else
                {
                    hr = getRangeSize(targetRange.get(), rowCount, columnCount);
                    if (FAILED(hr))
                    {
                        return throwComError(context, "failed to inspect Excel current region size", hr);
                    }
                }
            }
            else
            {
                targetRange.reset(selection.get());
                selection.get()->AddRef();
            }

            if (rowCount < 1 || columnCount < 1)
            {
                return JS_NewArray(context);
            }

            VARIANT rangeValue;
            VariantInit(&rangeValue);
            hr = getVariantProperty(targetRange.get(), L"Value2", std::vector<VARIANT>(), rangeValue);
            if (FAILED(hr))
            {
                return throwComError(context, "failed to read Excel selection values", hr);
            }

            std::vector<std::string> headers;
            headers.reserve(static_cast<size_t>(columnCount));
            for (LONG col = 1; col <= columnCount; ++col)
            {
                VARIANT headerValue;
                hr = getRangeCellValue(rangeValue, 1, col, headerValue);
                if (FAILED(hr))
                {
                    VariantClear(&rangeValue);
                    return throwComError(context, "failed to read Excel header cell", hr);
                }

                headers.push_back(variantToString(headerValue));
                VariantClear(&headerValue);
            }

            JSValue rows = JS_NewArray(context);
            uint32_t outputIndex = 0;
            for (LONG row = 2; row <= rowCount; ++row)
            {
                JSValue object = JS_NewObject(context);
                for (LONG col = 1; col <= columnCount; ++col)
                {
                    VARIANT cellValue;
                    hr = getRangeCellValue(rangeValue, row, col, cellValue);
                    if (FAILED(hr))
                    {
                        JS_FreeValue(context, object);
                        JS_FreeValue(context, rows);
                        VariantClear(&rangeValue);
                        return throwComError(context, "failed to read Excel data cell", hr);
                    }

                    JSValue value = variantToJsValue(context, cellValue);
                    VariantClear(&cellValue);
                    if (JS_IsException(value))
                    {
                        JS_FreeValue(context, object);
                        JS_FreeValue(context, rows);
                        VariantClear(&rangeValue);
                        return value;
                    }

                    JS_SetPropertyStr(context, object, headers[static_cast<size_t>(col - 1)].c_str(), value);
                }

                JS_SetPropertyUint32(context, rows, outputIndex++, object);
            }

            VariantClear(&rangeValue);
            return rows;
        }
    }

    JSValue createExcelModule(JSContext *context)
    {
        JSValue excel = JS_NewObject(context);
        JS_SetPropertyStr(context, excel, "writeTable", JS_NewCFunction(context, jsExcelWriteTable, "writeTable", 2));
        JS_SetPropertyStr(context, excel, "readTable", JS_NewCFunction(context, jsExcelReadTable, "readTable", 0));
        return excel;
    }
}
