# FLAKEWM only needs the remote-subsurface protocol from this package while
# configuring its vendored Waylib snapshot.
get_filename_component(TREELAND_PROTOCOLS_DATA_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../../protocols" ABSOLUTE
)
set(TreelandProtocols_VERSION 0.6.0)
set(TREELAND_PROTOCOLS_VERSION 0.6.0)
set(TreelandProtocols_FOUND TRUE)
