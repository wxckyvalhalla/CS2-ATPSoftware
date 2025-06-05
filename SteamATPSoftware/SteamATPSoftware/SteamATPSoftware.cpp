#include <windows.h>
#include <shlobj.h>
#include <tchar.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <tlhelp32.h>
#include <algorithm>

using namespace std;

wstring GetSteamPath()
{
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILESX86, NULL, 0, path)))
    {
        wstring steamPath = wstring(path) + L"\\Steam";
        if (GetFileAttributesW(steamPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            return steamPath;
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t steamPath[MAX_PATH];
        DWORD bufSize = sizeof(steamPath);
        if (RegQueryValueExW(hKey, L"SteamPath", NULL, NULL, (LPBYTE)steamPath, &bufSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            if (GetFileAttributesW(steamPath) != INVALID_FILE_ATTRIBUTES)
                return steamPath;
        }
        RegCloseKey(hKey);
    }

    return L"";
}

wstring FindSteamConfigFile(const wstring& steamPath)
{
    wstring configPath = steamPath + L"\\config\\config.vdf";
    if (GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return configPath;

    wstring userDataPath = steamPath + L"\\userdata";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW((userDataPath + L"\\*").c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && findData.cFileName[0] != '.')
            {
                wstring localConfig = userDataPath + L"\\" + findData.cFileName + L"\\config\\localconfig.vdf";
                if (GetFileAttributesW(localConfig.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    FindClose(hFind);
                    return localConfig;
                }
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }

    wstring shortcutsPath = steamPath + L"\\config\\shortcuts.vdf";
    if (GetFileAttributesW(shortcutsPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return shortcutsPath;

    return L"";
}

bool ModifyLaunchOptions(const wstring& configPath, const string& appId, const string& parameter)
{
    ifstream file(configPath, ios::binary);
    if (!file.is_open())
        return false;

    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    file.close();

    bool parameterExists = content.find(" " + parameter + " ") != string::npos ||
        content.find("\"" + parameter + "\"") != string::npos;

    if (parameterExists)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        cout << "Parameter " << parameter << " already exists" << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        return false;
    }

    regex pattern("\"" + appId + "\".*?\"LaunchOptions\"\\s+\"(.*?)\"", regex_constants::icase);
    smatch match;
    if (regex_search(content, match, pattern))
    {
        string currentOptions = match[1].str();
        string newOptions = currentOptions.empty() ? parameter : currentOptions + " " + parameter;

        content.replace(match.position(1), match.length(1), newOptions);

        ofstream outFile(configPath, ios::binary);
        if (!outFile.is_open())
            return false;

        outFile << content;
        outFile.close();
        return true;
    }

    pattern = "\"Software\"[^}]*}";
    if (regex_search(content, match, pattern))
    {
        string newEntry = "\n        \"" + appId + "\"\n        {\n            \"LaunchOptions\"       \"" + parameter + "\"\n        }";

        content.insert(match.position() + match.length(), newEntry);

        ofstream outFile(configPath, ios::binary);
        if (!outFile.is_open())
            return false;

        outFile << content;
        outFile.close();
        return true;
    }

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
    cout << "Failed to modify launch options" << endl;
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    return false;
}

void KillProcessByName(const wstring& processName)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0)
            {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProcess)
                {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
}

void AddParameterViaSteamConsole(const wstring& steamPath, const string& appId, const string& parameter)
{
    try
    {
        KillProcessByName(L"steam.exe");
        Sleep(2000);

        wstring command = L"\"" + steamPath + L"\\steam.exe\" -console -applaunch " + wstring(appId.begin(), appId.end()) + L" " + wstring(parameter.begin(), parameter.end());
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        CreateProcessW(NULL, (LPWSTR)command.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
        cout << "Steam launched in console mode" << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    catch (...)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
        cout << "Steam console error" << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
}

void RestartSteam(const wstring& steamPath)
{
    try
    {
        KillProcessByName(L"steam.exe");
        Sleep(2000);

        wstring command = L"\"" + steamPath + L"\\steam.exe\" -silent";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        CreateProcessW(NULL, (LPWSTR)command.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        Sleep(3000);
    }
    catch (...)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
        cout << "Steam restart error" << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
}

int main()
{
    const string appId = "730";
    const string parameterToAdd = "-allow_third_party_software";

    try
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN);
        cout << "Steam Launch Options Editor" << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        cout << "Target parameter: " << parameterToAdd << endl;

        wstring steamPath = GetSteamPath();
        if (steamPath.empty())
        {
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
            cout << "Steam path not found!" << endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            return 1;
        }

        wstring configPath = FindSteamConfigFile(steamPath);
        if (configPath.empty())
        {
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
            cout << "Steam config file not found!" << endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            return 1;
        }

        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
        wcout << L"Config file found: " << configPath << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

        wstring backupPath = configPath + L".bak";
        CopyFileW(configPath.c_str(), backupPath.c_str(), FALSE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        wcout << L"Backup created: " << backupPath << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

        bool modified = ModifyLaunchOptions(configPath, appId, parameterToAdd);

        if (modified)
        {
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
            cout << "Parameter added successfully. Restarting Steam..." << endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            RestartSteam(steamPath);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
            cout << "Operation completed successfully" << endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
        else
        {
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
            cout << "Trying alternative method via Steam Console..." << endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            AddParameterViaSteamConsole(steamPath, appId, parameterToAdd);
        }
    }
    catch (const exception& ex)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
        cout << "Error: " << ex.what() << endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    cout << "Press any key to exit..." << endl;
    cin.get();
    return 0;
}