
# Copyright (c) 2013-2015, EPFL/Blue Brain Project
#                     Raphael Dumusc <raphael.dumusc@epfl.ch>
#                     Daniel Nachbaur <daniel.nachbaur@epfl.ch>

add_subdirectory(DesktopStreamer)

if(TARGET DeflectQt)
  add_subdirectory(QmlStreamer)
endif()

if(GLUT_FOUND)
  add_subdirectory(SimpleStreamer)
endif()
