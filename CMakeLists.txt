set(COMPONENT_SRCS
  ports/atomvm_mqtt.c
  )

set(COMPONENT_ADD_INCLUDEDIRS
  ports/include
  )

set(COMPONENT_REQUIRES nifs)

register_component()
