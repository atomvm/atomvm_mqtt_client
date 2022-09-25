COMPONENT_ADD_INCLUDEDIRS := ports/include
COMPONENT_ADD_LDFLAGS = -Wl,--whole-archive -l$(COMPONENT_NAME) -Wl,--no-whole-archive
COMPONENT_SRCDIRS := ports
CXXFLAGS += -fno-rtti
