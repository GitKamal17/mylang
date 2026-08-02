// scripts/demo_render.swift — renders one MyLang demo frame to a PNG.
//
// Compiled once by make_demo_gif.sh (swiftc -O) and invoked per frame:
//
//   demo_render <out.png> <command> <textfile|-> <window:0|1> <ballx> <bally>
//
// The command line is drawn green like a shell prompt; the text file body is
// drawn in white below it (each line one screen row). With <window> = 1 it
// also draws the game window (black canvas, border, caption) and a green
// ball at (ballx, bally).
//
// Uses only AppKit / CoreGraphics, which ship with macOS — no third-party
// dependencies required. We wrap a raw CGContext in a flipped
// NSGraphicsContext so drawing uses top-left origin coordinates and text
// renders right-side up.

import AppKit
import CoreGraphics
import Foundation

let args = CommandLine.arguments
guard args.count >= 7 else {
    FileHandle.standardError.write(
        Data("usage: demo_render out command textfile window ballx bally\n".utf8))
    exit(1)
}

let outPath  = args[1]
let cmd      = args[2]
let textPath = args[3]
let windowOn = args[4] == "1"
let ballX    = Double(args[5]) ?? 0
let ballY    = Double(args[6]) ?? 0

let W: Int    = 880
let H: Int    = 560
let Wf: CGFloat = CGFloat(W)
let Hf: CGFloat = CGFloat(H)

// ---- raw bitmap context (top-left origin via flipped NSGraphicsContext) ---
guard let cg = CGContext(data: nil,
                         width: W, height: H,
                         bitsPerComponent: 8,
                         bytesPerRow: 0,
                         space: CGColorSpaceCreateDeviceRGB(),
                         bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
else { exit(1) }

NSGraphicsContext.saveGraphicsState()
let ns = NSGraphicsContext(cgContext: cg, flipped: true)
NSGraphicsContext.current = ns

func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: r, green: g, blue: b, alpha: a)
}

let bgColor = rgb(0.05, 0.07, 0.09)
let green   = rgb(0.29, 0.96, 0.15)
let white   = rgb(0.90, 0.90, 0.90)
let dim     = rgb(0.54, 0.58, 0.65)
let border  = rgb(0.19, 0.21, 0.24)

// ---- background ----------------------------------------------------------
bgColor.setFill()
NSBezierPath(rect: NSRect(x: 0, y: 0, width: Wf, height: Hf)).fill()

// ---- command line (green, like a shell prompt) ----------------------------
let cmdAttrs: [NSAttributedString.Key: Any] = [
    .font: NSFont.monospacedSystemFont(ofSize: 22, weight: .medium),
    .foregroundColor: green,
]
(cmd as NSString).draw(at: NSPoint(x: 24, y: 20), withAttributes: cmdAttrs)

// ---- body text (real mylang output, line by line) --------------------------
if textPath != "-", let body = try? String(contentsOfFile: textPath, encoding: .utf8) {
    let attrs: [NSAttributedString.Key: Any] = [
        .font: NSFont.monospacedSystemFont(ofSize: 15, weight: .regular),
        .foregroundColor: white,
    ]
    var y: CGFloat = 72
    for line in body.components(separatedBy: "\n") {
        (line as NSString).draw(at: NSPoint(x: 24, y: y), withAttributes: attrs)
        y += 24
    }
}

// ---- game window phase ------------------------------------------------------
if windowOn {
    let win = NSRect(x: 40, y: 60, width: 800, height: 440)
    NSColor.black.setFill()
    NSBezierPath(rect: win).fill()
    border.setStroke()
    let outline = NSBezierPath(rect: win)
    outline.lineWidth = 3
    outline.stroke()

    let captionAttrs: [NSAttributedString.Key: Any] = [
        .font: NSFont.monospacedSystemFont(ofSize: 15, weight: .regular),
        .foregroundColor: dim,
    ]
    ("My Game — mylang run examples/game.vibes" as NSString)
        .draw(at: NSPoint(x: 60, y: 472), withAttributes: captionAttrs)
    ("use arrow keys to move" as NSString)
        .draw(at: NSPoint(x: 60, y: 498), withAttributes: captionAttrs)

    green.setFill()
    NSBezierPath(ovalIn: NSRect(x: CGFloat(ballX), y: CGFloat(ballY),
                                width: 22, height: 22)).fill()
}

NSGraphicsContext.restoreGraphicsState()

// ---- encode PNG -------------------------------------------------------------
guard let cgImage = cg.makeImage() else {
    FileHandle.standardError.write(Data("error: could not make CGImage\n".utf8))
    exit(1)
}
let rep = NSBitmapImageRep(cgImage: cgImage)
guard let png = rep.representation(using: .png, properties: [:]) else {
    FileHandle.standardError.write(Data("error: could not encode PNG\n".utf8))
    exit(1)
}
try! png.write(to: URL(fileURLWithPath: outPath))
