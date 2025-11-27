# Shared Hardware component
idf_component_register(SRCS "shared_hardware.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver freertos hardware_manager
                    )