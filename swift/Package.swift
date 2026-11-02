// swift-tools-version: 6.3
// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.
import Foundation
import PackageDescription
let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
let artifact = "Artifacts/CRMRender.xcframework"
let native: Target = FileManager.default.fileExists(atPath: root.appendingPathComponent(artifact).path)
    ? .binaryTarget(name: "CRMRender", path: artifact)
    : .systemLibrary(name: "CRMRender", pkgConfig: "rmrender-apple-full")
let package = Package(
    name: "RMRenderC",
    platforms: [.macOS(.v15), .iOS(.v18)],
    products: [.library(name: "RMRenderC", targets: ["RMRenderC"])],
    targets: [native, .target(name: "RMRenderC", dependencies: ["CRMRender"]),
              .testTarget(name: "RMRenderCTests", dependencies: ["RMRenderC"])]
)
