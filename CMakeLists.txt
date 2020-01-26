cmake_minimum_required(VERSION 3.15)
project(ftpd C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

option(MOD_FTPD "Enable /ftpd Module" OFF)
option(SCAP "Add SCAP to /ftpd. Requires zlib" OFF)

add_executable(ftpd
        ftpd.c
        ftpd.h
        ftpd_util.c
        ftpd_util.h
        ftpd_vfs.c
        ftpd_vfs.h
        mod_ftpd.c
        mod_home.c
        mod_mnt.c
        mod_root.c
        mod_tmp.c
        scap.c
        scap.h main.c)

if( MOD_FTPD )
    add_definitions( _ENABLE_MOD_FTPD )
endif( MOD_FTPD )

if ( SCAP )
    add_definitions( _ENABLE_SCAP )
endif( SCAP )

target_link_libraries(ftpd ws2_32)