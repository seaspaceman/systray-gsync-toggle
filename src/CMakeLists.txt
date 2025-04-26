#----------------------------------------------------------------------------------------------------------------------
# Target setttings
#----------------------------------------------------------------------------------------------------------------------

add_executable(systray-gsync-toggle
    main.cpp
    resources.qrc
    ${CMAKE_SOURCE_DIR}/resources/windows.rc
)

if(WIN32)
    set_target_properties(systray-gsync-toggle PROPERTIES
        LINK_FLAGS "${LINK_FLAGS} /MANIFEST:NO"
    )
endif()

target_include_directories(systray-gsync-toggle PRIVATE
    ${Qt6Core_INCLUDE_DIRS}
    ${Qt6Gui_INCLUDE_DIRS}
    ${Qt6Widgets_INCLUDE_DIRS}
    ${Qt6Svg_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/vendor/QHotkey
    ${CMAKE_SOURCE_DIR}/externals
)

target_link_libraries(systray-gsync-toggle
    PRIVATE
    nvapiwrapper
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    QHotkey
)


add_custom_command(TARGET systray-gsync-toggle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::Core>
        $<TARGET_FILE:Qt6::Gui>
        $<TARGET_FILE:Qt6::Widgets>
        $<TARGET_FILE:Qt6::Svg>
        $<TARGET_FILE_DIR:systray-gsync-toggle>
)


add_custom_command(TARGET systray-gsync-toggle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:systray-gsync-toggle>/platforms
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
        $<TARGET_FILE_DIR:systray-gsync-toggle>/platforms
)
