/**
 * @file CssParser.hpp
 * @brief Simple CSS parser for window properties
 * 
 * This file provides a minimal CSS parser specifically designed for extracting
 * window configuration properties from HTML/CSS files. It focuses on the
 * subset of CSS properties relevant to window management.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace mdux {

/**
 * @brief Window styling properties extracted from CSS
 */
struct WindowStyle {
    std::optional<std::uint32_t> width;        ///< Window width in pixels
    std::optional<std::uint32_t> height;       ///< Window height in pixels
    std::optional<std::uint32_t> minWidth;     ///< Minimum window width
    std::optional<std::uint32_t> minHeight;    ///< Minimum window height
    std::optional<std::uint32_t> maxWidth;     ///< Maximum window width
    std::optional<std::uint32_t> maxHeight;    ///< Maximum window height
    std::optional<std::string> title;          ///< Window title
    std::optional<bool> resizable;             ///< Whether window is resizable
    std::optional<bool> vsync;                 ///< Vertical sync enabled
    std::optional<bool> fullscreen;            ///< Fullscreen mode
    
    /**
     * @brief Apply non-empty values from another WindowStyle
     * @param other Style to merge from
     */
    void mergeFrom(const WindowStyle& other);
    
    /**
     * @brief Check if any properties are set
     * @return true if at least one property has a value
     */
    bool hasAnyProperties() const noexcept;
    
    /**
     * @brief Get a string representation for debugging
     * @return String describing the style properties
     */
    std::string toString() const;
};

/**
 * @brief Simple CSS parser for window properties
 * 
 * Parses a subset of CSS focused on window configuration. Supports:
 * - body selector for window properties
 * - Basic property declarations (property: value;)
 * - String values with quotes
 * - Boolean values (true/false)
 * - Pixel values (with 'px' suffix)
 * - Comments (slash-star ... star-slash)
 */
class CssParser {
public:
    /**
     * @brief Parse CSS content and extract window styles
     * @param cssContent CSS content to parse
     * @return WindowStyle with extracted properties
     */
    static WindowStyle parseWindowStyle(const std::string& cssContent);
    
    /**
     * @brief Parse CSS from body selector specifically
     * @param cssContent CSS content to parse
     * @return WindowStyle with properties from body rule
     */
    static WindowStyle parseBodyStyle(const std::string& cssContent);

private:
    /**
     * @brief Remove CSS comments from content
     * @param content CSS content with potential comments
     * @return CSS content with comments removed
     */
    static std::string removeComments(const std::string& content);
    
    /**
     * @brief Find and parse a CSS rule block
     * @param cssContent CSS content to search
     * @param selector Selector to find (e.g., "body")
     * @return Properties map from the rule block
     */
    static std::unordered_map<std::string, std::string> 
        parseRuleBlock(const std::string& cssContent, const std::string& selector);
    
    /**
     * @brief Parse property declarations from a rule block content
     * @param ruleContent Content inside CSS rule braces
     * @return Map of property names to values
     */
    static std::unordered_map<std::string, std::string> 
        parseDeclarations(const std::string& ruleContent);
    
    /**
     * @brief Convert properties map to WindowStyle
     * @param properties Map of CSS property names to values
     * @return WindowStyle with converted properties
     */
    static WindowStyle propertiesToWindowStyle(
        const std::unordered_map<std::string, std::string>& properties);
    
    /**
     * @brief Parse a pixel value (e.g., "800px" -> 800)
     * @param value String value to parse
     * @return Parsed pixel value, or nullopt if invalid
     */
    static std::optional<std::uint32_t> parsePixelValue(const std::string& value);
    
    /**
     * @brief Parse a boolean value (true/false)
     * @param value String value to parse
     * @return Parsed boolean value, or nullopt if invalid
     */
    static std::optional<bool> parseBooleanValue(const std::string& value);
    
    /**
     * @brief Parse a string value (remove quotes if present)
     * @param value String value to parse
     * @return Parsed string value
     */
    static std::string parseStringValue(const std::string& value);
    
    /**
     * @brief Trim whitespace from both ends of a string
     * @param str String to trim
     * @return Trimmed string
     */
    static std::string trim(const std::string& str);
    
    /**
     * @brief Convert string to lowercase
     * @param str String to convert
     * @return Lowercase string
     */
    static std::string toLowercase(const std::string& str);
};

/**
 * @brief HTML parser for extracting embedded CSS
 * 
 * Simple HTML parser that can extract CSS from <style> tags within
 * HTML documents. Designed for development hot-reload scenarios.
 */
class HtmlParser {
public:
    /**
     * @brief Extract CSS content from HTML <style> tags
     * @param htmlContent HTML content to parse
     * @return Combined CSS content from all style tags
     */
    static std::string extractEmbeddedCss(const std::string& htmlContent);
    
    /**
     * @brief Parse window style from HTML document
     * @param htmlContent HTML content containing embedded CSS
     * @return WindowStyle extracted from embedded CSS
     */
    static WindowStyle parseWindowStyleFromHtml(const std::string& htmlContent);

private:
    /**
     * @brief Find content between opening and closing tags
     * @param content HTML content to search
     * @param tagName Tag name to find (e.g., "style")
     * @return Vector of content strings from matching tags
     */
    static std::vector<std::string> findTagContent(
        const std::string& content, const std::string& tagName);
    
    /**
     * @brief Convert string to lowercase for case-insensitive comparison
     * @param str String to convert
     * @return Lowercase string
     */
    static std::string toLowercase(const std::string& str);
};

} // namespace mdux