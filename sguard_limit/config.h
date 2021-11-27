#pragma once
#include <Windows.h>
#include <string>


// config load & write module
class ConfigManager {

private:
	static ConfigManager   configManager;

private:
	ConfigManager();
	~ConfigManager()                                   = default;
	ConfigManager(const ConfigManager&)                = delete;
	ConfigManager(ConfigManager&&)                     = delete;
	ConfigManager& operator= (const ConfigManager&)    = delete;
	ConfigManager& operator= (ConfigManager&&)         = delete;

public:
	static ConfigManager&  getInstance();

public:
	void    init(const std::string& profileDir);
	bool    loadConfig(bool driverReady);
	void    writeConfig();

private:
	std::string   profile;
};