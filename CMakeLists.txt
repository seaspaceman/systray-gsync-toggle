#----------------------------------------------------------------------------------------------------------------------
# Project config
#----------------------------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.27)
project(systray-gsync-toggle 
        VERSION 1.0.0
        DESCRIPTION "Windows system tray app to toggle G-Sync modes."
        HOMEPAGE_URL "https://github.com/FrogTheFrog/gsync-toggle"
        LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 COMPONENTS Core Gui Widgets Svg REQUIRED)

#----------------------------------------------------------------------------------------------------------------------
# Compile settings
#----------------------------------------------------------------------------------------------------------------------

if(MSVC)
    
    add_compile_options(/W4 /WX)
else()
    
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

#----------------------------------------------------------------------------------------------------------------------
# Subdirectories
#----------------------------------------------------------------------------------------------------------------------

add_subdirectory(externals/nvapi-wrapper)
add_subdirectory(vendor/QHotkey)
add_subdirectory(src)


if(WIN32)
    set(APP_RESOURCE_FILE "${CMAKE_SOURCE_DIR}/resources/windows.rc")
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /nologo")
    set(CMAKE_RC_COMPILER_INIT "rc")
    set(CMAKE_RC_COMPILE_OBJECT
        "<CMAKE_RC_COMPILER> /I${CMAKE_SOURCE_DIR}/resources /fo<OBJECT> <SOURCE>")
endif()


if(WIN32)
    set(APP_ICON "${CMAKE_SOURCE_DIR}/resources/icon.ico")
    set_target_properties(systray-gsync-toggle PROPERTIES
        WIN32_EXECUTABLE TRUE
        LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )
    if(MSVC)
        set_target_properties(systray-gsync-toggle PROPERTIES
            LINK_FLAGS "${LINK_FLAGS} /MANIFEST:NO"
        )
    endif()
endif()
