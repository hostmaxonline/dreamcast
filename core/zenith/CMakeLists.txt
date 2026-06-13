find_package(CURL REQUIRED)
add_library(zenith STATIC
    zenith_client.cpp
    pairing_dialog.cpp
)
target_include_directories(zenith PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
target_link_libraries(zenith PUBLIC CURL::libcurl)
if(NOT DEFINED ZENITH_SECRET)
    set(ZENITH_SECRET "REPLACE_ME_SECRET")
endif()
target_compile_definitions(zenith PUBLIC
    ZENITH_HOME_WORKER_SECRET="${ZENITH_SECRET}"
)
target_compile_features(zenith PUBLIC cxx_std_17)
