cmake_minimum_required(VERSION 3.23)

project(
    InTheirShoes
    VERSION 0.5.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CommonLibSSE-NG configuration via Git submodule
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE unit tests" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "Enable Xbyak support" FORCE)
set(COMMONLIB_ENABLE_IPO OFF CACHE BOOL "Disable IPO to speed up builds" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../extern/CommonLibSSE-NG" "${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE-NG" EXCLUDE_FROM_ALL)

add_library(${PROJECT_NAME} SHARED
    src/PCH.h
    src/main.cpp
    src/Forms.h
    src/Forms.cpp
    src/GameUtil.h
    src/Session.h
    src/Singleton.h
    src/Lifecycle.h
    src/Lifecycle.cpp
    src/ActorHooks.h
    src/ActorHooks.cpp
    src/ConditionHooks.h
    src/ConditionHooks.cpp
    src/OutfitManager.h
    src/OutfitManager.cpp
    src/AliasSwapManager.h
    src/AliasSwapManager.cpp
    src/FaceManager.h
    src/FaceManager.cpp
    src/InventoryManager.h
    src/InventoryManager.cpp
    src/StatManager.h
    src/StatManager.cpp
    src/IdleAIManager.h
    src/IdleAIManager.cpp
    src/DialogueManager.h
    src/DialogueManager.cpp
    src/IdleChatter.h
    src/IdleChatter.cpp
    src/QuestConditions.h
    src/QuestConditions.cpp
    src/Papyrus.h
    src/Papyrus.cpp
    src/Serialization.h
    src/Serialization.cpp
    src/ImpersonationManager.h
    src/ImpersonationManager.cpp
)

# The plugin version comes from project(VERSION) above
configure_file(cmake/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h" @ONLY)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    src/PCH.h
)

# Configure output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "InTheirShoes"
    SUFFIX ".dll"
)
