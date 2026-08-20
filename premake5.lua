-- Premake5 script for FastYZ
-- Generates project files for building the FastYZ CLI tool.

workspace "FastYZ"
    staticruntime "on"
    warnings "Extra"
    configurations { "Debug", "Release" }
    platforms { "x64", "x86" }
    location "build"
    startproject "fastyz"

    filter "platforms:x64"
        architecture "x86_64"

    filter "platforms:x86"
        architecture "x86"

    filter "system:linux"
        systemversion "latest"

    filter "system:windows"
        systemversion "latest"

        defines {
            "NOMINMAX",
            "WIN32_LEAN_AND_MEAN",
            "_CRT_SECURE_NO_WARNINGS"
        }

    filter { "system:windows", "not action:vs*" }
        linkoptions { "-static" }

    filter {}

project "fastyz"
    kind "ConsoleApp"
    language "C"
    cdialect "C99"

    targetdir ("bin/%{cfg.buildcfg}/%{cfg.platform}")
    objdir ("obj/%{cfg.buildcfg}/%{cfg.platform}")

    files {
        "fastyz_cli.c",
        "fastyz.c",
        "fastyz.h"
    }

    includedirs { "." }

    filter "configurations:Debug"
        symbols "On"
        optimize "Off"
        omitframepointer "Off"
        defines { "DEBUG" }

    filter "configurations:Release"
        symbols "Off"
        optimize "Speed"
        omitframepointer "On"
        defines { "NDEBUG" }
        linktimeoptimization "On"

    filter { "configurations:Debug", "platforms:x64", "not toolset:gcc" }
        sanitize { "Address", "UndefinedBehavior" }

    filter {}
