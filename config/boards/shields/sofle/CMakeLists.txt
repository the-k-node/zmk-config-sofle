# CMakeLists.txt for Sofle Shield Custom Display Status Screen
if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
    target_sources(app PRIVATE custom_status_screen.c)
endif()
