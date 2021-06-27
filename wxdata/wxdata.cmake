project(scwx-data)

find_package(Boost)

set(HDR_UTIL include/scwx/util/rangebuf.hpp
             include/scwx/util/vectorbuf.hpp)
set(SRC_UTIL source/scwx/util/rangebuf.cpp
             source/scwx/util/vectorbuf.cpp)
set(HDR_WSR88D include/scwx/wsr88d/ar2v_file.hpp)
set(SRC_WSR88D source/scwx/wsr88d/ar2v_file.cpp)
set(HDR_WSR88D_RDA include/scwx/wsr88d/rda/clutter_filter_map.hpp
                   include/scwx/wsr88d/rda/digital_radar_data.hpp
                   include/scwx/wsr88d/rda/message.hpp
                   include/scwx/wsr88d/rda/message_factory.hpp
                   include/scwx/wsr88d/rda/message_header.hpp
                   include/scwx/wsr88d/rda/performance_maintenance_data.hpp
                   include/scwx/wsr88d/rda/rda_adaptation_data.hpp
                   include/scwx/wsr88d/rda/rda_status_data.hpp
                   include/scwx/wsr88d/rda/volume_coverage_pattern_data.hpp)
set(SRC_WSR88D_RDA source/scwx/wsr88d/rda/clutter_filter_map.cpp
                   source/scwx/wsr88d/rda/digital_radar_data.cpp
                   source/scwx/wsr88d/rda/message.cpp
                   source/scwx/wsr88d/rda/message_factory.cpp
                   source/scwx/wsr88d/rda/message_header.cpp
                   source/scwx/wsr88d/rda/performance_maintenance_data.cpp
                   source/scwx/wsr88d/rda/rda_adaptation_data.cpp
                   source/scwx/wsr88d/rda/rda_status_data.cpp
                   source/scwx/wsr88d/rda/volume_coverage_pattern_data.cpp)

add_library(wxdata OBJECT ${HDR_UTIL}
                          ${SRC_UTIL}
                          ${HDR_WSR88D}
                          ${SRC_WSR88D}
                          ${HDR_WSR88D_RDA}
                          ${SRC_WSR88D_RDA})

source_group("Header Files\\util"        FILES ${HDR_UTIL})
source_group("Source Files\\util"        FILES ${SRC_UTIL})
source_group("Header Files\\wsr88d"      FILES ${HDR_WSR88D})
source_group("Source Files\\wsr88d"      FILES ${SRC_WSR88D})
source_group("Header Files\\wsr88d\\rda" FILES ${HDR_WSR88D_RDA})
source_group("Source Files\\wsr88d\\rda" FILES ${SRC_WSR88D_RDA})

target_include_directories(wxdata PRIVATE ${Boost_INCLUDE_DIR}
                                          ${scwx-data_SOURCE_DIR}/include
                                          ${scwx-data_SOURCE_DIR}/source)
target_include_directories(wxdata INTERFACE ${scwx-data_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(wxdata PRIVATE /W3)
endif()

set_target_properties(wxdata PROPERTIES CXX_STANDARD 17
                                        CXX_STANDARD_REQUIRED ON
                                        CXX_EXTENSIONS OFF)