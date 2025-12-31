#pragma once

#include <string>
#include <map>
#include <vector>

class Configuration {
public:
    static Configuration& GetInstance();

    // Load configuration from a file (key=value format)
    // Returns true if successful
    bool Load(const std::string& filepath);

    // Get a string value. Returns defaultValue if key not found.
    std::string GetString(const std::string& key, const std::string& defaultValue = "");

    // Set a value programmatically (useful for defaults)
    void SetString(const std::string& key, const std::string& value);

private:
    Configuration() = default;
    ~Configuration() = default;
    
    // Parse a line: "key=value"
    void ParseLine(const std::string& line);

    std::map<std::string, std::string> m_Settings;
};
