
# Copyright (c) 2017, EPFL/Blue Brain Project
#                     Stefan.Eilemann@epfl.ch

set(SIMPLESERVER_SOURCES main.cpp)
set(SIMPLESERVER_LINK_LIBRARIES PRIVATE
  Qt5::Core DeflectQt ${OPENGL_LIBRARIES}  Qt5::Widgets)

if(APPLE)
  common_application(SimpleServer GUI)
else()
  common_application(simpleserver GUI)
endif()
