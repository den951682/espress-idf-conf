# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS
      main.cpp          # list the source files of this component
      bt_spp_server.cpp
    INCLUDE_DIRS .      # optional, add here public include directories
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
      bt
    PRIV_REQUIRES       # optional, list the private requirements
      bt
      nvs_flash
      vfs
)
