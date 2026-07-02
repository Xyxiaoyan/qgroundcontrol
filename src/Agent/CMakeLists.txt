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
        ConfigStore.cpp
        ConfigStore.h
        LogAnalyzer.cpp
        LogAnalyzer.h
        LogAnalysisModel.cpp
        LogAnalysisModel.h

        AgentTool.h
        ToolRegistry.cpp
        ToolRegistry.h
        ReActLoop.cpp
        ReActLoop.h
        ConversationHistory.cpp
        ConversationHistory.h

        Tools/VehicleContextTool.cpp
        Tools/VehicleContextTool.h
        Tools/FlightControlTool.cpp
        Tools/FlightControlTool.h
        Tools/RelativeMoveTool.cpp
        Tools/RelativeMoveTool.h
        Tools/AltitudeSpeedTool.cpp
        Tools/AltitudeSpeedTool.h
        Tools/FlightModeTool.cpp
        Tools/FlightModeTool.h
        Tools/ParameterTool.cpp
        Tools/ParameterTool.h
        Tools/MavlinkCommandTool.cpp
        Tools/MavlinkCommandTool.h
        Tools/LogAnalysisTool.cpp
        Tools/LogAnalysisTool.h
        Tools/LocationTool.cpp
        Tools/LocationTool.h
        Tools/MissionTool.cpp
        Tools/MissionTool.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Tools)
