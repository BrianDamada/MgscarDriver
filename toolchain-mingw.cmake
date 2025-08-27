set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compiladores
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)

# Diretório raiz para encontrar libs e includes
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)

# Evita buscar libs/includes no sistema Linux
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Flags para linkagem estática
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
