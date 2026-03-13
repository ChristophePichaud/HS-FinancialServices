cmake_minimum_required(VERSION 3.16)
project(hs_server CXX)

add_executable(hs_server
    src/main.cpp
    src/server.cpp
    src/session.cpp
    src/services/sql_service.cpp
    src/services/trade_service.cpp
    src/services/position_service.cpp
    src/services/price_history_service.cpp
)

target_include_directories(hs_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(hs_server PRIVATE
    Boost::system
    Boost::thread
    Boost::program_options
    Threads::Threads
)

# BOOST_ASIO_NO_DEPRECATED keeps the interface clean.
target_compile_definitions(hs_server PRIVATE
    BOOST_ASIO_NO_DEPRECATED
)
