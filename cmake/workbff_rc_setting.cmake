if(WIN32)
    #判断 COMPILE_TIME 是否为空
    if(NOT DEFINED COMPILE_TIME)
        #version 设置
        include(${CMAKE_SOURCE_DIR}/cmake/version_setting.cmake)
    endif()

    configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/workbff.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/workbff.rc
    )

    set(WINDOWS_RC_FILE ${CMAKE_CURRENT_BINARY_DIR}/workbff.rc)
else()
    set(WINDOWS_RC_FILE "")
endif()