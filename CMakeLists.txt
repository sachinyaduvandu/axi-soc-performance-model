cmake_minimum_required(VERSION 3.10)
project(AXI_SoC_Performance_Model)

set(CMAKE_CXX_STANDARD 17)

set(SYSTEMC_PREFIX "/usr/local/systemc" CACHE PATH "SystemC Installation Directory" FORCE)
include_directories(${SYSTEMC_PREFIX}/include)
link_directories(${SYSTEMC_PREFIX}/lib)

add_executable(soc_sim
    src/simulation/top.cpp
    src/traffic/cpu_traffic_generator.cpp
    src/traffic/dma_model.cpp
    src/traffic/dnn_model.cpp
    src/axi/axi_interconnect.cpp
    src/memory/memory_controller.cpp
    src/traffic/memory_scheduler_stress.cpp
    src/traffic/memory_bandwidth_sweep.cpp
)

target_link_libraries(soc_sim systemc)
