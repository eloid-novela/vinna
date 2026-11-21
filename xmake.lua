set_project("suave")
set_version("0.1.0")
set_xmakever("2.8.5")

add_rules("mode.debug", "mode.release")
set_defaultmode("release")
set_languages("cxx17")
set_warnings("all")

-- Platform independent logic: loudness meter, controller, config, parsers.
target("suave_core")
    set_kind("static")
    add_files(
        "src/core/level_meter.cpp",
        "src/core/controller.cpp",
        "src/core/config.cpp",
        "src/net/http_client.cpp",
        "src/actuators/pactl_parse.cpp"
    )
    add_includedirs("include", "src", {public = true})
    if is_plat("windows", "mingw") then
        add_syslinks("ws2_32", {public = true})
        add_defines("_WIN32_WINNT=0x0601", {public = true})
        if is_plat("windows") then
            add_defines("_CRT_SECURE_NO_WARNINGS", {public = true})
        end
    end

-- Command line tool.
target("suave")
    set_kind("binary")
    add_deps("suave_core")
    add_files(
        "src/app/main.cpp",
        "src/app/runner.cpp",
        "src/capture/miniaudio_capture.cpp",
        "src/capture/miniaudio_impl.c",
        "src/actuators/factory.cpp",
        "src/actuators/vlc_http_actuator.cpp",
        "src/actuators/pulse_actuator.cpp",
        "src/actuators/wasapi_actuator.cpp"
    )
    add_includedirs("third_party")
    add_defines("SUAVE_VERSION=\"0.1.0\"")
    if is_plat("windows", "mingw") then
        add_syslinks("ole32")
        if is_plat("mingw") then
            add_ldflags("-static", {force = true})
        end
    else
        add_syslinks("pthread", "dl", "m")
    end

-- Unit tests: `xmake build suave_tests && xmake run suave_tests`
target("suave_tests")
    set_kind("binary")
    set_default(false)
    add_deps("suave_core")
    add_files("tests/test_main.cpp")
    add_tests("default")
