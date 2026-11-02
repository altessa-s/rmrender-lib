// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.
import CoreGraphics
import Foundation
import Testing

@testable import RMRenderC

private func renderer() throws -> Renderer {
  var root = URL(fileURLWithPath: #filePath)
  for _ in 0..<4 { root.deleteLastPathComponent() }
  return try Renderer(resourceDirectory: root.appendingPathComponent("resources"))
}
private let blank = Data("reMarkable .lines file, version=6          ".utf8)
@Test func pageAndRounding() throws {
  let render = try renderer()
  let image = try render.pageImage(blank, canvas: CGSize(width: 100.4, height: 200.6), scale: 1.5)
  #expect(image.width == 150 && image.height == 302)
  let bytes = try render.pagePDF(blank, paper: .tablet)
  let provider = try #require(CGDataProvider(data: bytes as CFData))
  #expect(CGPDFDocument(provider)?.numberOfPages == 1)
}
@Test func documentFilesAndTombstones() throws {
  let id = "11111111-1111-1111-1111-111111111111"
  let live = "22222222-2222-2222-2222-222222222222"
  let dead = "33333333-3333-3333-3333-333333333333"
  let metadata = Data(
    "{\"cPages\":{\"pages\":[{\"id\":\"\(live)\"},{\"id\":\"\(dead)\",\"deleted\":{\"value\":1}}]}}"
      .utf8)
  let result = try renderer().documentPDF(
    files: [
      .init(name: id + ".content", data: metadata),
      .init(name: id + "/" + dead + ".rm", data: blank),
    ], documentID: id)
  #expect(result.report.documentPages == 1 && result.report.renderedPages == 1)
  let provider = try #require(CGDataProvider(data: result.data as CFData))
  #expect(CGPDFDocument(provider)?.numberOfPages == 1)
}
@Test func malformedPageAndScaleAreErrors() throws {
  let render = try renderer()
  #expect(throws: RenderFailure.self) { try render.pagePDF(Data([1, 2, 3])) }
  #expect(throws: RenderFailure.self) { try render.pageImage(blank, scale: .nan) }
}
@Test func preservingImportedPDF() throws {
  let render = try renderer()
  let original = try render.pagePDF(blank, paper: .tablet)
  let id = "11111111-1111-1111-1111-111111111111"
  let result = try render.documentPDF(
    files: [.init(name: id + ".pdf", data: original)], documentID: id, preservingPDF: true)
  #expect(result.data == original)
  #expect(result.report.documentPages == 1 && result.report.renderedPages == 1)
}
