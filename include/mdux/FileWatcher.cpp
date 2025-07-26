/**
 * @file FileWatcher.cpp
 * @brief Implementation of cross-platform file watching utility
 */

#include "FileWatcher.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <locale>
#include <cctype>
#include <cwctype>

#ifdef MDUX_PLATFORM_WINDOWS
    #include <io.h>
    #include <fcntl.h>
#endif

#ifdef MDUX_PLATFORM_LINUX
    #include <sys/epoll.h>
    #include <cerrno>
    #include <cstring>
#endif

namespace mdux {

FileWatcher::~FileWatcher() {
    stopWatching();
}

FileWatcher::FileWatcher(FileWatcher&& other) noexcept
    : watchedFile(std::move(other.watchedFile))
    , changeCallback(std::move(other.changeCallback))
    , isActive(other.isActive.load())
    , shouldStop(other.shouldStop.load())
    , watchThread(std::move(other.watchThread))
{
#ifdef MDUX_PLATFORM_WINDOWS
    directoryHandle = other.directoryHandle;
    stopEvent = other.stopEvent;
    other.directoryHandle = INVALID_HANDLE_VALUE;
    other.stopEvent = nullptr;
#elif defined(MDUX_PLATFORM_LINUX)
    inotifyFd = other.inotifyFd;
    watchDescriptor = other.watchDescriptor;
    other.inotifyFd = -1;
    other.watchDescriptor = -1;
#endif
    
    // Reset other's state
    other.isActive = false;
    other.shouldStop = false;
}

FileWatcher& FileWatcher::operator=(FileWatcher&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        stopWatching();
        
        // Move data
        watchedFile = std::move(other.watchedFile);
        changeCallback = std::move(other.changeCallback);
        isActive = other.isActive.load();
        shouldStop = other.shouldStop.load();
        watchThread = std::move(other.watchThread);
        
#ifdef MDUX_PLATFORM_WINDOWS
        directoryHandle = other.directoryHandle;
        stopEvent = other.stopEvent;
        other.directoryHandle = INVALID_HANDLE_VALUE;
        other.stopEvent = nullptr;
#elif defined(MDUX_PLATFORM_LINUX)
        inotifyFd = other.inotifyFd;
        watchDescriptor = other.watchDescriptor;
        other.inotifyFd = -1;
        other.watchDescriptor = -1;
#endif
        
        // Reset other's state
        other.isActive = false;
        other.shouldStop = false;
    }
    return *this;
}

