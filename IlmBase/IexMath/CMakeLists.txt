# pstanczyk@ilm.com

ADD_LIBRARY ( IexMath
  IexMathFloatExc.cpp  
  IexMathFpu.cpp
  )

IF(BUILD_SHARED_LIBS)
  ADD_DEFINITIONS(-DIEXMATH_EXPORTS)
ENDIF()

TARGET_LINK_LIBRARIES(IexMath Iex)

INSTALL ( TARGETS
  IexMath
  DESTINATION
  lib
)

INSTALL ( FILES 
  IexMathFloatExc.h  
  IexMathFpu.h  
  IexMathIeeeExc.h
  DESTINATION
  include/OpenEXR
)
