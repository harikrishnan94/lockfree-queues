cmake_minimum_required(VERSION 3.8)

set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR})

if(MSVC)
  # Force to always compile with W4
	if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    	string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	endif()
else(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
	# Update if necessary
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")
endif()

set(CQUEUE_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/src/cqueue)

add_subdirectory(src)
add_subdirectory(test)
