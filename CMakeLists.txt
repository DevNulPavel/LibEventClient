cmake_minimum_required(VERSION 2.8)

set (PROJECT LibEventClient)
project(${PROJECT})

# Sanitizer
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

##### Компилятор #####
# Вариант с путями
# execute_process(COMMAND which clang OUTPUT_VARIABLE CLANG_PATH)
# execute_process(COMMAND which clang++ OUTPUT_VARIABLE CLANGXX_PATH)
# if(NOT ("${CLANG_PATH}" STREQUAL ""))
# Вариант поиска программы
find_program(CLANG_FOUND clang)
if(CLANG_FOUND)
	message("-> Clang will used for compilation")
	set(CMAKE_C_COMPILER "clang")
	set(CMAKE_CXX_COMPILER "clang++")
	# Скрываем ворнинги на clang + цвета
	# set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -Qunused-arguments")
	# set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -fsanitize=address -fno-omit-frame-pointer -Qunused-arguments")
endif(CLANG_FOUND)

# Ищем Boost
# set(Boost_USE_STATIC_LIBS OFF) 
# set(Boost_USE_MULTITHREADED ON)  
# set(Boost_USE_STATIC_RUNTIME OFF)
# set(BOOST_COMPONENTS)
# find_package(Boost 1.58.0 REQUIRED COMPONENTS  ${BOOST_COMPONENTS})
# if(Boost_FOUND)
# 	include_directories(${Boost_INCLUDE_DIR})
# 	link_directories(${Boost_LIBRARY_DIRS})
# 	message("Boost FOUND")
# else(Boost_FOUND)
# 	message(FATAL_ERROR "No BOOST")
# endif(Boost_FOUND)

# Поиск библиотеки protobuf
find_package(Protobuf REQUIRED)
if(PROTOBUF_FOUND)
	include_directories(${PROTOBUF_INCLUDE_DIRS})
	message("ProtoBuf FOUND")
else(PROTOBUF_FOUND)
	message(FATAL_ERROR "No ProtoBuf")
endif(PROTOBUF_FOUND)

# Поиск библиотеки потоков
find_package(Threads REQUIRED)

# Поиск библиотеки LibEvent
find_package(LibEvent REQUIRED)
if(LibEvent_FOUND)
	include_directories(${LIBEVENT_INCLUDE_DIR})
	message("LibEvent FOUND")
else(LibEvent_FOUND)
	message(FATAL_ERROR "No ProtoBuf")
endif(LibEvent_FOUND)

# Sanitizer
if(CLANG_FOUND)
	find_package(Sanitizers)
endif(CLANG_FOUND)

# Конвертация ProtoBuf
# if(PROTOBUF_FOUND)
# 	message("ProtoBuf Generate")
# 	PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/data.proto")
# endif(PROTOBUF_FOUND)

# дефайны
add_definitions(-DDEBUG)

# флаги
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

# Список исходников
set (HEADERS 
		"SingleThreadedTCPFilter.h")
set (SOURCES
		"SingleThreadedTCPFilter.cpp"
		"main.cpp")

# создаем группу, чтобы заголовочники и исходники были в одной папке
source_group("Sources" FILES ${HEADERS} ${SOURCES})

# тип приложения
# set(APP_TYPE MACOSX_BUNDLE)

# исполняемый файл
# add_executable(${PROJECT} ${APP_TYPE} ${HEADERS} ${SOURCES})
add_executable(${PROJECT} ${APP_TYPE} ${HEADERS} ${SOURCES})

# линкуемые библиотеки
target_link_libraries(${PROJECT} ${CMAKE_THREAD_LIBS_INIT} ${LIBEVENT_LIB} ${Boost_LIBRARIES} ${PROTOBUF_LIBRARIES})

# Sanitizer
if(CLANG_FOUND)
	add_sanitizers(${PROJECT})
endif(CLANG_FOUND)
