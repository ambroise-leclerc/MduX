/**
 * @file FileWatcher.hpp
 * @brief Cross-platform file watching utility for hot-reload
 * 
 * This file provides file system monitoring capabilities for MduX.
 * It enables hot-reload functionality by detecting changes to HTML/CSS
 * files and triggering UI updates.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

#ifdef MDUX_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(MDUX_PLATFORM_LINUX)
    #include <sys/inotify.h>
    #include <unistd.h>
#endif

namespace mdux {

/**
 * @brief Cross-platform file watcher for hot-reload functionality
 * 
 * Monitors a single file for changes and triggers a callback when modifications
 * are detected. Uses platform-specific APIs for efficient file monitoring:
 * - Windows: ReadDirectoryChangesW
 * - Linux: inotify
 * - Fallback: Polling-based monitoring
 */
class FileWatcher {
public:
    using ChangeCallback = std::function<void()>;
    
    /**
     * @brief Construct a new FileWatcher
     */
    FileWatcher() = default;
    
    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~FileWatcher();
    
    // Non-copyable but movable
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    FileWatcher(FileWatcher&& other) noexcept;
    FileWatcher& operator=(FileWatcher&& other) noexcept;
    
    /**
     * @brief Start watching a file for changes
     * @param filePath Path to the file to monitor
     * @param callback Function to call when file changes are detected
     * @return true if watching started successfully, false otherwise
     */
    bool startWatching(const std::filesystem::path& filePath, 
                      ChangeCallback callback);
    
    /**
     * @brief Stop watching the current file
     */
    void stopWatching();
    
    /**
     * @brief Check if currently watching a file
     * @return true if actively watching, false otherwise
     */
    bool isWatching() const noexcept { return isActive.load(); }
    
    /**
     * @brief Get the path of the currently watched file
     * @return Path to watched file, or empty path if not watching
     */
    std::filesystem::path getWatchedFile() const noexcept { return watchedFile; }

private:
    std::filesystem::path watchedFile;
    ChangeCallback changeCallback;
    std::atomic<bool> isActive{false};
    std::atomic<bool> shouldStop{false};
    std::thread watchThread;
    
    // Add fallback detection
    std::atomic<bool> usePollingFallback{false};
    
#ifdef MDUX_PLATFORM_WINDOWS
    HANDLE directoryHandle = INVALID_HANDLE_VALUE;
    HANDLE stopEvent = nullptr;
    
    void windowsWatchLoop();
#elif defined(MDUX_PLATFORM_LINUX)
    int inotifyFd = -1;
    int watchDescriptor = -1;
    
    void linuxWatchLoop();
#endif
    
    /**
     * @brief Fallback polling-based file monitoring
     */
    void pollingWatchLoop();
    
    /**
     * @brief Get last write time of the watched file
     * @return Last write time, or time_point::min() on error
     */
    std::filesystem::file_time_type getLastWriteTime() const;
    
    /**
     * @brief Internal cleanup method
     */
    void cleanup();
};

/**
 * @brief RAII wrapper for automatic file watching
 * 
 * Provides automatic start/stop of file watching with scope-based lifetime
 * management. Useful for temporary file monitoring scenarios.
 */
class ScopedFileWatcher {
public:
    /**
     * @brief Construct and immediately start watching
     * @param filePath Path to file to monitor
     * @param callback Function to call on file changes
     */
    ScopedFileWatcher(const std::filesystem::path& filePath, 
                     FileWatcher::ChangeCallback callback);
    
    /**
     * @brief Destructor automatically stops watching
     */
    ~ScopedFileWatcher();
    
    // Non-copyable and non-movable for simplicity
    ScopedFileWatcher(const ScopedFileWatcher&) = delete;
    ScopedFileWatcher& operator=(const ScopedFileWatcher&) = delete;
    ScopedFileWatcher(ScopedFileWatcher&&) = delete;
    ScopedFileWatcher& operator=(ScopedFileWatcher&&) = delete;
    
    /**
     * @brief Check if watching is active
     * @return true if watching, false otherwise
     */
    bool isActive() const noexcept { return watcher.isWatching(); }

private:
    FileWatcher watcher;
};

} // namespace mdux