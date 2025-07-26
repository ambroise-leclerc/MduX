# VulkanHelper.cmake - Windows Vulkan SDK path hints for find_package(Vulkan)

function(configure_vulkan_windows)
  if(WIN32)
    # Only provide path hints if Vulkan variables are not already set
    # Let CMake's find_package(Vulkan) do the heavy lifting
    
    if(NOT Vulkan_INCLUDE_DIR AND NOT Vulkan_LIBRARY)
      # Common Vulkan SDK installation directories
      set(VULKAN_HINT_PATHS)
      
      # 1. Environment variable
      if(ENV{VULKAN_SDK})
        list(APPEND VULKAN_HINT_PATHS "$ENV{VULKAN_SDK}")
      endif()
      
      # 2. Standard installation paths with dynamic version discovery
      set(VULKAN_BASE_DIRS 
        "C:/VulkanSDK" 
        "C:/Program Files/VulkanSDK" 
        "$ENV{ProgramFiles}/VulkanSDK" 
        "$ENV{ProgramFiles\(x86\)}/VulkanSDK"
      )
      
      foreach(BASE_DIR ${VULKAN_BASE_DIRS})
        if(EXISTS "${BASE_DIR}")
          file(GLOB VULKAN_VERSIONS RELATIVE "${BASE_DIR}" "${BASE_DIR}/*")
          list(SORT VULKAN_VERSIONS COMPARE NATURAL ORDER DESCENDING)
          foreach(VERSION ${VULKAN_VERSIONS})
            if(IS_DIRECTORY "${BASE_DIR}/${VERSION}")
              list(APPEND VULKAN_HINT_PATHS "${BASE_DIR}/${VERSION}")
            endif()
          endforeach()
        endif()
      endforeach()
      
      # 3. Set hints for find_package(Vulkan) to use
      if(VULKAN_HINT_PATHS)
        list(GET VULKAN_HINT_PATHS 0 FIRST_PATH)
        if(EXISTS "${FIRST_PATH}/Include/vulkan/vulkan.h")
          set(Vulkan_INCLUDE_DIR "${FIRST_PATH}/Include" PARENT_SCOPE)
        endif()
        if(EXISTS "${FIRST_PATH}/Lib/vulkan-1.lib")
          set(Vulkan_LIBRARY "${FIRST_PATH}/Lib/vulkan-1.lib" PARENT_SCOPE)
        endif()
      endif()
    endif()
  endif()
endfunction()