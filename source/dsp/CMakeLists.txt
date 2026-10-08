add_library(nota_dsp STATIC
    Engine.cpp
    NoteTracker.cpp
    PitchTracker.cpp
)
target_include_directories(nota_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(nota_dsp PUBLIC cxx_std_17)
target_link_libraries(nota_dsp PRIVATE nota_warnings)
