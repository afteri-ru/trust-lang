# Helper script to package VS Code extension as .vsix file
# Usage: cmake -P cmake/package_vsix.cmake
# Required variables: VSIX_SOURCE_DIR, VSIX_OUTPUT_DIR, VSIX_OUTPUT_FILE
# Optional variables: VSIX_ID, VSIX_VERSION, VSIX_PUBLISHER, VSIX_DISPLAY_NAME, VSIX_DESCRIPTION, VSIX_LANGUAGE, VSIX_TEMPLATE_DIR

# Default values
if(NOT DEFINED VSIX_ID)
    set(VSIX_ID "trust-lang")
endif()
if(NOT DEFINED VSIX_VERSION)
    set(VSIX_VERSION "1.0.0")
endif()
if(NOT DEFINED VSIX_PUBLISHER)
    set(VSIX_PUBLISHER "trust-lang")
endif()
if(NOT DEFINED VSIX_DISPLAY_NAME)
    set(VSIX_DISPLAY_NAME "Trust Lang")
endif()
if(NOT DEFINED VSIX_DESCRIPTION)
    set(VSIX_DESCRIPTION "Language support and debugger for Trust language programs transpiled to C++")
endif()
if(NOT DEFINED VSIX_LANGUAGE)
    set(VSIX_LANGUAGE "en-US")
endif()
if(NOT DEFINED VSIX_TEMPLATE_DIR)
    set(VSIX_TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

if(NOT DEFINED VSIX_SOURCE_DIR)
    message(FATAL_ERROR "VSIX_SOURCE_DIR is not defined")
endif()
if(NOT DEFINED VSIX_OUTPUT_DIR)
    message(FATAL_ERROR "VSIX_OUTPUT_DIR is not defined")
endif()
if(NOT DEFINED VSIX_OUTPUT_FILE)
    message(FATAL_ERROR "VSIX_OUTPUT_FILE is not defined")
endif()

# Function to replace placeholders in template
function(substitute_template input_file output_file)
    file(READ "${input_file}" content)
    string(REPLACE "{{VSIX_ID}}" "${VSIX_ID}" content "${content}")
    string(REPLACE "{{VSIX_VERSION}}" "${VSIX_VERSION}" content "${content}")
    string(REPLACE "{{VSIX_PUBLISHER}}" "${VSIX_PUBLISHER}" content "${content}")
    string(REPLACE "{{VSIX_DISPLAY_NAME}}" "${VSIX_DISPLAY_NAME}" content "${content}")
    string(REPLACE "{{VSIX_DESCRIPTION}}" "${VSIX_DESCRIPTION}" content "${content}")
    string(REPLACE "{{VSIX_LANGUAGE}}" "${VSIX_LANGUAGE}" content "${content}")
    file(WRITE "${output_file}" "${content}")
endfunction()

# Step 1: Create temporary directory structure (unique name per VSIX to avoid race conditions)
string(REPLACE "." "_" VSIX_ID_CLEAN "${VSIX_ID}")
set(TEMP_DIR "${VSIX_OUTPUT_DIR}/_vsix_build_tmp_${VSIX_ID_CLEAN}")
file(REMOVE_RECURSE "${TEMP_DIR}")
file(MAKE_DIRECTORY "${TEMP_DIR}/extension")

# Step 2: Copy extension files
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VSIX_SOURCE_DIR}" "${TEMP_DIR}/extension"
)

# Step 3: Create extension.vsixmanifest from template
substitute_template(
    "${VSIX_TEMPLATE_DIR}/extension.vsixmanifest.template"
    "${TEMP_DIR}/extension.vsixmanifest"
)

# Step 4: Create [Content_Types].xml from template (no placeholders)
file(COPY "${VSIX_TEMPLATE_DIR}/content_types.xml.template" DESTINATION "${TEMP_DIR}")
file(RENAME "${TEMP_DIR}/content_types.xml.template" "${TEMP_DIR}/[Content_Types].xml")

# Step 5: Create ZIP archive (VSIX is a ZIP with .vsix extension)
file(MAKE_DIRECTORY "${VSIX_OUTPUT_DIR}")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar "cf" "${VSIX_OUTPUT_FILE}" --format=zip "."
    WORKING_DIRECTORY "${TEMP_DIR}"
    RESULT_VARIABLE TAR_RESULT
)
if(TAR_RESULT)
    message(FATAL_ERROR "Failed to create ZIP archive")
endif()

# Step 6: Cleanup
file(REMOVE_RECURSE "${TEMP_DIR}")

message(STATUS "VSIX file created: ${VSIX_OUTPUT_FILE}")