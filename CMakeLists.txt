cmake_minimum_required(VERSION 3.5)

project(ping LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Для совместимости между компиляторами

# Укажите файлы исходного кода
set(SOURCES
    main.cpp
    ping.cpp
)

# Укажите заголовочные файлы
set(HEADERS
    ping.h  # У вас был ping.h, но в коде используется ping.hpp
)

# Создаем исполняемый файл
add_executable(ping ${SOURCES} ${HEADERS})

# Устанавливаем свойства для исполняемого файла
target_compile_features(ping PRIVATE cxx_std_11)

# Настройки для Linux (требуются права root для raw socket)
if(UNIX AND NOT APPLE)
    target_link_libraries(ping PRIVATE)
    # Для raw socket может потребоваться дополнительные библиотеки
    find_package(Threads REQUIRED)
    target_link_libraries(ping PRIVATE Threads::Threads)
endif()
