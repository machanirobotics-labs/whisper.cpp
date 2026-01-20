set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# HTTP Server (original)
set(TARGET whisper-server)
add_executable(${TARGET} server.cpp httplib.h)

include(DefaultTargetOptions)

target_link_libraries(${TARGET} PRIVATE common json_cpp whisper ${CMAKE_THREAD_LIBS_INIT})

if (WIN32)
    target_link_libraries(${TARGET} PRIVATE ws2_32)
endif()

install(TARGETS ${TARGET} RUNTIME)

# WebSocket Streaming Server (optional - requires uWebSockets)
set(UWEBSOCKETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/uWebSockets" CACHE PATH "Path to uWebSockets")
set(USOCKETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/uSockets" CACHE PATH "Path to uSockets")

if(EXISTS ${UWEBSOCKETS_DIR} AND EXISTS ${USOCKETS_DIR})
    message(STATUS "Building WebSocket streaming server (uWebSockets found)")
    
    # Build uSockets library
    add_library(usockets STATIC
        ${USOCKETS_DIR}/src/bsd.c
        ${USOCKETS_DIR}/src/context.c
        ${USOCKETS_DIR}/src/loop.c
        ${USOCKETS_DIR}/src/socket.c
        ${USOCKETS_DIR}/src/eventing/epoll_kqueue.c
        ${USOCKETS_DIR}/src/crypto/openssl.c
        ${USOCKETS_DIR}/src/crypto/sni_tree.cpp
    )
    
    target_include_directories(usockets PUBLIC ${USOCKETS_DIR}/src)
    target_compile_definitions(usockets PRIVATE LIBUS_USE_OPENSSL)
    
    # Platform-specific compile definitions
    if(UNIX AND NOT APPLE)
        target_compile_definitions(usockets PRIVATE LIBUS_USE_EPOLL)
    endif()
    
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    
    target_link_libraries(usockets PUBLIC OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)
    
    # Linux-specific libraries
    if(UNIX AND NOT APPLE)
        target_link_libraries(usockets PUBLIC pthread dl)
    endif()
    
    # WebSocket streaming server
    set(TARGET_WS whisper-websocket-server)
    add_executable(${TARGET_WS} 
        websocket-server.cpp 
        realtime-stream.cpp
    )
    
    include(DefaultTargetOptions)
    
    target_include_directories(${TARGET_WS} PRIVATE ${UWEBSOCKETS_DIR}/src)
    target_include_directories(${TARGET_WS} PRIVATE ${USOCKETS_DIR}/src)
    
    target_link_libraries(${TARGET_WS} PRIVATE 
        common 
        json_cpp 
        whisper 
        usockets
        ${CMAKE_THREAD_LIBS_INIT}
    )
    
    # Platform-specific frameworks and libraries
    if(APPLE)
        target_link_libraries(${TARGET_WS} PRIVATE "-framework CoreFoundation" "-framework Security")
    elseif(UNIX)
        # Linux needs dl for dynamic linking
        target_link_libraries(${TARGET_WS} PRIVATE dl)
    endif()
    
    install(TARGETS ${TARGET_WS} RUNTIME)
    
else()
    message(STATUS "Skipping WebSocket server (uWebSockets not found)")
    message(STATUS "To build WebSocket server, run: ./setup-websocket.sh")
endif()
