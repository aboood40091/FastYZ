-- Premake5 script for FastYZ
-- Generates project files for building the FastYZ CLI tool.

workspace "FastYZ"
    configurations { "Debug", "Release" }
    platforms { "x64", "x86" }
    location "build"
    startproject "fastyz"

    filter "platforms:x64"
        architecture "x86_64"
    filter "platforms:x86"
        architecture "x86"
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

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }

    filter "action:vs*"
        -- MSVC warns on unknown pragmas (e.g., GCC diagnostic pragmas)
        disablewarnings { "4068" }

    filter "configurations:Debug"
        symbols "On"
        optimize "Off"
        defines { "DEBUG" }

    filter "configurations:Release"
        symbols "Off"
        optimize "Speed"
        defines { "NDEBUG" }

    filter {}
