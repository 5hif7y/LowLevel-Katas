cmake_minimum_required(VERSION 3.13)
project(fat_vfs C)

# Se puede usar sola o desde otro proyecto (add_subdirectory). En un firmware,
# sólo la biblioteca: los tests y fatimg necesitan stdio y se apagan con
#     -DFAT_VFS_HOST_TOOLS=OFF
option(FAT_VFS_HOST_TOOLS "Compilar tests y fatimg (requieren un host con stdio)" ON)
set(FAT_MAX_SECTOR_SIZE 512 CACHE STRING "Sector más grande montable (512..4096)")

add_library(fat_vfs STATIC
    src/fat.c
    src/vfs.c
    src/fat_vfs.c
)
target_include_directories(fat_vfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# PUBLIC: el tamaño de fat_fs depende de esto, así que quien incluya fat.h
# tiene que ver el mismo valor que la biblioteca.
target_compile_definitions(fat_vfs PUBLIC FAT_MAX_SECTOR_SIZE=${FAT_MAX_SECTOR_SIZE})
set_target_properties(fat_vfs PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

if(FAT_VFS_HOST_TOOLS)
    add_executable(test_fat tests/test_fat.c)
    target_link_libraries(test_fat PRIVATE fat_vfs)

    add_executable(fatimg tools/fatimg.c)
    target_link_libraries(fatimg PRIVATE fat_vfs)

    enable_testing()
    add_test(NAME test_fat COMMAND test_fat)
endif()
