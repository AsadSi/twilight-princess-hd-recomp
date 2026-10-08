// Records the game window, and nothing else, as numbered PNG frames (the largest on-screen window of the process).
// Usage: wincap <pid> <seconds> <out dir> [width]
// Uses ScreenCaptureKit with a single-window filter, so other windows and the desktop are never captured.
// Writes frames/NNNN.png (scaled to `width`, default 640) and frames.txt with each frame's capture time;
// ScreenCaptureKit only delivers a frame when the window's content changed.
import CoreImage
import Foundation
import ImageIO
import ScreenCaptureKit
import UniformTypeIdentifiers

let args = CommandLine.arguments
guard args.count >= 4, let pid = Int32(args[1]), let seconds = Double(args[2]) else {
    FileHandle.standardError.write("usage: wincap <pid> <seconds> <out dir> [width]\n".data(using: .utf8)!)
    exit(2)
}
let outDir = URL(fileURLWithPath: args[3])
let width = args.count > 4 ? Int(args[4]) ?? 640 : 640
let framesDir = outDir.appendingPathComponent("frames")
try FileManager.default.createDirectory(at: framesDir, withIntermediateDirectories: true)

final class Recorder: NSObject, SCStreamOutput {
    let ciContext = CIContext()
    let writeQueue = DispatchQueue(label: "write")
    var count = 0
    var times = ""

    func stream(_ stream: SCStream, didOutputSampleBuffer buffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, let pixels = buffer.imageBuffer,
              let attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
              let rawStatus = attachments.first?[.status] as? Int, SCFrameStatus(rawValue: rawStatus) == .complete
        else { return }
        let image = CIImage(cvPixelBuffer: pixels)
        guard let cg = ciContext.createCGImage(image, from: image.extent) else { return }
        let index = count
        count += 1
        times += String(format: "%04d %.4f\n", index, CMSampleBufferGetPresentationTimeStamp(buffer).seconds)
        writeQueue.async {
            let url = framesDir.appendingPathComponent(String(format: "%04d.png", index))
            if let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) {
                CGImageDestinationAddImage(dest, cg, nil)
                CGImageDestinationFinalize(dest)
            }
        }
    }
}

let recorder = Recorder()
Task {
    do {
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
        guard let window = content.windows.first(where: {
            $0.owningApplication?.processID == pid && $0.frame.width >= 320
        }) else {
            FileHandle.standardError.write("game window not found\n".data(using: .utf8)!)
            exit(1)
        }
        let filter = SCContentFilter(desktopIndependentWindow: window)
        let config = SCStreamConfiguration()
        config.width = width
        config.height = Int(Double(width) * window.frame.height / window.frame.width)
        config.minimumFrameInterval = CMTime(value: 1, timescale: 120)
        config.showsCursor = false
        config.queueDepth = 8
        let stream = SCStream(filter: filter, configuration: config, delegate: nil)
        try stream.addStreamOutput(recorder, type: .screen, sampleHandlerQueue: DispatchQueue(label: "frames"))
        try await stream.startCapture()
        try await Task.sleep(nanoseconds: UInt64(seconds * 1e9))
        try await stream.stopCapture()
        recorder.writeQueue.sync {}
        try recorder.times.write(to: outDir.appendingPathComponent("frames.txt"), atomically: true, encoding: .utf8)
        print("\(recorder.count) frames")
        exit(0)
    } catch {
        FileHandle.standardError.write("wincap: \(error)\n".data(using: .utf8)!)
        exit(1)
    }
}
RunLoop.main.run()
