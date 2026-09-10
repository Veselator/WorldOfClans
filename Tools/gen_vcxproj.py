#!/usr/bin/env python3
"""Regenerates PPaTU_Lr2.vcxproj (and its .filters) from the source tree.

The project is plain MSBuild so it opens in Visual Studio with no extra tooling, but
hand-maintaining a file list is a chore; run this after adding or removing sources.
"""
import os
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, "PPaTU_Lr2.vcxproj")
FILTERS = PROJECT + ".filters"

SOURCE_DIRS = ["Source"]
THIRD_PARTY_SOURCES = [r"ThirdParty\volk\volk.c"]

INCLUDE_DIRS = [
    r"$(ProjectDir)Source",
    r"$(ProjectDir)ThirdParty\VulkanHeaders",
    r"$(ProjectDir)ThirdParty\volk",
    r"$(ProjectDir)ThirdParty\stb",
]

DEFINES = ["VK_USE_PLATFORM_WIN32_KHR", "VK_NO_PROTOTYPES", "NOMINMAX", "_CRT_SECURE_NO_WARNINGS"]


def collect(extensions):
    found = []
    for base in SOURCE_DIRS:
        for dirpath, _dirnames, filenames in os.walk(os.path.join(ROOT, base)):
            for name in sorted(filenames):
                if os.path.splitext(name)[1].lower() in extensions:
                    full = os.path.join(dirpath, name)
                    found.append(os.path.relpath(full, ROOT).replace("/", "\\"))
    return sorted(found)


def configuration_block(config, platform):
    debug = config == "Debug"
    return f"""  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='{config}|{platform}'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>true</SDLCheck>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <AdditionalIncludeDirectories>{';'.join(INCLUDE_DIRS)};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>{';'.join(DEFINES)};{'_DEBUG' if debug else 'NDEBUG'};_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <Optimization>{'Disabled' if debug else 'MaxSpeed'}</Optimization>
      {'' if debug else '<FunctionLevelLinking>true</FunctionLevelLinking><IntrinsicFunctions>true</IntrinsicFunctions>'}
    </ClCompile>
    <Link>
      <SubSystem>Console</SubSystem>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <AdditionalDependencies>gdi32.lib;user32.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <PreBuildEvent>
      <Command>"$(ProjectDir)Tools\\compile_shaders.bat" "$(ProjectDir)"</Command>
      <Message>Compiling GLSL shaders to SPIR-V</Message>
    </PreBuildEvent>
  </ItemDefinitionGroup>
"""


def main():
    sources = collect({".cpp", ".c"}) + THIRD_PARTY_SOURCES
    headers = collect({".h", ".hpp"})

    compile_items = "\n".join(f'    <ClCompile Include="{path}" />' for path in sources)
    header_items = "\n".join(f'    <ClInclude Include="{path}" />' for path in headers)

    configs = [("Debug", "x64"), ("Release", "x64")]
    project_configs = "\n".join(
        f"""    <ProjectConfiguration Include="{c}|{p}">
      <Configuration>{c}</Configuration>
      <Platform>{p}</Platform>
    </ProjectConfiguration>"""
        for c, p in configs)

    config_props = "\n".join(
        f"""  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='{c}|{p}'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>{'true' if c == 'Debug' else 'false'}</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
    {'<WholeProgramOptimization>true</WholeProgramOptimization>' if c == 'Release' else ''}
  </PropertyGroup>"""
        for c, p in configs)

    property_sheets = "\n".join(
        f"""  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='{c}|{p}'">
    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>"""
        for c, p in configs)

    definitions = "".join(configuration_block(c, p) for c, p in configs)

    project = f"""<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
{project_configs}
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>18.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{{8a790e08-dc14-48b6-a5dc-ce7a29ab573a}}</ProjectGuid>
    <RootNamespace>WorldOfClans</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
{config_props}
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings" />
  <ImportGroup Label="Shared" />
{property_sheets}
  <PropertyGroup Label="UserMacros" />
  <PropertyGroup>
    <OutDir>$(ProjectDir)Build\\$(Configuration)\\</OutDir>
    <IntDir>$(ProjectDir)Build\\Intermediate\\$(Configuration)\\</IntDir>
    <LocalDebuggerWorkingDirectory>$(ProjectDir)</LocalDebuggerWorkingDirectory>
  </PropertyGroup>
{definitions}  <ItemGroup>
{compile_items}
  </ItemGroup>
  <ItemGroup>
{header_items}
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets" />
</Project>
"""

    with open(PROJECT, "w", encoding="utf-8") as handle:
        handle.write(project)

    # --- filters: mirror the folder layout so the Solution Explorer is navigable ----------
    folders = set()
    for path in sources + headers:
        parts = path.split("\\")[:-1]
        for i in range(1, len(parts) + 1):
            folders.add("\\".join(parts[:i]))

    filter_defs = "\n".join(
        f"""    <Filter Include="{folder}">
      <UniqueIdentifier>{{{uuid.uuid5(uuid.NAMESPACE_DNS, folder)}}}</UniqueIdentifier>
    </Filter>"""
        for folder in sorted(folders))

    def entries(tag, paths):
        rows = []
        for path in paths:
            folder = "\\".join(path.split("\\")[:-1])
            rows.append(f'    <{tag} Include="{path}">\n      <Filter>{folder}</Filter>\n    </{tag}>')
        return "\n".join(rows)

    filters = f"""<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
{filter_defs}
  </ItemGroup>
  <ItemGroup>
{entries("ClCompile", sources)}
  </ItemGroup>
  <ItemGroup>
{entries("ClInclude", headers)}
  </ItemGroup>
</Project>
"""
    with open(FILTERS, "w", encoding="utf-8") as handle:
        handle.write(filters)

    print(f"{len(sources)} sources, {len(headers)} headers")


if __name__ == "__main__":
    main()
