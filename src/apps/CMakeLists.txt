# CLI applications.
foreach(app feedsim replay_app feedhandler)
    add_executable(mdfh_${app} ${app}.cpp)
    target_link_libraries(mdfh_${app} PRIVATE mdfh)
endforeach()

# Give the apps their conventional names.
set_target_properties(mdfh_replay_app PROPERTIES OUTPUT_NAME mdfh_replay)
