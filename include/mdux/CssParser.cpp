/**
 * @file CssParser.cpp
 * @brief Implementation of CSS parser for window properties
 */

#include "CssParser.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

namespace mdux {

// WindowStyle implementation
void WindowStyle::mergeFrom(const WindowStyle& other) {
    if (other.width) width = other.width;
    if (other.height) height = other.height;
    if (other.minWidth) minWidth = other.minWidth;
    if (other.minHeight) minHeight = other.minHeight;
    if (other.maxWidth) maxWidth = other.maxWidth;
    if (other.maxHeight) maxHeight = other.maxHeight;
    if (other.title) title = other.title;
    if (other.resizable) resizable = other.resizable;
    if (other.vsync) vsync = other.vsync;
    if (other.fullscreen) fullscreen = other.fullscreen;
}

bool WindowStyle::hasAnyProperties() const noexcept {
    return width || height || minWidth || minHeight || maxWidth || maxHeight ||
           title || resizable || vsync || fullscreen;
}

std::string WindowStyle::toString() const {
    std::stringstream ss;
    ss << "WindowStyle{";
    bool first = true;
    
    auto addProperty = [&](const std::string& name, const auto& value) {
        if (value) {
            if (!first) ss << ", ";
            ss << name << ": " << *value;
            first = false;
        }
    };
    
    addProperty("width", width);
    addProperty("height", height);
    addProperty("minWidth", minWidth);
    addProperty("minHeight", minHeight);
    addProperty("maxWidth", maxWidth);
    addProperty("maxHeight", maxHeight);
    addProperty("title", title);
    addProperty("resizable", resizable);
    addProperty("vsync", vsync);
    addProperty("fullscreen", fullscreen);
    
    ss << "}";
    return ss.str();
}

// CssParser implementation
WindowStyle CssParser::parseWindowStyle(const std::string& cssContent) {
    return parseBodyStyle(cssContent);
}

WindowStyle CssParser::parseBodyStyle(const std::string& cssContent) {
    try {
        // Remove comments first
        auto cleanCss = removeComments(cssContent);
        
        // Parse body rule
        auto properties = parseRuleBlock(cleanCss, "body");
        
        // Convert to WindowStyle
        return propertiesToWindowStyle(properties);
        
    } catch (const std::exception& e) {
        std::cerr << "CssParser: Error parsing CSS: " << e.what() << std::endl;
        return WindowStyle{};
    }
}

std::string CssParser::removeComments(const std::string& content) {
    std::string result;
    result.reserve(content.size());
    
    bool inComment = false;
    for (size_t i = 0; i < content.size(); ++i) {
        if (!inComment && i + 1 < content.size() && 
            content[i] == '/' && content[i + 1] == '*') {
            inComment = true;
            ++i; // Skip the '*'
        } else if (inComment && i + 1 < content.size() && 
                  content[i] == '*' && content[i + 1] == '/') {
            inComment = false;
            ++i; // Skip the '/'
        } else if (!inComment) {
            result += content[i];
        }
    }
    
    return result;
}

std::unordered_map<std::string, std::string> 
CssParser::parseRuleBlock(const std::string& cssContent, const std::string& selector) {
    // Find selector in CSS content
    auto lowerCss = toLowercase(cssContent);
    auto lowerSelector = toLowercase(selector);
    
    size_t selectorPos = lowerCss.find(lowerSelector);
    if (selectorPos == std::string::npos) {
        return {};
    }
    
    // Find opening brace after selector
    size_t braceStart = cssContent.find('{', selectorPos);
    if (braceStart == std::string::npos) {
        return {};
    }
    
    // Find matching closing brace
    size_t braceEnd = std::string::npos;
    int braceCount = 1;
    for (size_t i = braceStart + 1; i < cssContent.size() && braceCount > 0; ++i) {
        if (cssContent[i] == '{') {
            ++braceCount;
        } else if (cssContent[i] == '}') {
            --braceCount;
            if (braceCount == 0) {
                braceEnd = i;
                break;
            }
        }
    }
    
    if (braceEnd == std::string::npos) {
        return {};
    }
    
    // Extract rule content between braces
    std::string ruleContent = cssContent.substr(braceStart + 1, braceEnd - braceStart - 1);
    
    return parseDeclarations(ruleContent);
}

std::unordered_map<std::string, std::string> 
CssParser::parseDeclarations(const std::string& ruleContent) {
    std::unordered_map<std::string, std::string> properties;
    
    // Split by semicolons
    std::stringstream ss(ruleContent);
    std::string declaration;
    
    while (std::getline(ss, declaration, ';')) {
        declaration = trim(declaration);
        if (declaration.empty()) continue;
        
        // Find colon separator
        size_t colonPos = declaration.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string property = trim(declaration.substr(0, colonPos));
        std::string value = trim(declaration.substr(colonPos + 1));
        
        if (!property.empty() && !value.empty()) {
            properties[toLowercase(property)] = value;
        }
    }
    
    return properties;
}

WindowStyle CssParser::propertiesToWindowStyle(
    const std::unordered_map<std::string, std::string>& properties) {
    
    WindowStyle style;
    
    for (const auto& [property, value] : properties) {
        if (property == "width") {
            style.width = parsePixelValue(value);
        } else if (property == "height") {
            style.height = parsePixelValue(value);
        } else if (property == "min-width") {
            style.minWidth = parsePixelValue(value);
        } else if (property == "min-height") {
            style.minHeight = parsePixelValue(value);
        } else if (property == "max-width") {
            style.maxWidth = parsePixelValue(value);
        } else if (property == "max-height") {
            style.maxHeight = parsePixelValue(value);
        } else if (property == "title") {
            style.title = parseStringValue(value);
        } else if (property == "resizable") {
            style.resizable = parseBooleanValue(value);
        } else if (property == "vsync") {
            style.vsync = parseBooleanValue(value);
        } else if (property == "fullscreen") {
            style.fullscreen = parseBooleanValue(value);
        }
    }
    
    return style;
}

std::optional<std::uint32_t> CssParser::parsePixelValue(const std::string& value) {
    std::string trimmedValue = trim(value);
    
    // Remove 'px' suffix if present
    if (trimmedValue.size() >= 2 && 
        toLowercase(trimmedValue.substr(trimmedValue.size() - 2)) == "px") {
        trimmedValue = trimmedValue.substr(0, trimmedValue.size() - 2);
        trimmedValue = trim(trimmedValue);
    }
    
    // Parse as integer
    try {
        int intValue = std::stoi(trimmedValue);
        if (intValue >= 0) {
            return static_cast<std::uint32_t>(intValue);
        }
    } catch (const std::exception&) {
        // Parsing failed
    }
    
    return std::nullopt;
}

std::optional<bool> CssParser::parseBooleanValue(const std::string& value) {
    std::string lowerValue = toLowercase(trim(value));
    
    if (lowerValue == "true" || lowerValue == "1" || lowerValue == "yes") {
        return true;
    } else if (lowerValue == "false" || lowerValue == "0" || lowerValue == "no") {
        return false;
    }
    
    return std::nullopt;
}

std::string CssParser::parseStringValue(const std::string& value) {
    std::string trimmedValue = trim(value);
    
    // Remove quotes if present
    if (trimmedValue.size() >= 2) {
        char firstChar = trimmedValue[0];
        char lastChar = trimmedValue[trimmedValue.size() - 1];
        
        if ((firstChar == '"' && lastChar == '"') || 
            (firstChar == '\'' && lastChar == '\'')) {
            return trimmedValue.substr(1, trimmedValue.size() - 2);
        }
    }
    
    return trimmedValue;
}

std::string CssParser::trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string CssParser::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// HtmlParser implementation
std::string HtmlParser::extractEmbeddedCss(const std::string& htmlContent) {
    auto styleBlocks = findTagContent(htmlContent, "style");
    
    std::string combinedCss;
    for (const auto& block : styleBlocks) {
        combinedCss += block + "\n";
    }
    
    return combinedCss;
}

WindowStyle HtmlParser::parseWindowStyleFromHtml(const std::string& htmlContent) {
    auto css = extractEmbeddedCss(htmlContent);
    return CssParser::parseWindowStyle(css);
}

std::vector<std::string> HtmlParser::findTagContent(
    const std::string& content, const std::string& tagName) {
    
    std::vector<std::string> results;
    std::string lowerContent = toLowercase(content);
    std::string lowerTagName = toLowercase(tagName);
    
    std::string openTag = "<" + lowerTagName;
    std::string closeTag = "</" + lowerTagName + ">";
    
    size_t searchPos = 0;
    while (true) {
        // Find opening tag
        size_t openPos = lowerContent.find(openTag, searchPos);
        if (openPos == std::string::npos) break;
        
        // Find end of opening tag (handle attributes)
        size_t openEnd = lowerContent.find('>', openPos);
        if (openEnd == std::string::npos) break;
        
        // Find closing tag
        size_t closePos = lowerContent.find(closeTag, openEnd);
        if (closePos == std::string::npos) break;
        
        // Extract content between tags
        size_t contentStart = openEnd + 1;
        size_t contentLength = closePos - contentStart;
        
        if (contentLength > 0) {
            std::string tagContent = content.substr(contentStart, contentLength);
            results.push_back(tagContent);
        }
        
        // Continue searching after this tag
        searchPos = closePos + closeTag.length();
    }
    
    return results;
}

std::string HtmlParser::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace mdux