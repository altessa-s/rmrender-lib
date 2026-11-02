// Copyright 2026 ALTESSA SOLUTIONS INC.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.
import CRMRender
import CoreGraphics
import Foundation

public struct RenderFailure: Error, LocalizedError, Sendable {
  public let message: String
  public var errorDescription: String? { message }
}
public struct DocumentReport: Sendable {
  public let documentPages, renderedPages, skippedBlocks, refusedSources: Int
  public let preservedLinks, droppedLinks: Int
}
public struct RenderedDocument: Sendable {
  public let data: Data
  public let report: DocumentReport
}
public enum Paper: Sendable {
  case a4, letter, tablet
  fileprivate var native: rmr_paper {
    switch self {
    case .a4: RMR_PAPER_A4
    case .letter: RMR_PAPER_LETTER
    case .tablet: RMR_PAPER_TABLET
    }
  }
}
public struct DocumentFile: Sendable {
  public let name: String
  public let data: Data
  public init(name: String, data: Data) {
    self.name = name
    self.data = data
  }
}
/// One synchronous renderer owns its font/template stores. Create separate
/// instances for concurrent work; neither native contexts nor global caches
/// are shared. C limits apply to every operation.
public final class Renderer {
  private let fonts: OpaquePointer
  private let templates: OpaquePointer
  private var limits: rmr_limits
  public init(fontDirectory: URL, templateDirectory: URL, maximumPlanBytes: Int? = nil) throws {
    var limits = rmr_limits_default()
    if let maximumPlanBytes {
      guard maximumPlanBytes > 0 else { throw RenderFailure(message: "Invalid memory limit") }
      limits.maximum_plan_bytes = maximumPlanBytes
    }
    var fonts: OpaquePointer?
    var templates: OpaquePointer?
    var error = rmr_error()
    try Self.check(
      rmr_apple_fonts_create_directory(fontDirectory.path, &limits, &fonts, &error), &error)
    do {
      try Self.check(
        rmr_template_store_create(templateDirectory.path, &limits, &templates, &error), &error)
    } catch {
      rmr_apple_fonts_free(fonts)
      throw error
    }
    self.fonts = fonts!
    self.templates = templates!
    self.limits = limits
  }
  public convenience init(resourceDirectory: URL, maximumPlanBytes: Int? = nil) throws {
    try self.init(
      fontDirectory: resourceDirectory.appendingPathComponent("fonts"),
      templateDirectory: resourceDirectory.appendingPathComponent("templates"),
      maximumPlanBytes: maximumPlanBytes
    )
  }
  deinit {
    rmr_template_store_free(templates)
    rmr_apple_fonts_free(fonts)
  }
  private var resources: rmr_page_resources {
    var result = rmr_page_resources()
    result.template = rmr_template_store_resolve
    result.template_context = UnsafeMutableRawPointer(templates)
    return result
  }
  private static func check(_ status: rmr_status, _ error: inout rmr_error) throws {
    if status == RMR_ERR_CANCELLED { throw CancellationError() }
    guard status == RMR_OK else {
      let message = withUnsafePointer(to: &error.message) {
        $0.withMemoryRebound(to: CChar.self, capacity: 192) { String(cString: $0) }
      }
      throw RenderFailure(
        message: message.isEmpty ? String(cString: rmr_status_string(status)) : message)
    }
  }
  private func withParsed<T>(_ data: Data, _ body: (UnsafeMutablePointer<rm_page>) throws -> T)
    throws -> T
  {
    try Task.checkCancellation()
    var page: UnsafeMutablePointer<rm_page>?
    var error = rm_error()
    let status = data.withUnsafeBytes { bytes in
      rm_parse_page(bytes.bindMemory(to: UInt8.self).baseAddress, bytes.count, &page, &error)
    }
    guard status == RM_OK, let page else {
      throw RenderFailure(message: String(cString: rm_status_string(status)))
    }
    defer { rm_page_free(page) }
    return try body(page)
  }
  private func canvas(_ page: UnsafePointer<rm_page>, _ size: CGSize?) throws -> rmr_point {
    if let size { return rmr_point(x: size.width, y: size.height) }
    var input: UnsafePointer<rm_page>? = page
    var result = rmr_point()
    var error = rmr_error()
    try Self.check(
      rmr_lines_document_canvas(&input, 1, rmr_point(), false, &limits, &result, &error), &error)
    return result
  }
  /// Overload for hosts that already hold a parsed page; the borrowed page and
  /// optional resolver context remain valid only for this call.
  public func pagePDF(
    borrowing page: UnsafePointer<rm_page>, canvas size: CGSize? = nil,
    paper: Paper = .a4, imageResolver: rmr_asset_resolve_fn? = nil,
    imageContext: UnsafeMutableRawPointer? = nil
  ) throws -> Data {
    let size = try canvas(page, size)
    var resources = self.resources
    var plan: OpaquePointer?
    var error = rmr_error()
    resources.image = imageResolver
    resources.image_context = imageContext
    try Self.check(
      rmr_apple_page_plan(
        page, size, RMR_EDGE_RIBBON, 0, &resources, fonts, &limits, &plan, &error), &error)
    defer { rmr_plan_free(plan) }
    var sheet = rmr_sheet()
    try Self.check(
      rmr_paper_sheet(paper.native, size.x, size.y, rmr_plan_viewport(plan), &sheet, &error), &error
    )
    var output = rmr_buffer()
    try Self.check(
      rmr_apple_pdf_sheets(
        &plan, &sheet, 1, &limits, { _ in Task.isCancelled }, nil, &output, &error), &error)
    defer { rmr_buffer_free(&output) }
    return Data(bytes: output.bytes!, count: output.size)
  }
  public func pagePDF(_ data: Data, canvas: CGSize? = nil, paper: Paper = .a4) throws -> Data {
    try withParsed(data) { try pagePDF(borrowing: $0, canvas: canvas, paper: paper) }
  }
  public func pageImage(
    borrowing page: UnsafePointer<rm_page>, canvas size: CGSize? = nil,
    scale: Double = 1, imageResolver: rmr_asset_resolve_fn? = nil,
    imageContext: UnsafeMutableRawPointer? = nil
  ) throws -> CGImage {
    guard scale.isFinite, scale > 0 else { throw RenderFailure(message: "Invalid bitmap scale") }
    let size = try canvas(page, size)
    var resources = self.resources
    var plan: OpaquePointer?
    var error = rmr_error()
    resources.image = imageResolver
    resources.image_context = imageContext
    try Self.check(
      rmr_apple_page_plan(page, size, RMR_EDGE_UNCUT, 0, &resources, fonts, &limits, &plan, &error),
      &error)
    defer { rmr_plan_free(plan) }
    var sheet = rmr_sheet()
    var bitmap = rmr_bitmap()
    try Self.check(
      rmr_sheet_create(rmr_plan_viewport(plan), size.x, size.y, 1, &sheet, &error), &error)
    sheet.width = max(1, (sheet.width * scale).rounded())
    sheet.height = max(1, (sheet.height * scale).rounded())
    sheet.transform.a *= scale
    sheet.transform.b *= scale
    sheet.transform.c *= scale
    sheet.transform.d *= scale
    sheet.transform.tx *= scale
    sheet.transform.ty *= scale
    try Self.check(
      rmr_apple_bitmap_on_sheet(
        plan, &sheet, rmr_color(red: 1, green: 1, blue: 1, alpha: 1), &limits,
        { _ in Task.isCancelled }, nil,
        &bitmap, &error), &error)
    defer { rmr_bitmap_free(&bitmap) }
    let data = Data(bytes: bitmap.pixels!, count: bitmap.height * bitmap.stride)
    guard let provider = CGDataProvider(data: data as CFData),
      let color = CGColorSpace(name: CGColorSpace.sRGB),
      let image = CGImage(
        width: bitmap.width, height: bitmap.height, bitsPerComponent: 8, bitsPerPixel: 32,
        bytesPerRow: bitmap.stride, space: color,
        bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
        provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    else { throw RenderFailure(message: "Bitmap could not be created") }
    return image
  }
  public func pageImage(_ data: Data, canvas: CGSize? = nil, scale: Double = 1) throws -> CGImage {
    try withParsed(data) { try pageImage(borrowing: $0, canvas: canvas, scale: scale) }
  }
  private func render(
    _ archive: inout rmd_archive, documentID: String, paper: Paper, maximumPages: Int?,
    preservingPDF: Bool
  ) throws
    -> RenderedDocument
  {
    var document: OpaquePointer?
    var error = rmr_error()
    try Self.check(
      rmr_archive_document_create(&archive, documentID, true, &limits, &document, &error), &error)
    defer { rmr_archive_document_free(document) }
    var resources = self.resources
    var output = rmr_buffer()
    var report = rmr_document_report()
    let renderPDF = preservingPDF ? rmr_apple_document_pdf_preserving : rmr_apple_document_pdf
    try Self.check(
      renderPDF(
        document, &resources, fonts, paper.native, maximumPages.map { max($0, 1) } ?? 0,
        &limits, { _ in Task.isCancelled }, nil, &output, &report, &error), &error)
    defer { rmr_buffer_free(&output) }
    return RenderedDocument(
      data: Data(bytes: output.bytes!, count: output.size),
      report: DocumentReport(
        documentPages: report.document_pages, renderedPages: report.rendered_pages,
        skippedBlocks: report.skipped_blocks, refusedSources: report.refused_sources,
        preservedLinks: report.pdf.links_preserved, droppedLinks: report.pdf.links_dropped))
  }
  /// With preservingPDF, imported PDFs use a strict append-only update.
  /// Unsupported structures/page changes throw; no redraw fallback. The original
  /// signed revision remains intact, but the new revision is not signed.
  public func documentPDF(
    _ archiveData: Data, documentID: String, paper: Paper = .a4, maximumPages: Int? = nil,
    preservingPDF: Bool = false
  )
    throws -> RenderedDocument
  {
    var archive = rmd_archive()
    try archiveData.withUnsafeBytes { bytes in
      var reader: OpaquePointer?
      var error = rmd_error()
      let opened = rmd_reader_open(
        rmd_bytes(bytes: bytes.bindMemory(to: UInt8.self).baseAddress, size: bytes.count), nil,
        &reader, &error)
      guard opened == RMD_OK else {
        throw RenderFailure(message: String(cString: rmd_status_string(opened)))
      }
      defer { rmd_reader_close(reader) }
      let unpacked = rmd_reader_unpack(reader, nil, nil, &archive, &error)
      guard unpacked == RMD_OK else {
        throw RenderFailure(message: String(cString: rmd_status_string(unpacked)))
      }
    }
    defer { rmd_archive_free(&archive) }
    return try render(
      &archive, documentID: documentID, paper: paper, maximumPages: maximumPages,
      preservingPDF: preservingPDF)
  }
  /// Cloud/cache hosts can supply fetched files without packing a ZIP first.
  public func documentPDF(
    files: [DocumentFile], documentID: String, paper: Paper = .a4, maximumPages: Int? = nil,
    preservingPDF: Bool = false
  )
    throws -> RenderedDocument
  {
    let names = files.map { Array($0.name.utf8CString) }
    let payloads = files.map { $0.data as NSData }
    let storage = names.map { name -> UnsafeMutablePointer<CChar> in
      let p = UnsafeMutablePointer<CChar>.allocate(capacity: name.count)
      p.initialize(from: name, count: name.count)
      return p
    }
    defer {
      for (i, p) in storage.enumerated() {
        p.deinitialize(count: names[i].count)
        p.deallocate()
      }
    }
    let native = files.indices.map { i in
      rmd_file(
        name: rmd_str(bytes: storage[i], length: names[i].count - 1),
        data: rmd_bytes(
          bytes: payloads[i].bytes.assumingMemoryBound(to: UInt8.self), size: payloads[i].length))
    }
    return try withExtendedLifetime(payloads) {
      try native.withUnsafeBufferPointer { files in
        var archive = rmd_archive(files: files.baseAddress, count: files.count, _storage: nil)
        return try render(
          &archive, documentID: documentID, paper: paper, maximumPages: maximumPages,
          preservingPDF: preservingPDF)
      }
    }
  }
}
