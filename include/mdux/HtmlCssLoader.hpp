/**
 * @file HtmlCssLoader.hpp
 * @brief HTML/CSS loader with hot-reload functionality
 * 
 * This file provides the main interface for loading HTML/CSS files and
 * automatically reloading them when changes are detected. It combines
 * the FileWatcher and CSS parsing functionality to provide a seamless
 * development experience.
 */

#pragma once

#include "FileWatcher.hpp"
#include "CssParser.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace mdux {

/**
 * @brief UI content data for rendering
 */
struct UiContent {
    std::string htmlContent;           ///< Full HTML content for UI rendering
    std::string cssContent;            ///< CSS styles for UI rendering
    std::string title;                 ///< Page title for display
    std::vector<std::string> errors;   ///< Parsing or validation errors
    
    /**
     * @brief Check if content is valid for rendering
     * @return true if content can be rendered
     */
    bool isValid() const noexcept { return !htmlContent.empty() && errors.empty(); }
    
    /**
     * @brief Get combined content for rendering
     * @return Complete HTML with embedded CSS
     */
    std::string getCombinedContent() const;
};

/**
 * @brief Event data for HTML/CSS reload callbacks
 */
struct ReloadEvent {
    std::filesystem::path filePath;    ///< Path to the file that changed
    WindowStyle windowStyle;           ///< Parsed window style from the file
    UiContent uiContent;               ///< UI content for rendering (NEW)
    std::string errorMessage;          ///< Error message if parsing failed
    bool success;                      ///< Whether parsing was successful
    bool windowConfigChanged;          ///< Whether window config changed (NEW)
    bool uiContentChanged;             ///< Whether UI content changed (NEW)
    
    /**
     * @brief Check if the reload was successful
     * @return true if no errors occurred
     */
    bool isSuccess() const noexcept { return success && errorMessage.empty(); }
    
    /**
     * @brief Check if only UI content changed (no window recreation needed)
     * @return true if only UI content changed
     */
    bool isUiOnlyChange() const noexcept { return uiContentChanged && !windowConfigChanged; }
};

/**
 * @brief HTML/CSS loader with automatic hot-reload functionality
 * 
 * This class provides the main interface for development-time HTML/CSS loading.
 * It can load files, parse window properties, and automatically reload when
 * files change. Designed for rapid UI iteration during development.
 */
class HtmlCssLoader {
public:
    using ReloadCallback = std::function<void(const ReloadEvent&)>;
    
    /**
     * @brief Construct a new HtmlCssLoader
     */
    HtmlCssLoader() = default;
    
    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~HtmlCssLoader();
    
    // Non-copyable but movable
    HtmlCssLoader(const HtmlCssLoader&) = delete;
    HtmlCssLoader& operator=(const HtmlCssLoader&) = delete;
    HtmlCssLoader(HtmlCssLoader&& other) noexcept;
    HtmlCssLoader& operator=(HtmlCssLoader&& other) noexcept;
    
    /**
     * @brief Load HTML/CSS file and parse window properties
     * @param filePath Path to HTML or CSS file
     * @return ReloadEvent with parsing results
     */
    ReloadEvent loadFile(const std::filesystem::path& filePath);
    
    /**
     * @brief Start watching a file for changes with automatic reload
     * @param filePath Path to HTML or CSS file to watch
     * @param callback Function to call when file changes
     * @return true if watching started successfully
     */
    bool startWatching(const std::filesystem::path& filePath, ReloadCallback callback);
    
    /**
     * @brief Stop watching the current file
     */
    void stopWatching();
    
    /**
     * @brief Check if currently watching a file
     * @return true if actively watching
     */
    bool isWatching() const noexcept;
    
    /**
     * @brief Get the path of the currently watched file
     * @return Path to watched file, or empty if not watching
     */
    std::filesystem::path getWatchedFile() const noexcept;
    
    /**
     * @brief Get the last successfully parsed window style
     * @return WindowStyle from the last successful parse
     */
    const WindowStyle& getLastWindowStyle() const noexcept { return lastWindowStyle; }
    
