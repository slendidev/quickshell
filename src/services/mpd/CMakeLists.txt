qt_add_library(quickshell-service-mpd STATIC
mpd.cpp
)

qt_add_qml_module(quickshell-service-mpd
URI Quickshell.Services.Mpd
  VERSION 0.1
DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-service-mpd Quickshell)

install_qml_module(quickshell-service-mpd)

target_link_libraries(quickshell-service-mpd PRIVATE Qt::Qml Qt::Network)

qs_module_pch(quickshell-service-mpd)

target_link_libraries(quickshell PRIVATE quickshell-service-mpdplugin)
