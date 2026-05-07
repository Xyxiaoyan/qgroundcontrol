# AI Agent module
target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        AgentManager.cpp
        AgentManager.h
        LLMClient.cpp
        LLMClient.h
        ContextProvider.cpp
        ContextProvider.h
        CommandEngine.cpp
        CommandEngine.h
        AgentMapProvider.cpp
        AgentMapProvider.h
        CameraProvider.cpp
        CameraProvider.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
