# Version.cmake - 版本号自动递增管理

# 读取当前版本号
function(read_version version_file)
    if(EXISTS ${version_file})
        file(READ ${version_file} CURRENT_VERSION)
        string(STRIP "${CURRENT_VERSION}" CURRENT_VERSION)
        message(STATUS "当前版本号: ${CURRENT_VERSION}")
    else()
        set(CURRENT_VERSION "1.0.0.0")
        message(STATUS "版本文件不存在，使用默认版本: ${CURRENT_VERSION}")
    endif()
    
    set(CURRENT_VERSION ${CURRENT_VERSION} PARENT_SCOPE)
endfunction()

# 递增版本号第四位
function(increment_version version_file)
    read_version(${version_file})
    
    # 解析版本号各部分
    string(REPLACE "." ";" VERSION_LIST ${CURRENT_VERSION})
    list(LENGTH VERSION_LIST VERSION_LENGTH)
    
    if(VERSION_LENGTH EQUAL 4)
        list(GET VERSION_LIST 0 MAJOR)
        list(GET VERSION_LIST 1 MINOR)
        list(GET VERSION_LIST 2 PATCH)
        list(GET VERSION_LIST 3 BUILD)
        
        # 递增构建号
        math(EXPR NEW_BUILD "${BUILD} + 1")
        set(NEW_VERSION "${MAJOR}.${MINOR}.${PATCH}.${NEW_BUILD}")
        
        # 写回文件
        file(WRITE ${version_file} "${NEW_VERSION}")
        message(STATUS "版本号更新为: ${NEW_VERSION}")
        
        set(UPDATED_VERSION ${NEW_VERSION} PARENT_SCOPE)
    else()
        message(WARNING "版本号格式错误，使用默认版本")
        set(UPDATED_VERSION "1.0.0.0" PARENT_SCOPE)
    endif()
endfunction()