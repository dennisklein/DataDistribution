# @author Gvozden Neskovic
# @brief  cmake for ReadoutEmulator

set(EXE_EMU_SOURCES
  CruMemoryHandler
  CruEmulator
  ReadoutDevice
  runReadoutEmulatorDevice
)

add_executable(ReadoutEmulator ${EXE_EMU_SOURCES} $<TARGET_OBJECTS:common>)

include(CheckIPOSupported)
check_ipo_supported(RESULT result)
if(result)
  set_target_properties(ReadoutEmulator PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_include_directories(ReadoutEmulator
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${MS_GSL_INCLUDE_DIR}
)

target_link_libraries(ReadoutEmulator
  PUBLIC
  PRIVATE
    FairMQ::FairMQ
    AliceO2::Headers
    ${Monitoring_LIBRARIES}
    ROOT::Gui
)


install(TARGETS ReadoutEmulator RUNTIME DESTINATION bin)
