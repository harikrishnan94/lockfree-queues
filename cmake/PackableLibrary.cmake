# Must include after the project call due to GNUInstallDirs requiring a language be enabled (IE.
# CXX)
include(GNUInstallDirs)

# Necessary for 'write_basic_package_version_file'
include(CMakePackageConfigHelpers)

function(add_packable_library LIB_NAME)
    set(OPTIONAL_ARGS INTERFACE)
    set(SINGLEVALUE_ARGS ROOT_INCLUDE_DIR NAMESPACE DEPENDENCIES_CMAKE)
    set(MULTIVALUE_ARGS INTERFACE_INCLUDE_DIRS)
    cmake_parse_arguments(ADD_PACK_LIB "${OPTIONAL_ARGS}" "${SINGLEVALUE_ARGS}"
                          "${MULTIVALUE_ARGS}" ${ARGN})

    if(ADD_PACK_LIB_INTERFACE)
        add_library(${LIB_NAME} INTERFACE)
    else()
        add_library(${LIB_NAME})
    endif()

    add_library(${ADD_PACK_LIB_NAMESPACE}::${LIB_NAME} ALIAS ${LIB_NAME})

    target_include_directories(
        ${LIB_NAME} INTERFACE $<BUILD_INTERFACE:${ADD_PACK_LIB_ROOT_INCLUDE_DIR}>
                              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    foreach(include_dir IN ITEMS ${ADD_PACK_LIB_INTERFACE_INCLUDE_DIRS})
        install(DIRECTORY ${ADD_PACK_LIB_ROOT_INCLUDE_DIR}/${include_dir}
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endforeach()

    if(ADD_PACK_LIB_DEPENDENCIES_CMAKE)
        file(
            WRITE ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake.in
            "
@PACKAGE_INIT@

include(\"\${CMAKE_CURRENT_LIST_DIR}/${LIB_NAME}Targets.cmake\")
include(\"\${CMAKE_CURRENT_LIST_DIR}/${LIB_NAME}Dependencies.cmake\")
check_required_components(\"${LIB_NAME}\")
            ")

        install(
            FILES ${ADD_PACK_LIB_DEPENDENCIES_CMAKE}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/${LIB_NAME}
            RENAME ${LIB_NAME}Dependencies.cmake)
    else()
        file(
            WRITE ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake.in
            "
@PACKAGE_INIT@

include(\"\${CMAKE_CURRENT_LIST_DIR}/${LIB_NAME}.cmake\")
check_required_components(\"${LIB_NAME}\")
        ")
    endif()

    install(
        TARGETS ${LIB_NAME}
        EXPORT ${LIB_NAME}_Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Make library importable by other projects
    install(
        EXPORT ${LIB_NAME}_Targets
        NAMESPACE ${ADD_PACK_LIB_NAMESPACE}::
        FILE ${LIB_NAME}Targets.cmake
        DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/${LIB_NAME})

    if(ADD_PACK_LIB_INTERFACE)
        write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}ConfigVersion.cmake
                                         COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    else()
        write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}ConfigVersion.cmake
                                         COMPATIBILITY SameMajorVersion)
    endif()

    configure_package_config_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/${LIB_NAME})

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}Config.cmake
            DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/${LIB_NAME})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}ConfigVersion.cmake
            DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/${LIB_NAME})
endfunction()