bool FileWatcher::startWatching(const std::filesystem::path& filePath, 
                               ChangeCallback callback) {
    if (isActive.load()) {
        stopWatching();
    }
    
    if (!std::filesystem::exists(filePath)) {
        std::cerr << "FileWatcher: File does not exist: " << filePath << std::endl;
        return false;
    }
    
    watchedFile = filePath;
    changeCallback = std::move(callback);
    shouldStop = false;
    
    try {
#ifdef MDUX_PLATFORM_WINDOWS
        // Check if we're on a problematic filesystem or directory
        // that doesn't support proper change notifications
        bool shouldUsePolling = false;
        auto absolutePath = std::filesystem::absolute(filePath);
        std::string pathStr = absolutePath.string();
        
        // Check for network drives, mapped drives, or other problematic locations
        if (pathStr.length() >= 2 && pathStr[1] == ':') {
            // Local drive, try ReadDirectoryChangesW first
            auto directory = filePath.parent_path();
            directoryHandle = CreateFileW(
                directory.wstring().c_str(),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                nullptr
            );
            
            if (directoryHandle == INVALID_HANDLE_VALUE) {
                DWORD error = GetLastError();
                std::cerr << "FileWatcher: Failed to open directory handle (error " << error << "). Falling back to polling mode." << std::endl;
                shouldUsePolling = true;
            } else {
                // Create stop event
                stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
                if (!stopEvent) {
                    CloseHandle(directoryHandle);
                    directoryHandle = INVALID_HANDLE_VALUE;
                    shouldUsePolling = true;
                } else {
                    std::cout << "FileWatcher: Using ReadDirectoryChangesW for file monitoring." << std::endl;
                }
            }
        } else {
            std::cout << "FileWatcher: Detected network or special path. Using polling mode for better compatibility." << std::endl;
            shouldUsePolling = true;
        }
        
        if (shouldUsePolling) {
            // Use polling mode
            isActive = true;
            watchThread = std::thread(&FileWatcher::pollingWatchLoop, this);
        } else {
            isActive = true;
            watchThread = std::thread(&FileWatcher::windowsWatchLoop, this);
        }
        
#elif defined(MDUX_PLATFORM_LINUX)
        // Check if we're on a filesystem that supports inotify properly
        // Many virtual/mounted filesystems (like 9p in WSL2) don't support inotify events
        bool shouldUsePolling = false;
        
        // Check if path starts with /mnt/ (common for mounted Windows drives in WSL)
        auto absolutePath = std::filesystem::absolute(filePath);
        std::string pathStr = absolutePath.string();
        if (pathStr.starts_with("/mnt/")) {
            shouldUsePolling = true;
            std::cout << "FileWatcher: Detected mounted filesystem. Using polling mode for better compatibility." << std::endl;
        }
        
        if (shouldUsePolling) {
            // Use polling directly
            isActive = true;
            watchThread = std::thread(&FileWatcher::pollingWatchLoop, this);
        } else {
            // Try to initialize inotify
            inotifyFd = inotify_init1(IN_NONBLOCK);
            if (inotifyFd == -1) {
                std::cout << "FileWatcher: Failed to initialize inotify: " 
                         << std::strerror(errno) << ". Falling back to polling mode." << std::endl;
                isActive = true;
                watchThread = std::thread(&FileWatcher::pollingWatchLoop, this);
            } else {
                // Add watch for the specific file
                watchDescriptor = inotify_add_watch(inotifyFd, filePath.c_str(), 
                                                  IN_MODIFY | IN_CLOSE_WRITE);
                if (watchDescriptor == -1) {
                    std::cout << "FileWatcher: Failed to add inotify watch: " 
                             << std::strerror(errno) << ". Falling back to polling mode." << std::endl;
                    close(inotifyFd);
                    inotifyFd = -1;
                    isActive = true;
                    watchThread = std::thread(&FileWatcher::pollingWatchLoop, this);
                } else {
                    std::cout << "FileWatcher: Using inotify for file monitoring." << std::endl;
                    isActive = true;
                    watchThread = std::thread(&FileWatcher::linuxWatchLoop, this);
                }
            }
        }
        
#else
        // Fallback to polling
        isActive = true;
        watchThread = std::thread(&FileWatcher::pollingWatchLoop, this);
#endif
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "FileWatcher: Exception during initialization: " << e.what() << std::endl;
        cleanup();
        return false;
    }
}

void FileWatcher::stopWatching() {
    if (!isActive.load()) {
        return;
    }
    
    shouldStop = true;
    
#ifdef MDUX_PLATFORM_WINDOWS
    if (stopEvent) {
        SetEvent(stopEvent);
    }
#endif
    
    if (watchThread.joinable()) {
        watchThread.join();
    }
    
    cleanup();
    isActive = false;
}

#ifdef MDUX_PLATFORM_WINDOWS
void FileWatcher::windowsWatchLoop() {
    const DWORD bufferSize = 8192;
    char buffer[bufferSize];
    DWORD bytesReturned;
    OVERLAPPED overlapped = {};
    
    // Create event for overlapped I/O
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) {
        std::cerr << "FileWatcher: Failed to create overlapped event" << std::endl;
        return;
    }
    
    auto filename = watchedFile.filename().wstring();
    std::transform(filename.begin(), filename.end(), filename.begin(), std::towlower);
    
    std::wcout << L"FileWatcher: Watching for changes to: " << filename << std::endl;
    
    while (!shouldStop.load()) {
        // Reset the overlapped structure
        ResetEvent(overlapped.hEvent);
        
        // Start asynchronous read
        BOOL result = ReadDirectoryChangesW(
            directoryHandle,
            buffer,
            bufferSize,
            FALSE, // Don't watch subdirectories
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SIZE,
            &bytesReturned,
            &overlapped,
            nullptr
        );
        
        if (!result) {
            DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) {
                std::cerr << "FileWatcher: ReadDirectoryChangesW failed with error " << error << std::endl;
                break;
            }
        }
        
        // Wait for either directory change or stop event (with timeout)
        HANDLE events[] = {overlapped.hEvent, stopEvent};
        DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, 1000); // 1 second timeout
        
        if (waitResult == WAIT_OBJECT_0) {
            // Directory change occurred
            if (GetOverlappedResult(directoryHandle, &overlapped, &bytesReturned, FALSE)) {
                // Parse the change notifications
                DWORD offset = 0;
                while (offset < bytesReturned) {
                    auto* notification = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer + offset);
                    
                    // Convert filename to compare (case-insensitive)
                    std::wstring changedFile(notification->FileName, 
                                           notification->FileNameLength / sizeof(wchar_t));
                    std::transform(changedFile.begin(), changedFile.end(), changedFile.begin(), std::towlower);
                    
                    std::wcout << L"FileWatcher: Change detected in: " << changedFile << std::endl;
                    
                    if (changedFile == filename) {
                        std::wcout << L"FileWatcher: Target file changed, triggering callback" << std::endl;
                        if (changeCallback) {
                            changeCallback();
                        }
                    }
                    
                    if (notification->NextEntryOffset == 0) {
                        break;
                    }
                    offset += notification->NextEntryOffset;
                }
            }
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            // Stop event
            break;
        } else if (waitResult == WAIT_TIMEOUT) {
            // Timeout - continue loop to check shouldStop
            continue;
        } else {
            // Error
            DWORD error = GetLastError();
            std::cerr << "FileWatcher: WaitForMultipleObjects failed with error " << error << std::endl;
            break;
        }
    }
    
    CloseHandle(overlapped.hEvent);
}
#endif

