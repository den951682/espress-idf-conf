# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

file(GLOB PROTO_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/proto-model/*.pb.c")
file(GLOB PROTO_GOOGLE_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/proto-model/google/protobuf/*.pb.c")

idf_component_register(
    SRCS
      main.cpp          # list the source files of this component
      message_type.cpp
      bt_spp_server.cpp
      fd_connection.cpp
      serial_line_reader.cpp
      protocol/ecdh_aes_protocol.cpp
      protocol/passphrase_aes_protocol.cpp
      crypto_ecdh_aes.cpp
      parameter_store.cpp
      parameter_sync.cpp
      utils.cpp
      ${PROTO_GOOGLE_SRCS}  # <-- Google descriptor first
      ${PROTO_SRCS}         # <-- Then all the rest
    INCLUDE_DIRS 
      .     
      proto-model
      proto-model/google/protobuf
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES            # optional, list the public requirements (component names)
      bt
    PRIV_REQUIRES       # optional, list the private requirements
      bt
      nvs_flash
      vfs
      driver
      mbedtls
)
