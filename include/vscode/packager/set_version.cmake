# set_version.cmake - Set version and ID in package.json
# Required variables: PACKAGE_JSON_FILE, NEW_VERSION
# Optional variables: NEW_ID

if(NOT DEFINED PACKAGE_JSON_FILE)
    message(FATAL_ERROR "PACKAGE_JSON_FILE is not defined")
endif()
if(NOT DEFINED NEW_VERSION)
    message(FATAL_ERROR "NEW_VERSION is not defined")
endif()

if(NOT EXISTS "${PACKAGE_JSON_FILE}")
    message(FATAL_ERROR "package.json not found: ${PACKAGE_JSON_FILE}")
endif()

# Read file content
file(READ "${PACKAGE_JSON_FILE}" content)

# Replace version field
string(REGEX REPLACE
    "\"version\"[ ]*:[ ]*\"[^\"]*\""
    "\"version\": \"${NEW_VERSION}\""
    content
    "${content}"
)

# Replace name/ID field if provided
if(DEFINED NEW_ID)
    string(REGEX REPLACE
        "\"name\"[ ]*:[ ]*\"[^\"]*\""
        "\"name\": \"${NEW_ID}\""
        content
        "${content}"
    )
    message(STATUS "Set ID to ${NEW_ID}")
endif()

file(WRITE "${PACKAGE_JSON_FILE}" "${content}")
message(STATUS "Set version to ${NEW_VERSION} in ${PACKAGE_JSON_FILE}")
