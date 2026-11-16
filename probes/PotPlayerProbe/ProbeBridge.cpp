#include <windows.h>
#include <cstdint>
#include <string>

extern "C" __declspec(dllexport) int32_t Probe_CopyUtf8(const uint8_t* data, uint32_t length) {
    if (!data || length > 65536) return -1;
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Probe_CopyUtf8), &module);
    wchar_t file[32768]{};
    GetModuleFileNameW(module, file, 32768);
    std::wstring path(file);
    path = path.substr(0, path.find_last_of(L"\\/")) + L"\\probe-native.tsv";
    HANDLE stream = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stream == INVALID_HANDLE_VALUE) return -2;
    DWORD written{};
    BOOL ok = WriteFile(stream, data, length, &written, nullptr);
    CloseHandle(stream);
    return ok && written == length ? static_cast<int32_t>(length) : -3;
}
