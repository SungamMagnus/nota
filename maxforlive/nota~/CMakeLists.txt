set_property(DIRECTORY PROPERTY max::external YES)
set_property(DIRECTORY PROPERTY max::glob ON)
project(nota_tilde)

target_link_libraries(nota_tilde PRIVATE nota_dsp)
target_include_directories(nota_tilde PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../source/dsp)
