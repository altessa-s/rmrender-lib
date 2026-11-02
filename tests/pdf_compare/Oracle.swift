// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.
import CoreGraphics
import CryptoKit
// Compiled alongside the rmrender-swift sources. Only resource lookup and I/O
// are supplied here; neither routing, PDF parsing nor drawing is reimplemented.
import Foundation
import RMLines

extension Bundle {
  static var module: Bundle {
    Bundle(path: ProcessInfo.processInfo.environment["RMR_REFERENCE_BUNDLE"]!)!
  }
}
struct LocalBlobs: PageBlobs {
  let directory: URL
  func data(hash: String, name: String, owner: String?) async throws -> Data {
    try Data(contentsOf: directory.appendingPathComponent(name))
  }
  func file(
    hash: String, name: String, owner: String?, size: Int64,
    onProgress: @escaping @Sendable (Int64, Int64) -> Void
  ) async throws -> BlobFile {
    BlobFile(url: directory.appendingPathComponent(name), isTemporary: false)
  }
}
private func emit(_ object: [String: Any]) throws {
  let data = try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
  print(String(decoding: data, as: UTF8.self))
}
private func hash(_ data: Data) -> String {
  SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
}
private func object(_ dictionary: CGPDFDictionaryRef, _ name: String) -> CGPDFObjectRef? {
  var value: CGPDFObjectRef?
  return CGPDFDictionaryGetObject(dictionary, name, &value) ? value : nil
}
private func array(_ value: CGPDFArrayRef, _ index: Int) -> CGPDFObjectRef? {
  var result: CGPDFObjectRef?
  return CGPDFArrayGetObject(value, index, &result) ? result : nil
}
private func semantic(_ value: CGPDFObjectRef?, pages: [CGPDFDictionaryRef], depth: Int = 0) -> Any
{
  guard let value else { return NSNull() }
  if depth > 12 { return "depth-limit" }
  switch CGPDFObjectGetType(value) {
  case .null: return NSNull()
  case .boolean:
    var v: CGPDFBoolean = 0
    _ = CGPDFObjectGetValue(value, .boolean, &v)
    return v != 0
  case .integer:
    var v: CGPDFInteger = 0
    _ = CGPDFObjectGetValue(value, .integer, &v)
    return v
  case .real:
    var v: CGPDFReal = 0
    _ = CGPDFObjectGetValue(value, .real, &v)
    return v
  case .name:
    var v: UnsafePointer<CChar>?
    _ = CGPDFObjectGetValue(value, .name, &v)
    return v.map { String(cString: $0) } ?? ""
  case .string:
    var v: CGPDFStringRef?
    _ = CGPDFObjectGetValue(value, .string, &v)
    guard let v else { return "" }
    let count = CGPDFStringGetLength(v)
    return count == 0
      ? "" : Data(bytes: CGPDFStringGetBytePtr(v)!, count: count).base64EncodedString()
  case .array:
    var v: CGPDFArrayRef?
    _ = CGPDFObjectGetValue(value, .array, &v)
    guard let v else { return [] }
    return (0..<min(100, CGPDFArrayGetCount(v))).map {
      semantic(array(v, $0), pages: pages, depth: depth + 1)
    }
  case .dictionary:
    var v: CGPDFDictionaryRef?
    _ = CGPDFObjectGetValue(value, .dictionary, &v)
    guard let v else { return [:] }
    if let index = pages.firstIndex(of: v) { return ["pageIndex": index] }
    // Deliberate projection: compare document semantics, not newly assigned
    // object/resource IDs or producer-specific compression/metadata.
    let keys = [
      "Type", "Subtype", "FT", "T", "V", "Rect", "Contents", "ByteRange", "SubFilter",
      "Fields", "Kids", "NeedAppearances", "Names", "EmbeddedFiles", "F", "EF", "UF",
      "S", "URI", "A", "Dest", "D", "Title", "Count", "First", "Next", "Custom",
    ]
    var result: [String: Any] = [:]
    for key in keys where object(v, key) != nil {
      result[key] = semantic(object(v, key), pages: pages, depth: depth + 1)
    }
    return result
  case .stream:
    var v: CGPDFStreamRef?
    _ = CGPDFObjectGetValue(value, .stream, &v)
    guard let v else { return NSNull() }
    var format = CGPDFDataFormat.raw
    guard let bytes = CGPDFStreamCopyData(v, &format) else { return "undecodable-stream" }
    return ["sha256": hash(bytes as Data), "size": CFDataGetLength(bytes)] as [String: Any]
  @unknown default: return "unknown-object"
  }
}
private func rectangle(_ box: CGRect) -> [Double] { [box.minX, box.minY, box.width, box.height] }
// Normalise the selected rectangle explicitly. CoreGraphics may intersect a
// drawing-transform request with CropBox even when .mediaBox was requested.
// Both engines must be inspected against the same, stated viewport.
private func rasterTransform(_ page: CGPDFPage, _ box: CGPDFBox, _ width: Int, _ height: Int)
  -> CGAffineTransform
{
  let native = page.getDrawingTransform(
    .mediaBox, rect: CGRect(x: 0, y: 0, width: 100, height: 100), rotate: 0,
    preserveAspectRatio: false)
  let bounds = page.getBoxRect(box).applying(native)
  let sx = Double(width) / bounds.width
  let sy = Double(height) / bounds.height
  return native.concatenating(
    CGAffineTransform(a: sx, b: 0, c: 0, d: sy, tx: -bounds.minX * sx, ty: -bounds.minY * sy))
}
private func inspect(_ input: URL, _ directory: URL) throws {
  guard let provider = CGDataProvider(url: input as CFURL), let document = CGPDFDocument(provider),
    let catalog = document.catalog
  else {
    try emit(["ok": false, "error": "CoreGraphics cannot open PDF"])
    return
  }
  guard document.numberOfPages > 0 else {
    try emit(["ok": false, "error": "no readable PDF pages"])
    return
  }
  let pages = (1...document.numberOfPages).compactMap { document.page(at: $0) }
  let dictionaries = pages.map { $0.dictionary! }
  var rows: [[String: Any]] = []
  try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
  for (index, page) in pages.enumerated() {
    let width = 320
    let height = 480
    var pixels = [UInt8](repeating: 255, count: width * height * 4)
    try pixels.withUnsafeMutableBytes { bytes in
      let space = CGColorSpaceCreateDeviceRGB()
      guard
        let cg = CGContext(
          data: bytes.baseAddress, width: width, height: height,
          bitsPerComponent: 8, bytesPerRow: width * 4, space: space,
          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
      else { throw CocoaError(.fileReadUnknown) }
      cg.setFillColor(CGColor(gray: 1, alpha: 1))
      cg.fill(CGRect(x: 0, y: 0, width: width, height: height))
      cg.concatenate(rasterTransform(page, .mediaBox, width, height))
      cg.drawPDFPage(page)
    }
    try Data(pixels).write(to: directory.appendingPathComponent("page-\(index).rgba"))
    var row: [String: Any] = [
      "media": rectangle(page.getBoxRect(.mediaBox)), "crop": rectangle(page.getBoxRect(.cropBox)),
      "art": rectangle(page.getBoxRect(.artBox)), "rotation": page.rotationAngle,
      "annotations": semantic(object(page.dictionary!, "Annots"), pages: dictionaries),
      "custom": semantic(object(page.dictionary!, "Custom"), pages: dictionaries),
    ]
    let media = page.getBoxRect(.mediaBox)
    row["displaySize"] =
      page.rotationAngle % 180 == 0 ? [media.width, media.height] : [media.height, media.width]
    row["pixelsSHA256"] = hash(Data(pixels))
    var cropped = [UInt8](repeating: 255, count: width * height * 4)
    try cropped.withUnsafeMutableBytes { bytes in
      guard
        let cg = CGContext(
          data: bytes.baseAddress, width: width, height: height,
          bitsPerComponent: 8, bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
      else { throw CocoaError(.fileReadUnknown) }
      cg.setFillColor(CGColor(gray: 1, alpha: 1))
      cg.fill(CGRect(x: 0, y: 0, width: width, height: height))
      cg.concatenate(rasterTransform(page, .cropBox, width, height))
      cg.drawPDFPage(page)
    }
    try Data(cropped).write(to: directory.appendingPathComponent("page-\(index)-crop.rgba"))
    rows.append(row)
  }
  var result: [String: Any] = ["ok": true, "pages": rows, "pixelWidth": 320, "pixelHeight": 480]
  for key in ["AcroForm", "Names", "Outlines", "Metadata", "Custom", "Perms"] {
    result[key] = semantic(object(catalog, key), pages: dictionaries)
  }
  try emit(result)
}
@main struct Oracle {
  static func main() async {
    let args = CommandLine.arguments
    do {
      switch args[1] {
      case "inspect":
        try inspect(URL(fileURLWithPath: args[2]), URL(fileURLWithPath: args[3]))
        return
      case "writer":
        let content = try Data(contentsOf: URL(fileURLWithPath: args[3]))
        let box = args[5] == "expand" ? CGRect(x: -20, y: -10, width: 240, height: 340) : nil
        try PDFOverlayWriter().write(
          original: URL(fileURLWithPath: args[2]),
          overlays: [
            .init(
              pageIndex: 0, content: content,
              states: [.init(name: "TestAlpha", blendMode: "Multiply", alpha: 0.65)], box: box)
          ],
          to: URL(fileURLWithPath: args[4]))
      case "document":
        let directory = URL(fileURLWithPath: args[2])
        let names = try String(
          contentsOf: directory.appendingPathComponent("manifest.txt"), encoding: .utf8
        ).split(separator: "\n").map(String.init)
        let files = try names.map { name in
          DocumentFile(
            hash: name, name: name,
            size: Int64(try Data(contentsOf: directory.appendingPathComponent(name)).count))
        }
        let cap = Int(args[6])!
        try await AnnotatedPDFRenderer(blobs: LocalBlobs(directory: directory)).renderPDF(
          documentID: args[5], index: DocumentFiles(files: files),
          to: URL(fileURLWithPath: args[3]),
          allowOverlay: args[4] == "auto", maximumPages: cap == 0 ? nil : cap)
      default: throw CocoaError(.featureUnsupported)
      }
      try emit(["ok": true])
    } catch { try? emit(["ok": false, "error": String(describing: error)]) }
  }
}
