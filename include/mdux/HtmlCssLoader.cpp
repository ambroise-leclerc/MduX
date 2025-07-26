/**
 * @file HtmlCssLoader.cpp
 * @brief Implementation of HTML/CSS loader with hot-reload functionality
 */

#include "HtmlCssLoader.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace mdux {

HtmlCssLoader::~HtmlCssLoader() {
    stopWatching();
}

HtmlCssLoader::HtmlCssLoader(HtmlCssLoader&& other) noexcept
    : fileWatcher(std::move(other.fileWatcher))
    , reloadCallback(std::move(other.reloadCallback))
    , lastWindowStyle(std::move(other.lastWindowStyle))
    , lastUiContent(std::move(other.lastUiContent))
    , currentFile(std::move(other.currentFile))
    , debounceDelay(other.debounceDelay)
    , lastChangeTime(other.lastChangeTime)
    , debounceThread(std::move(other.debounceThread))
    , pendingChange(other.pendingChange.load())
    , shouldStopDebounce(other.shouldStopDebounce.load())
{
    // Reset other's state
    other.pendingChange = false;
    other.shouldStopDebounce = false;
}

HtmlCssLoader& HtmlCssLoader::operator=(HtmlCssLoader&& other) noexcept {
    if (this != &other) {
        // Clean up current state
        stopWatching();
        
        // Move data
        fileWatcher = std::move(other.fileWatcher);
        reloadCallback = std::move(other.reloadCallback);
        lastWindowStyle = std::move(other.lastWindowStyle);
        lastUiContent = std::move(other.lastUiContent);
        currentFile = std::move(other.currentFile);
        debounceDelay = other.debounceDelay;
        lastChangeTime = other.lastChangeTime;
        debounceThread = std::move(other.debounceThread);
        pendingChange = other.pendingChange.load();
        shouldStopDebounce = other.shouldStopDebounce.load();
        
        // Reset other's state
        other.pendingChange = false;
        other.shouldStopDebounce = false;
    }
    return *this;
}

ReloadEvent HtmlCssLoader::loadFile(const std::filesystem::path& filePath) {
    if (!std::filesystem::exists(filePath)) {
        return ReloadEvent{
            filePath,
            WindowStyle{},
            UiContent{},
            "File does not exist: " + filePath.string(),
            false,
            false,
            false
        };
    }
    
    auto content = readFileContent(filePath);
    if (content.empty()) {
        return ReloadEvent{
            filePath,
            WindowStyle{},
            UiContent{},
            "Failed to read file or file is empty: " + filePath.string(),
            false,
            false,
            false
        };
    }
    
    return parseContent(content, filePath);
}

bool HtmlCssLoader::startWatching(const std::filesystem::path& filePath, ReloadCallback callback) {
    if (!std::filesystem::exists(filePath)) {
        std::cerr << "HtmlCssLoader: File does not exist: " << filePath << std::endl;
        return false;
    }
    
    // Stop any existing watching
    stopWatching();
    
    // Store callback and file path
    reloadCallback = std::move(callback);
    currentFile = filePath;
    
    // Load initial content
    auto initialEvent = loadFile(filePath);
    if (initialEvent.isSuccess()) {
        lastWindowStyle = initialEvent.windowStyle;
        lastUiContent = initialEvent.uiContent;
    }
    
    // Trigger initial callback
    if (reloadCallback) {
        reloadCallback(initialEvent);
    }
    
    // Create and start file watcher
    fileWatcher = std::make_unique<FileWatcher>();
    return fileWatcher->startWatching(filePath, [this]() { onFileChanged(); });
}

void HtmlCssLoader::stopWatching() {
    if (fileWatcher) {
        fileWatcher->stopWatching();
        fileWatcher.reset();
    }
    
    cleanupDebounceThread();
    
    reloadCallback = nullptr;
    currentFile.clear();
}

bool HtmlCssLoader::isWatching() const noexcept {
    return fileWatcher && fileWatcher->isWatching();
}

std::filesystem::path HtmlCssLoader::getWatchedFile() const noexcept {
    if (fileWatcher) {
        return fileWatcher->getWatchedFile();
    }
    return {};
}

