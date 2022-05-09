#include "pch.h"
#include "hooks.h"
#include "main.h"
#include "logging.h"
#include "keyvalues.h"
#include "masterserver.h"
#include "gameutils.h"
#include "memalloc.h"
#include "maxplayers.h"
#include "configurables.h"
#include "plugin_abi.h"
#include "plugins.h"
#include "version.h"
#include "pch.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"

#include <string.h>
#include <filesystem>
namespace fs = std::filesystem;

typedef void (*initPluginFuncPtr)(void* getPluginObject);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}

void WaitForDebugger(HMODULE baseAddress)
{
	// earlier waitfordebugger call than is in vanilla, just so we can debug stuff a little easier
	if (CommandLine()->CheckParm("-waitfordebugger"))
	{
		spdlog::info("waiting for debugger...");

		while (!IsDebuggerPresent())
			Sleep(100);
	}
}

void freeLibrary(HMODULE hLib)
{
	if (!FreeLibrary(hLib))
	{
		spdlog::error("There was an error while trying to free library");
	}
}

bool LoadPlugins()
{

	std::vector<fs::path> paths;

	std::string pluginPath = GetNorthstarPrefix() + "/plugins";
	if (!fs::exists(pluginPath))
	{
		spdlog::warn("Could not find a plugins directory. Skipped loading plugins");
		return false;
	}
	// ensure dirs exist
	fs::recursive_directory_iterator iterator(pluginPath);
	if (std::filesystem::begin(iterator) == std::filesystem::end(iterator))
	{
		spdlog::warn("Could not find any plugins. Skipped loading plugins");
		return false;
	}
	for (auto const& entry : iterator)
	{
		if (fs::is_regular_file(entry) && entry.path().extension() == ".dll")
			paths.emplace_back(entry.path().filename());
	}
	initGameState();
	for (fs::path path : paths)
	{
		std::string pathstring = (pluginPath / path).string();
		std::wstring wpath = (pluginPath / path).wstring();

		LPCWSTR wpptr = wpath.c_str();
		HMODULE datafile =
			LoadLibraryExW(wpptr, 0, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE); // Load the DLL as a data file
		if (datafile == NULL)
		{
			spdlog::info("Failed to load library {}: ", std::system_category().message(GetLastError()));
			continue;
		}
		HRSRC manifestResource = FindResourceW(datafile, MAKEINTRESOURCE(101), MAKEINTRESOURCE(RT_RCDATA));

		if (manifestResource == NULL)
		{
			spdlog::info("Could not find manifest for library {}", pathstring);
			freeLibrary(datafile);
			continue;
		}
		spdlog::info("Loading resource from library");
		HGLOBAL myResourceData = LoadResource(datafile, manifestResource);
		if (myResourceData == NULL)
		{
			spdlog::error("Failed to load resource from library");
			freeLibrary(datafile);
			continue;
		}
		int manifestSize = SizeofResource(datafile, manifestResource);
		std::string manifest = std::string((const char*)LockResource(myResourceData), 0, manifestSize);
		freeLibrary(datafile);

		rapidjson_document manifestJSON;
		manifestJSON.Parse(manifest.c_str());

		if (manifestJSON.HasParseError())
		{
			spdlog::error("Manifest for {} was invalid", pathstring);
			continue;
		}
		if (!manifestJSON.HasMember("api_version"))
		{
			spdlog::error("{} does not have a version number in its manifest", pathstring);
			continue;
			// spdlog::info(manifestJSON["version"].GetString());
		}
		if (strcmp(manifestJSON["api_version"].GetString(), std::to_string(ABI_VERSION).c_str()))
		{
			spdlog::error("{} has an incompatible API version number in its manifest", pathstring);
			continue;
		}
		// Passed all checks, going to actually load it now

		HMODULE pluginLib = LoadLibraryW(wpptr); // Load the DLL as a data file
		if (pluginLib == NULL)
		{
			spdlog::info("Failed to load library {}: ", std::system_category().message(GetLastError()));
			continue;
		}
		initPluginFuncPtr initPlugin = (initPluginFuncPtr)GetProcAddress(pluginLib, "initializePlugin");
		if (initPlugin == NULL)
		{
			spdlog::info("Library {} has no function initializePlugin", pathstring);
			continue;
		}
		spdlog::info("Succesfully loaded {}", pathstring);
		initPlugin(&getPluginObject);
	}
	return true;
}

bool InitialiseNorthstar()
{
	static bool bInitialised = false;

	if (bInitialised)
	{
		// spdlog::warn("Called InitialiseNorthstar more than once!"); // it's actually 100% fine for that to happen
		return false;
	}

	bInitialised = true;

	parseConfigurables();
	InitialiseVersion();

	// Fix some users' failure to connect to respawn datacenters
	SetEnvironmentVariableA("OPENSSL_ia32cap", "~0x200000200000000");

	curl_global_init_mem(CURL_GLOBAL_DEFAULT, _malloc_base, _free_base, _realloc_base, _strdup_base, _calloc_base);

	InitialiseLogging();
	InstallInitialHooks();
	CreateLogFiles();

	// Write launcher version to log
	spdlog::info("NorthstarLauncher version: {}", version);

	AddDllLoadCallback("tier0.dll", InitialiseTier0GameUtilFunctions);
	AddDllLoadCallback("engine.dll", WaitForDebugger);
	AddDllLoadCallback("engine.dll", InitialiseEngineGameUtilFunctions);
	AddDllLoadCallback("server.dll", InitialiseServerGameUtilFunctions);

	// run callbacks for any libraries that are already loaded by now
	CallAllPendingDLLLoadCallbacks();

	return true;
}