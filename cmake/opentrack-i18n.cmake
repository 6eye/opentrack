function(otr_i18n_for_target_directory n)
    set(k "opentrack-${n}")

    get_property(lupdate-binary TARGET "${Qt5_LUPDATE_EXECUTABLE}" PROPERTY IMPORTED_LOCATION)

    foreach(i ${opentrack_all-translations})
        set(t "${CMAKE_CURRENT_SOURCE_DIR}/lang/${i}.ts")
        set(t2 "${CMAKE_CURRENT_BINARY_DIR}/lang/${i}.ts")
        set(input "${${k}-all}")
        add_custom_command(OUTPUT "${t2}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/lang"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lang"
            COMMAND "${lupdate-binary}"
                -I "${CMAKE_SOURCE_DIR}"
                -silent
                -recursive
                -no-obsolete
                -locations none
                .
                -ts "${t}"
            COMMAND "${CMAKE_COMMAND}" -E copy "${t}" "${t2}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS ${input}
            COMMENT "Running lupdate for ${n}/${i}")
        set_property(SOURCE ${input} PROPERTY GENERATED TRUE)
        set(target-name "i18n-lang-${i}-module-${n}")
        add_custom_target(${target-name} DEPENDS "${t2}" "${t}")
        set_property(GLOBAL APPEND PROPERTY "opentrack-ts-files-${i}" "${t2}")
        set_property(GLOBAL APPEND PROPERTY "opentrack-ts-module-${n}" "${target-name}")
    endforeach()
endfunction()

function(otr_merge_translations)
    install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/i18n\")")

    get_property(variant GLOBAL PROPERTY opentrack-variant)
    if(NOT ".${variant}" STREQUAL ".default")
        set(force-skip-update TRUE)
    else()
        set(force-skip-update FALSE)
    endif()

    set(all-qm-files "")

    get_property(all-modules GLOBAL PROPERTY opentrack-all-modules)

    set(all-ts-targets "")

    foreach(target ${all-modules})
        get_property(ts-targets GLOBAL PROPERTY "opentrack-ts-module-${target}")
        list(APPEND all-ts-targets "${ts-targets}")
    endforeach()

    foreach(i ${opentrack_all-translations})
        get_property(ts-files GLOBAL PROPERTY "opentrack-ts-files-${i}")
        get_property(lrelease-binary TARGET "${Qt5_LRELEASE_EXECUTABLE}" PROPERTY IMPORTED_LOCATION)

        set(qm-output "${CMAKE_CURRENT_BINARY_DIR}/${i}.qm")
        list(APPEND all-qm-files "${qm-output}")

        add_custom_command(OUTPUT "${qm-output}"
            COMMAND "${lrelease-binary}" -nounfinished -silent ${ts-files} -qm "${qm-output}"
            DEPENDS ${all-ts-targets}
            COMMENT "Running lrelease for ${i}")

        install(FILES "${qm-output}"
                DESTINATION "${CMAKE_INSTALL_PREFIX}/${opentrack-i18n-pfx}"
                PERMISSIONS ${opentrack-perms-file})
    endforeach()

    add_custom_target(i18n ALL DEPENDS ${all-qm-files})
endfunction()
