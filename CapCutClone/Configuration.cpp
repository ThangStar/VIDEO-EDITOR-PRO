#include "Configuration.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

Configuration& Configuration::GetInstance() {
    static Configuration instance;
    return instance;
}

bool Configuration::Load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Configuration] Failed to open config file: " << filepath << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        ParseLine(line);
    }

    file.close();
    std::cout << "[Configuration] Loaded config from: " << filepath << std::endl;
    return true;
}

std::string Configuration::GetString(const std::string& key, const std::string& defaultValue) {
    auto it = m_Settings.find(key);
    if (it != m_Settings.end()) {
        return it->second;
    }
    return defaultValue;
}

void Configuration::SetString(const std::string& key, const std::string& value) {
    m_Settings[key] = value;
}

void Configuration::ParseLine(const std::string& line) {
    if (line.empty() || line[0] == ';' || line[0] == '#') return; // Skip comments/empty

    size_t delimiterPos = line.find('=');
    if (delimiterPos != std::string::npos) {
        std::string key = line.substr(0, delimiterPos);
        std::string value = line.substr(delimiterPos + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), s.end());
        };

        trim(key);
        trim(value);

        if (!key.empty()) {
            m_Settings[key] = value;
        }
    }
}
