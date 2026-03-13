cmake_minimum_required(VERSION 3.16)
project(hs_loadgen CXX)

add_executable(hs_loadgen
    src/main.cpp
    src/client_session.cpp
    src/metrics_collector.cpp
)

target_include_directories(hs_loadgen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # Reuse the server's protocol.hpp without copying it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/include
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(hs_loadgen PRIVATE
    Boost::system
    Boost::thread
    Boost::program_options
    Threads::Threads
)

target_compile_definitions(hs_loadgen PRIVATE
    BOOST_ASIO_NO_DEPRECATED
)
