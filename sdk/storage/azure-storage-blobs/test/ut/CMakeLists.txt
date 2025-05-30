# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required (VERSION 3.13)

project (azure-storage-blobs-test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(GoogleTest)

# Export the test folder for recordings access.
add_compile_definitions(AZURE_TEST_RECORDING_DIR="${CMAKE_CURRENT_LIST_DIR}")
include(TestProxyPrep)
SetUpTestProxy("sdk/storage")

add_executable (
  azure-storage-blobs-test
    append_blob_client_test.cpp
    append_blob_client_test.hpp
    bearer_token_test.cpp
    blob_batch_client_test.cpp
    blob_container_client_test.cpp
    blob_container_client_test.hpp
    blob_query_test.cpp
    blob_sas_test.cpp
    blob_service_client_test.cpp
    blob_service_client_test.hpp
    block_blob_client_test.cpp
    block_blob_client_test.hpp
    connection_reuse_test.cpp
    macro_guard.cpp
    page_blob_client_test.cpp
    page_blob_client_test.hpp
    simplified_header_test.cpp
    storage_retry_policy_test.cpp
    storage_timeout_test.cpp
    # Include shared test source code
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../azure-storage-common/test/ut/test_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../azure-storage-common/test/ut/test_base.hpp
)

target_compile_definitions(azure-storage-blobs-test PRIVATE _azure_BUILDING_TESTS)

create_per_service_target_build(storage azure-storage-blobs-test)

create_map_file(azure-storage-blobs-test azure-storage-blobs-test.map)

# Include shared test headers
target_include_directories(azure-storage-blobs-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../azure-storage-common)

target_link_libraries(azure-storage-blobs-test PRIVATE azure-identity azure-storage-blobs azure-storage-files-shares azure-core-test-fw gtest gtest_main gmock)

# gtest_discover_tests will scan the test from azure-storage-blobs-test and call add_test
# for each test to ctest. This enables `ctest -r` to run specific tests directly.
gtest_discover_tests(azure-storage-blobs-test
     TEST_PREFIX azure-storage-blobs.
     NO_PRETTY_TYPES
     NO_PRETTY_VALUES
     DISCOVERY_TIMEOUT 600)
