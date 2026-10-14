cmake_minimum_required(VERSION 3.22)
# Set the deployment target before project() initializes the macOS toolchain.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS" FORCE)
endif()
project(SP1200BankCreator VERSION 1.12.3 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_binary_data(BankTemplate SOURCES DemoBanks/EMPTY_.sp12)
juce_add_binary_data(AppLegal NAMESPACE AppLegal HEADER_NAME AppLegal.h SOURCES LICENSE)
add_library(SampleCore Source/Core.cpp Source/NativeBank.cpp)
target_include_directories(SampleCore PUBLIC Source)
target_compile_definitions(SampleCore PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SampleCore PUBLIC juce::juce_audio_utils juce::juce_cryptography BankTemplate)
juce_add_gui_app(SP1200BankCreator PRODUCT_NAME "SP-1200 Bank Creator" BUNDLE_ID "com.sp1200.sampletool"
    ICON_BIG Assets/AppIcon.png ICON_SMALL Assets/AppIcon.png
    PLIST_TO_MERGE [=[<plist version="1.0"><dict>
        <key>CFBundleDocumentTypes</key><array><dict>
            <key>CFBundleTypeName</key><string>SP-1200 Kit</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>LSHandlerRank</key><string>Owner</string>
            <key>LSItemContentTypes</key><array><string>com.sp1200.sampletool.spkit</string></array>
        </dict><dict>
            <key>CFBundleTypeName</key><string>SP-1200 Bank</string>
            <key>CFBundleTypeRole</key><string>Editor</string>
            <key>LSHandlerRank</key><string>Default</string>
            <key>LSItemContentTypes</key><array><string>com.sp1200.sampletool.sp12</string></array>
        </dict></array>
        <key>UTExportedTypeDeclarations</key><array><dict>
            <key>UTTypeIdentifier</key><string>com.sp1200.sampletool.spkit</string>
            <key>UTTypeDescription</key><string>SP-1200 Kit</string>
            <key>UTTypeConformsTo</key><array><string>public.data</string></array>
            <key>UTTypeTagSpecification</key><dict>
                <key>public.filename-extension</key><array><string>spkit</string></array>
            </dict>
        </dict></array>
        <key>UTImportedTypeDeclarations</key><array><dict>
            <key>UTTypeIdentifier</key><string>com.sp1200.sampletool.sp12</string>
            <key>UTTypeDescription</key><string>SP-1200 Bank</string>
            <key>UTTypeConformsTo</key><array><string>public.data</string></array>
            <key>UTTypeTagSpecification</key><dict>
                <key>public.filename-extension</key><array><string>sp12</string></array>
            </dict>
        </dict></array>
    </dict></plist>]=]
    COMPANY_NAME "Numerisch GmbH" COMPANY_COPYRIGHT "Copyright © 2026 Knut Schade / Numerisch GmbH")
target_sources(SP1200BankCreator PRIVATE Source/Main.cpp Source/HelpContent.h Source/Theme.h Source/AppPreferences.h)
target_sources(SP1200BankCreator PRIVATE LICENSE THIRD_PARTY_NOTICES.md)
set_source_files_properties(LICENSE THIRD_PARTY_NOTICES.md PROPERTIES
    HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION Resources)
juce_add_bundle_resources_directory(SP1200BankCreator "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES")
target_compile_definitions(SP1200BankCreator PRIVATE SP_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(SP1200BankCreator PRIVATE SampleCore AppLegal)
juce_add_console_app(CoreTests PRODUCT_NAME "CoreTests")
target_sources(CoreTests PRIVATE tests/CoreTests.cpp)
target_link_libraries(CoreTests PRIVATE SampleCore)
target_compile_definitions(CoreTests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
enable_testing()
add_test(NAME CoreTests COMMAND CoreTests)

add_test(NAME AppStartup COMMAND $<TARGET_FILE:SP1200BankCreator> --smoke-test)
set_tests_properties(AppStartup PROPERTIES TIMEOUT 15)

add_test(NAME MemoryErrorPlayback COMMAND $<TARGET_FILE:SP1200BankCreator> --playback-test)
set_tests_properties(MemoryErrorPlayback PROPERTIES TIMEOUT 15)
