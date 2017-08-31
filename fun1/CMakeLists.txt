cmake_minimum_required (VERSION 3.1)
set(CMAKE_C_STANDARD 11)
project (fun1)

# Build type
if(NOT CMAKE_BUILD_TYPE )
   set( CMAKE_BUILD_TYPE "Release" )
endif()

# C flags
set(CMAKE_C_FLAGS_RELEASE "-fPIC -ftree-vectorize -pipe -Wall -O2")

# Definitions  : only need -DOMX_SKIP64BIT to avoid a warning
add_definitions(
# -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX
# -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -DHAVE_LIBOPENMAX=2 
# -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM 
-DOMX_SKIP64BIT
)

# Includes
include_directories(
 	/opt/vc/include
#	/opt/vc/include/interface/vcos/pthreads 
#	/opt/vc/include/interface/vmcs_host/linux
)

# Threads
find_package(Threads)

# Libs
link_directories(/opt/vc/lib)
set(RPI_LIBS openmaxil bcm_host vcos ${CMAKE_THREAD_LIBS_INIT})

# Targets
add_executable(play play.c)
target_link_libraries(play ${RPI_LIBS})

add_executable(play2 play2.c)
target_link_libraries(play2 ${RPI_LIBS})
