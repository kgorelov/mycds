include(FindPackageHandleStandardArgs)

find_program(INTLTOOL_UPDATE_EXECUTABLE NAMES intltool-update)
find_program(INTLTOOL_EXTRACT_EXECUTABLE NAMES intltool-extract)
find_program(INTLTOOL_MERGE_EXECUTABLE NAMES intltool-merge)
find_program(INTLTOOL_PREPARE_EXECUTABLE NAMES intltool-prepare)
find_program(GETTEXT_MSGFMT_EXECUTABLE NAMES msgfmt)

if(INTLTOOL_UPDATE_EXECUTABLE)
    execute_process(
        COMMAND ${INTLTOOL_UPDATE_EXECUTABLE} --version
        COMMAND head -n 1
        COMMAND cut -d " " -f 3
        OUTPUT_VARIABLE INTLTOOL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(INTLTOOL_VERSION "0.51.0")
endif()

set(IntlTool_FOUND TRUE)
set(INTLTOOL_FOUND TRUE)

function(intltool_process_po_files po_dir catalog_name)
    if(NOT GETTEXT_MSGFMT_EXECUTABLE)
        return()
    endif()

    set(gmo_files)
    if(${ARGC} GREATER 2)
        set(po_files)
        foreach(whitelisted_language ${ARGN})
            list(APPEND po_files ${po_dir}/${whitelisted_language}.po)
        endforeach()
    else()
        file(GLOB po_files ${po_dir}/*.po)
    endif()

    foreach(po_file ${po_files})
        get_filename_component(lang ${po_file} NAME_WE)
        set(gmo_file ${CMAKE_CURRENT_BINARY_DIR}/${lang}.gmo)
        add_custom_command(
            OUTPUT ${gmo_file}
            COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check -o ${gmo_file} ${po_file}
            DEPENDS ${po_file}
        )
        install(
            FILES ${gmo_file}
            DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES
            RENAME ${catalog_name}.mo
        )
        list(APPEND gmo_files ${gmo_file})
    endforeach()

    set(translations_target "${PROJECT_NAME}-translations")
    add_custom_target(${translations_target} ALL DEPENDS ${gmo_files})
endfunction()

function(intltool_merge options po_dir in_filename out_filename)
    if(INTLTOOL_MERGE_EXECUTABLE AND EXISTS "${po_dir}")
        string(FIND "${options}" "--no-translations" no_translations)
        string(REPLACE " " ";" options_list "${options}")
        if(${no_translations} EQUAL -1)
            add_custom_command(
                OUTPUT ${out_filename}
                COMMAND ${INTLTOOL_MERGE_EXECUTABLE} ${options_list} -q -u ${po_dir}
                    ${in_filename} ${out_filename}
                DEPENDS ${in_filename}
            )
        else()
            add_custom_command(
                OUTPUT ${out_filename}
                COMMAND ${INTLTOOL_MERGE_EXECUTABLE} ${options_list} -q -u
                    ${in_filename} ${out_filename}
                DEPENDS ${in_filename}
            )
        endif()
    else()
        add_custom_command(
            OUTPUT ${out_filename}
            COMMAND ${CMAKE_COMMAND} -E copy ${in_filename} ${out_filename}
            DEPENDS ${in_filename}
        )
    endif()

    get_filename_component(out_name ${out_filename} NAME)
    add_custom_target(intltool-merge-${out_name} ALL DEPENDS ${out_filename})
endfunction()
