// swift-tools-version: 5.9
import PackageDescription

// macOS launcher (SwiftUI).  The C++ core and the macOS platform layer are
// shared with the Windows/Linux builds in src/.
let package = Package(
    name: "ReunionLite",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "ReunionLite", targets: ["ReunionLite"]),
    ],
    targets: [
        .target(
            name: "ReunionCore",
            path: "src",
            sources: ["core", "platform/mac"],
            publicHeadersPath: "platform/mac/api/include",
            cxxSettings: [
                .headerSearchPath("."),
                .headerSearchPath("core"),
                .headerSearchPath("platform/mac"),
                .headerSearchPath("platform/mac/api/include"),
            ],
            linkerSettings: [
                .linkedLibrary("iconv"),
            ]
        ),
        .executableTarget(
            name: "ReunionLite",
            dependencies: ["ReunionCore"],
            path: "macos/ReunionLite"
        ),
    ],
    cxxLanguageStandard: .cxx17
)