    /**
     * @brief Get the last successfully parsed UI content
     * @return UiContent from the last successful parse
     */
    const UiContent& getLastUiContent() const noexcept { return lastUiContent; }
    
    /**
     * @brief Load only UI content from file (no window config)
     * @param filePath Path to HTML or CSS file
     * @return UiContent with parsed content
     */
    UiContent loadUiContent(const std::filesystem::path& filePath);
    
    /**
     * @brief Reload UI content without triggering window changes
     * @return true if UI content was successfully reloaded
     */
    bool reloadUiContent();
    
    /**
     * @brief Set debounce delay for file change notifications
     * @param delayMs Delay in milliseconds before processing changes
     * 
     * This helps prevent multiple rapid reloads when files are saved
     * by text editors that write temporary files.
     */
    void setDebounceDelay(std::chrono::milliseconds delayMs) { debounceDelay = delayMs; }

private:
    std::unique_ptr<FileWatcher> fileWatcher;
    ReloadCallback reloadCallback;
    WindowStyle lastWindowStyle;
    UiContent lastUiContent;                      // NEW: Track UI content separately
    std::filesystem::path currentFile;
    std::chrono::milliseconds debounceDelay{100}; // 100ms default debounce
    
    // Debouncing support
    std::chrono::steady_clock::time_point lastChangeTime;
    std::thread debounceThread;
    std::atomic<bool> pendingChange{false};
    std::atomic<bool> shouldStopDebounce{false};
    
    /**
     * @brief Internal callback for file changes (with debouncing)
     */
    void onFileChanged();
    
    /**
     * @brief Process the actual file reload after debouncing
     */
    void processFileReload();
    
    /**
     * @brief Read file content from disk
     * @param filePath Path to file to read
     * @return File content as string, or empty string on error
     */
    std::string readFileContent(const std::filesystem::path& filePath);
    
    /**
     * @brief Parse window style from file content
     * @param content File content (HTML or CSS)
     * @param filePath Original file path (for error reporting)
     * @return ReloadEvent with parsing results
     */
    ReloadEvent parseContent(const std::string& content, 
                           const std::filesystem::path& filePath);
    
    /**
     * @brief Parse UI content from file content (NEW)
     * @param content File content (HTML or CSS)
     * @param filePath Original file path (for error reporting)
     * @return UiContent with parsed content
     */
    UiContent parseUiContent(const std::string& content, 
                           const std::filesystem::path& filePath);
    
    /**
     * @brief Compare window styles to detect changes (NEW)
     * @param oldStyle Previous window style
     * @param newStyle New window style
     * @return true if window configuration changed
     */
    bool hasWindowConfigChanged(const WindowStyle& oldStyle, 
                               const WindowStyle& newStyle);
    
    /**
     * @brief Compare UI content to detect changes (NEW)
     * @param oldContent Previous UI content
     * @param newContent New UI content
     * @return true if UI content changed
     */
    bool hasUiContentChanged(const UiContent& oldContent, 
                           const UiContent& newContent);
    
    /**
     * @brief Determine file type from extension
     * @param filePath Path to examine
     * @return File type classification
     */
    enum class FileType { HTML, CSS, UNKNOWN };
    FileType determineFileType(const std::filesystem::path& filePath);
    
    /**
     * @brief Clean up debounce thread
     */
    void cleanupDebounceThread();
};

/**
 * @brief Utility function to load window style from file
 * @param filePath Path to HTML or CSS file
 * @return WindowStyle parsed from file, or empty style on error
 * 
 * This is a convenience function for one-off file loading without
 * setting up watching or callbacks.
 */
WindowStyle loadWindowStyleFromFile(const std::filesystem::path& filePath);

/**
 * @brief Utility function to validate window style properties
 * @param style WindowStyle to validate
 * @return Vector of validation warning messages
 * 
 * Checks for common issues like negative dimensions, conflicting
 * min/max values, etc.
 */
std::vector<std::string> validateWindowStyle(const WindowStyle& style);

} // namespace mdux