void HtmlCssLoader::onFileChanged() {
    // Record the change time for debouncing
    lastChangeTime = std::chrono::steady_clock::now();
    pendingChange = true;
    
    // If no debounce thread is running, start one
    if (!debounceThread.joinable()) {
        shouldStopDebounce = false;
        debounceThread = std::thread([this]() {
            while (!shouldStopDebounce.load()) {
                if (pendingChange.load()) {
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceChange = now - lastChangeTime;
                    
                    if (timeSinceChange >= debounceDelay) {
                        // Process the change
                        pendingChange = false;
                        processFileReload();
                    }
                }
                
                // Check every 10ms
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
}

void HtmlCssLoader::processFileReload() {
    if (currentFile.empty() || !reloadCallback) {
        return;
    }
    
    auto reloadEvent = loadFile(currentFile);
    
    // Update last known good content if successful
    if (reloadEvent.isSuccess()) {
        lastWindowStyle = reloadEvent.windowStyle;
        lastUiContent = reloadEvent.uiContent;
        std::cout << "HtmlCssLoader: Successfully reloaded " << currentFile.filename() << std::endl;
    } else {
        std::cerr << "HtmlCssLoader: Failed to reload " << currentFile.filename() 
                  << ": " << reloadEvent.errorMessage << std::endl;
    }
    
    // Always call callback to report status
    reloadCallback(reloadEvent);
}

std::string HtmlCssLoader::readFileContent(const std::filesystem::path& filePath) {
    try {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        auto fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read entire file
        std::string content;
        content.resize(static_cast<size_t>(fileSize));
        file.read(content.data(), fileSize);
        
        return content;
        
    } catch (const std::exception& e) {
        std::cerr << "HtmlCssLoader: Exception reading file " << filePath 
                  << ": " << e.what() << std::endl;
        return "";
    }
}

ReloadEvent HtmlCssLoader::parseContent(const std::string& content, 
                                      const std::filesystem::path& filePath) {
    try {
        WindowStyle newWindowStyle;
        UiContent newUiContent;
        FileType fileType = determineFileType(filePath);
        
        // Parse window configuration
        switch (fileType) {
            case FileType::HTML:
                newWindowStyle = HtmlParser::parseWindowStyleFromHtml(content);
                break;
                
            case FileType::CSS:
                newWindowStyle = CssParser::parseWindowStyle(content);
                break;
                
            default:
                return ReloadEvent{
                    filePath,
                    WindowStyle{},
                    UiContent{},
                    "Unsupported file type: " + filePath.extension().string(),
                    false,
                    false,
                    false
                };
        }
        
        // Parse UI content
        newUiContent = parseUiContent(content, filePath);
        
        // Detect what changed
        bool windowConfigChanged = hasWindowConfigChanged(lastWindowStyle, newWindowStyle);
        bool uiContentChanged = hasUiContentChanged(lastUiContent, newUiContent);
        
        // Validate the parsed style
        auto warnings = validateWindowStyle(newWindowStyle);
        std::string warningMessage;
        if (!warnings.empty()) {
            std::stringstream ss;
            ss << "Validation warnings: ";
            for (size_t i = 0; i < warnings.size(); ++i) {
                if (i > 0) ss << "; ";
                ss << warnings[i];
            }
            warningMessage = ss.str();
            std::cout << "HtmlCssLoader: " << warningMessage << std::endl;
        }
        
        return ReloadEvent{
            filePath,
            newWindowStyle,
            newUiContent,
            warningMessage,
            true,
            windowConfigChanged,
            uiContentChanged
        };
        
    } catch (const std::exception& e) {
        return ReloadEvent{
            filePath,
            WindowStyle{},
            UiContent{},
            "Parsing error: " + std::string(e.what()),
            false,
            false,
            false
        };
    }
}

HtmlCssLoader::FileType HtmlCssLoader::determineFileType(const std::filesystem::path& filePath) {
    auto extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), 
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    if (extension == ".html" || extension == ".htm") {
        return FileType::HTML;
    } else if (extension == ".css") {
        return FileType::CSS;
    }
    
    return FileType::UNKNOWN;
}

void HtmlCssLoader::cleanupDebounceThread() {
    if (debounceThread.joinable()) {
        shouldStopDebounce = true;
        debounceThread.join();
    }
    pendingChange = false;
}

// Utility functions
WindowStyle loadWindowStyleFromFile(const std::filesystem::path& filePath) {
    HtmlCssLoader loader;
    auto event = loader.loadFile(filePath);
    
    if (event.isSuccess()) {
        return event.windowStyle;
    } else {
        std::cerr << "Failed to load window style from " << filePath 
                  << ": " << event.errorMessage << std::endl;
        return WindowStyle{};
    }
}

std::vector<std::string> validateWindowStyle(const WindowStyle& style) {
    std::vector<std::string> warnings;
    
    // Check for zero or negative dimensions
    if (style.width && *style.width == 0) {
        warnings.push_back("Width is zero");
    }
    if (style.height && *style.height == 0) {
        warnings.push_back("Height is zero");
    }
    
    // Check min/max constraints
    if (style.width && style.minWidth && *style.width < *style.minWidth) {
        warnings.push_back("Width is less than minimum width");
    }
    if (style.width && style.maxWidth && *style.width > *style.maxWidth) {
        warnings.push_back("Width is greater than maximum width");
    }
    if (style.height && style.minHeight && *style.height < *style.minHeight) {
        warnings.push_back("Height is less than minimum height");
    }
    if (style.height && style.maxHeight && *style.height > *style.maxHeight) {
        warnings.push_back("Height is greater than maximum height");
    }
    
    // Check min/max consistency
    if (style.minWidth && style.maxWidth && *style.minWidth > *style.maxWidth) {
        warnings.push_back("Minimum width is greater than maximum width");
    }
    if (style.minHeight && style.maxHeight && *style.minHeight > *style.maxHeight) {
        warnings.push_back("Minimum height is greater than maximum height");
    }
    
    // Check reasonable size limits for medical device displays
    const uint32_t MAX_REASONABLE_SIZE = 8192;
    if (style.width && *style.width > MAX_REASONABLE_SIZE) {
        warnings.push_back("Width is unusually large");
    }
    if (style.height && *style.height > MAX_REASONABLE_SIZE) {
        warnings.push_back("Height is unusually large");
    }
    
    // Check for empty title
    if (style.title && style.title->empty()) {
        warnings.push_back("Title is empty");
    }
    
    return warnings;
}

// New methods implementation

std::string UiContent::getCombinedContent() const {
    if (htmlContent.empty()) {
        return "";
    }
    
    // If HTML already contains CSS, return as-is
    if (htmlContent.find("<style>") != std::string::npos || cssContent.empty()) {
        return htmlContent;
    }
    
    // Inject CSS into HTML head
    std::string combined = htmlContent;
    size_t headPos = combined.find("<head>");
    if (headPos != std::string::npos) {
        size_t insertPos = headPos + 6; // After <head>
        combined.insert(insertPos, "\n    <style>\n" + cssContent + "\n    </style>");
    }
    
    return combined;
}

UiContent HtmlCssLoader::parseUiContent(const std::string& content, 
                                      const std::filesystem::path& filePath) {
    UiContent uiContent;
    FileType fileType = determineFileType(filePath);
    
    try {
        switch (fileType) {
            case FileType::HTML: {
                uiContent.htmlContent = content;
                
                // Extract embedded CSS from <style> tags
                size_t styleStart = content.find("<style>");
                size_t styleEnd = content.find("</style>");
                if (styleStart != std::string::npos && styleEnd != std::string::npos) {
                    styleStart += 7; // Skip "<style>"
                    uiContent.cssContent = content.substr(styleStart, styleEnd - styleStart);
                }
                
                // Extract title
                size_t titleStart = content.find("<title>");
                size_t titleEnd = content.find("</title>");
                if (titleStart != std::string::npos && titleEnd != std::string::npos) {
                    titleStart += 7; // Skip "<title>"
                    uiContent.title = content.substr(titleStart, titleEnd - titleStart);
                }
                break;
            }
            
            case FileType::CSS:
                uiContent.cssContent = content;
                uiContent.title = filePath.stem().string() + " Styles";
                // For pure CSS files, create a minimal HTML wrapper
                uiContent.htmlContent = "<!DOCTYPE html><html><head><style>" + content + "</style></head><body><p>CSS Preview</p></body></html>";
                break;
                
            default:
                uiContent.errors.push_back("Unsupported file type for UI content");
                break;
        }
        
    } catch (const std::exception& e) {
        uiContent.errors.push_back("Error parsing UI content: " + std::string(e.what()));
    }
    
    return uiContent;
}

bool HtmlCssLoader::hasWindowConfigChanged(const WindowStyle& oldStyle, 
                                         const WindowStyle& newStyle) {
    return oldStyle.width != newStyle.width ||
           oldStyle.height != newStyle.height ||
           oldStyle.title != newStyle.title ||
           oldStyle.resizable != newStyle.resizable ||
           oldStyle.vsync != newStyle.vsync ||
           oldStyle.fullscreen != newStyle.fullscreen ||
           oldStyle.minWidth != newStyle.minWidth ||
           oldStyle.maxWidth != newStyle.maxWidth ||
           oldStyle.minHeight != newStyle.minHeight ||
           oldStyle.maxHeight != newStyle.maxHeight;
}

bool HtmlCssLoader::hasUiContentChanged(const UiContent& oldContent, 
                                       const UiContent& newContent) {
    return oldContent.htmlContent != newContent.htmlContent ||
           oldContent.cssContent != newContent.cssContent ||
           oldContent.title != newContent.title;
}

UiContent HtmlCssLoader::loadUiContent(const std::filesystem::path& filePath) {
    if (!std::filesystem::exists(filePath)) {
        UiContent content;
        content.errors.push_back("File does not exist: " + filePath.string());
        return content;
    }
    
    auto fileContent = readFileContent(filePath);
    if (fileContent.empty()) {
        UiContent content;
        content.errors.push_back("Failed to read file or file is empty");
        return content;
    }
    
    return parseUiContent(fileContent, filePath);
}

bool HtmlCssLoader::reloadUiContent() {
    if (currentFile.empty()) {
        return false;
    }
    
    try {
        auto newUiContent = loadUiContent(currentFile);
        if (newUiContent.isValid()) {
            lastUiContent = newUiContent;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "HtmlCssLoader: Failed to reload UI content: " << e.what() << std::endl;
    }
    
    return false;
}

} // namespace mdux