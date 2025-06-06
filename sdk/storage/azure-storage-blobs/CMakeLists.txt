# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake-modules")
include(AzureVcpkg)
az_vcpkg_integrate()

cmake_minimum_required (VERSION 3.13)
project(azure-storage-blobs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

option(FETCH_SOURCE_DEPS "build source dependencies" OFF)

include(AzureVersion)
include(AzureCodeCoverage)
include(AzureTransportAdapters)
include(AzureDoxygen)
include(AzureGlobalCompileOptions)
include(AzureConfigRTTI)
include(AzureBuildTargetForCI)
include(CreateMapFile)

if(FETCH_SOURCE_DEPS)
    set(AZ_ALL_LIBRARIES ON)
    include(FolderList)
    SetCompileOptions(STORAGE_BLOBS)
    GetFolderList(STORAGE_BLOBS)
    foreach(oneFolder IN LISTS BUILD_FOLDERS)
        message("add folder ${oneFolder}")
        add_subdirectory(${oneFolder} EXCLUDE_FROM_ALL)
    endforeach()
elseif(NOT AZ_ALL_LIBRARIES)
  find_package(azure-storage-common-cpp CONFIG QUIET)
  if(NOT azure-storage-common-cpp_FOUND)
    find_package(azure-storage-common-cpp REQUIRED)
  endif()
endif()

set(
  AZURE_STORAGE_BLOBS_HEADER
    inc/azure/storage/blobs.hpp
    inc/azure/storage/blobs/append_blob_client.hpp
    inc/azure/storage/blobs/blob_batch.hpp
    inc/azure/storage/blobs/blob_client.hpp
    inc/azure/storage/blobs/blob_container_client.hpp
    inc/azure/storage/blobs/blob_lease_client.hpp
    inc/azure/storage/blobs/blob_options.hpp
    inc/azure/storage/blobs/blob_responses.hpp
    inc/azure/storage/blobs/blob_sas_builder.hpp
    inc/azure/storage/blobs/blob_service_client.hpp
    inc/azure/storage/blobs/block_blob_client.hpp
    inc/azure/storage/blobs/deferred_response.hpp
    inc/azure/storage/blobs/dll_import_export.hpp
    inc/azure/storage/blobs/page_blob_client.hpp
    inc/azure/storage/blobs/rest_client.hpp
    inc/azure/storage/blobs/rtti.hpp
)

set(
  AZURE_STORAGE_BLOBS_SOURCE
    src/append_blob_client.cpp
    src/blob_batch.cpp
    src/blob_client.cpp
    src/blob_container_client.cpp
    src/blob_lease_client.cpp
    src/blob_options.cpp
    src/blob_responses.cpp
    src/blob_sas_builder.cpp
    src/blob_service_client.cpp
    src/block_blob_client.cpp
    src/page_blob_client.cpp
    src/private/avro_parser.cpp
    src/private/avro_parser.hpp
    src/private/package_version.hpp
    src/rest_client.cpp
)

add_library(azure-storage-blobs ${AZURE_STORAGE_BLOBS_HEADER} ${AZURE_STORAGE_BLOBS_SOURCE})
create_per_service_target_build(storage azure-storage-blobs)

# make sure that users can consume the project as a library.
add_library(Azure::azure-storage-blobs ALIAS azure-storage-blobs)

target_include_directories(
  azure-storage-blobs
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
      $<INSTALL_INTERFACE:include>
)

target_link_libraries(azure-storage-blobs PUBLIC Azure::azure-storage-common)

target_compile_definitions(azure-storage-blobs PRIVATE _azure_BUILDING_SDK)

get_az_version("${CMAKE_CURRENT_SOURCE_DIR}/src/private/package_version.hpp")
set_target_properties(azure-storage-blobs PROPERTIES VERSION ${AZ_LIBRARY_VERSION})
generate_documentation(azure-storage-blobs ${AZ_LIBRARY_VERSION})

az_vcpkg_export(
    azure-storage-blobs
    STORAGE_BLOBS
    "azure/storage/blobs/dll_import_export.hpp"
  )

az_rtti_setup(
  azure-storage-blobs
  STORAGE_BLOBS
  "azure/storage/blobs/rtti.hpp"
)

# coverage. Has no effect if BUILD_CODE_COVERAGE is OFF
create_code_coverage(storage azure-storage-blobs azure-storage-test "tests?/*;samples?/*")

if(BUILD_TESTING)
  add_subdirectory(test/ut)
  add_subdirectory(test/fault-injector)
endif()

if(BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

if(BUILD_PERFORMANCE_TESTS)
  add_subdirectory(test/perf)
endif()