#ifdef MDUX_PLATFORM_LINUX
void FileWatcher::linuxWatchLoop() {
    const size_t bufferSize = 4096;
    char buffer[bufferSize];
    
    while (!shouldStop.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(inotifyFd, &readSet);
        
        // Wait with timeout for responsiveness to stop signal
        struct timeval timeout = {0, 100000}; // 100ms
        int selectResult = select(inotifyFd + 1, &readSet, nullptr, nullptr, &timeout);
        
        if (selectResult > 0 && FD_ISSET(inotifyFd, &readSet)) {
            ssize_t length = read(inotifyFd, buffer, bufferSize);
            if (length > 0) {
                size_t offset = 0;
                while (offset < static_cast<size_t>(length)) {
                    auto* event = reinterpret_cast<struct inotify_event*>(buffer + offset);
                    
                    if ((event->mask & (IN_MODIFY | IN_CLOSE_WRITE)) && changeCallback) {
                        changeCallback();
                    }
                    
                    offset += sizeof(struct inotify_event) + event->len;
                }
            }
        } else if (selectResult == -1 && errno != EINTR) {
            // Error occurred
            break;
        }
    }
}
#endif

void FileWatcher::pollingWatchLoop() {
    auto lastWriteTime = getLastWriteTime();
    
    std::cout << "FileWatcher: Using polling mode for file monitoring (checking every 250ms)" << std::endl;
    
    while (!shouldStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        
        auto currentWriteTime = getLastWriteTime();
        if (currentWriteTime != lastWriteTime && 
            currentWriteTime != std::filesystem::file_time_type::min()) {
            
            std::cout << "FileWatcher: File change detected via polling: " << watchedFile.filename() << std::endl;
            lastWriteTime = currentWriteTime;
            if (changeCallback) {
                changeCallback();
            }
        }
    }
    
    std::cout << "FileWatcher: Polling loop stopped" << std::endl;
}

std::filesystem::file_time_type FileWatcher::getLastWriteTime() const {
    try {
        return std::filesystem::last_write_time(watchedFile);
    } catch (const std::filesystem::filesystem_error&) {
        return std::filesystem::file_time_type::min();
    }
}

void FileWatcher::cleanup() {
#ifdef MDUX_PLATFORM_WINDOWS
    if (directoryHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(directoryHandle);
        directoryHandle = INVALID_HANDLE_VALUE;
    }
    if (stopEvent) {
        CloseHandle(stopEvent);
        stopEvent = nullptr;
    }
#elif defined(MDUX_PLATFORM_LINUX)
    if (watchDescriptor != -1) {
        inotify_rm_watch(inotifyFd, watchDescriptor);
        watchDescriptor = -1;
    }
    if (inotifyFd != -1) {
        close(inotifyFd);
        inotifyFd = -1;
    }
#endif
}

// ScopedFileWatcher implementation
ScopedFileWatcher::ScopedFileWatcher(const std::filesystem::path& filePath, 
                                   FileWatcher::ChangeCallback callback) {
    if (!watcher.startWatching(filePath, std::move(callback))) {
        throw std::runtime_error("Failed to start file watching for: " + filePath.string());
    }
}

ScopedFileWatcher::~ScopedFileWatcher() {
    watcher.stopWatching();
}

} // namespace mdux