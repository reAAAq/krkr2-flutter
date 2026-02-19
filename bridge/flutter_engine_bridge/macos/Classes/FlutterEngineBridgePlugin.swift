import Accelerate
import Cocoa
import CoreVideo
import FlutterMacOS

private final class EngineHostTexture: NSObject, FlutterTexture {
  private let lock = NSLock()
  private var pixelBuffer: CVPixelBuffer?
  private var pixelWidth: Int = 0
  private var pixelHeight: Int = 0

  func copyPixelBuffer() -> Unmanaged<CVPixelBuffer>? {
    lock.lock()
    defer { lock.unlock() }
    guard let pixelBuffer else {
      return nil
    }
    return Unmanaged.passRetained(pixelBuffer)
  }

  func updateFrame(
    rgbaData: Data,
    width: Int,
    height: Int,
    rowBytes: Int
  ) -> Bool {
    guard width > 0, height > 0, rowBytes >= width * 4 else {
      return false
    }

    lock.lock()
    if pixelBuffer == nil || pixelWidth != width || pixelHeight != height {
      var buffer: CVPixelBuffer?
      let attrs: [CFString: Any] = [
        kCVPixelBufferCGImageCompatibilityKey: true,
        kCVPixelBufferCGBitmapContextCompatibilityKey: true,
        kCVPixelBufferIOSurfacePropertiesKey: [:],
      ]
      let status = CVPixelBufferCreate(
        kCFAllocatorDefault,
        width,
        height,
        kCVPixelFormatType_32BGRA,
        attrs as CFDictionary,
        &buffer
      )
      guard status == kCVReturnSuccess, let newBuffer = buffer else {
        lock.unlock()
        return false
      }
      pixelBuffer = newBuffer
      pixelWidth = width
      pixelHeight = height
    }
    guard let pixelBuffer else {
      lock.unlock()
      return false
    }
    lock.unlock()

    CVPixelBufferLockBaseAddress(pixelBuffer, [])
    defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, []) }
    guard let baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer) else {
      return false
    }
    let targetStride = CVPixelBufferGetBytesPerRow(pixelBuffer)
    return rgbaData.withUnsafeBytes { bytes in
      guard let srcBase = bytes.baseAddress else {
        return false
      }
      var srcBuffer = vImage_Buffer(
        data: UnsafeMutableRawPointer(mutating: srcBase),
        height: vImagePixelCount(height),
        width: vImagePixelCount(width),
        rowBytes: rowBytes
      )
      var dstBuffer = vImage_Buffer(
        data: baseAddress,
        height: vImagePixelCount(height),
        width: vImagePixelCount(width),
        rowBytes: targetStride
      )
      var map: [UInt8] = [2, 1, 0, 3]  // RGBA -> BGRA
      let convertResult = vImagePermuteChannels_ARGB8888(
        &srcBuffer,
        &dstBuffer,
        &map,
        vImage_Flags(kvImageNoFlags)
      )
      return convertResult == kvImageNoError
    }
  }
}

private final class EngineNativeSurfaceView: NSView {
  private static let inputPointerDown = 1
  private static let inputPointerMove = 2
  private static let inputPointerUp = 3
  private static let inputPointerScroll = 4

  private let viewId: Int64
  private let onDispose: (Int64) -> Void
  private let onPointerEvent: (Int64, [String: Any]) -> Void
  private let inputOverlayView = NSView(frame: .zero)
  private var nativeView: NSView?
  private weak var nativeViewOwnerWindow: NSWindow?
  private var nativeViewWindowPlaceholder: NSView?
  private var nativeViewPinnedConstraints: [NSLayoutConstraint] = []
  private weak var nativeViewOriginalSuperview: NSView?
  private var nativeViewOriginalFrame: NSRect = .zero
  private var nativeViewOriginalAutoresizingMask: NSView.AutoresizingMask = []
  private var nativeViewOriginalTranslatesAutoresizingMask = true
  private var nativeWindow: NSWindow?
  private var hostWindowObservers: [NSObjectProtocol] = []
  private var clipViewObserver: NSObjectProtocol?
  private weak var observedClipView: NSClipView?
  private var frameSyncScheduled = false
  private var ownerWindowFrameSyncScheduled = false

  init(
    viewId: Int64,
    onDispose: @escaping (Int64) -> Void,
    onPointerEvent: @escaping (Int64, [String: Any]) -> Void
  ) {
    self.viewId = viewId
    self.onDispose = onDispose
    self.onPointerEvent = onPointerEvent
    super.init(frame: .zero)
    wantsLayer = true
    layer?.backgroundColor = NSColor.black.cgColor
    inputOverlayView.translatesAutoresizingMaskIntoConstraints = false
    inputOverlayView.wantsLayer = true
    inputOverlayView.layer?.backgroundColor = NSColor.clear.cgColor
    addSubview(inputOverlayView)
    NSLayoutConstraint.activate([
      inputOverlayView.leadingAnchor.constraint(equalTo: leadingAnchor),
      inputOverlayView.trailingAnchor.constraint(equalTo: trailingAnchor),
      inputOverlayView.topAnchor.constraint(equalTo: topAnchor),
      inputOverlayView.bottomAnchor.constraint(equalTo: bottomAnchor),
    ])
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) {
    fatalError("init(coder:) has not been implemented")
  }

  deinit {
    removeHostObservers()
    removeClipViewObserver()
    detachNativeView()
    detachNativeWindow()
    onDispose(viewId)
  }

  override func viewDidMoveToWindow() {
    super.viewDidMoveToWindow()
    if window == nil {
      removeHostObservers()
      removeClipViewObserver()
      detachNativeWindow()
      return
    }
    installHostObservers()
    updateClipViewObserver()
    attachToHostWindowIfPossible()
    updateNativeWindowFrame()
    updateOwnerWindowFrame()
  }

  override func viewDidMoveToSuperview() {
    super.viewDidMoveToSuperview()
    updateClipViewObserver()
    scheduleNativeWindowFrameUpdate()
    scheduleOwnerWindowFrameUpdate()
  }

  override func layout() {
    super.layout()
    updateClipViewObserver()
    scheduleNativeWindowFrameUpdate()
    scheduleOwnerWindowFrameUpdate()
  }

  override func hitTest(_ point: NSPoint) -> NSView? {
    if bounds.contains(point) {
      return self
    }
    return super.hitTest(point)
  }

  override var acceptsFirstResponder: Bool {
    return true
  }

  override var isFlipped: Bool {
    return true
  }

  override func mouseDown(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerDown, event: event, fallbackMask: 1)
  }

  override func mouseDragged(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerMove, event: event)
  }

  override func mouseUp(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerUp, event: event, fallbackMask: 1)
  }

  override func rightMouseDown(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerDown, event: event, fallbackMask: 2)
  }

  override func rightMouseDragged(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerMove, event: event)
  }

  override func rightMouseUp(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerUp, event: event, fallbackMask: 2)
  }

  override func otherMouseDown(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerDown, event: event, fallbackMask: 4)
  }

  override func otherMouseDragged(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerMove, event: event)
  }

  override func otherMouseUp(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerUp, event: event, fallbackMask: 4)
  }

  override func scrollWheel(with event: NSEvent) {
    emitPointerEvent(type: Self.inputPointerScroll, event: event)
  }

  func attachNativeView(_ view: NSView, nativeWindow window: NSWindow?) {
    let ownerWindow = window ?? view.window
    let targetView = resolvePreferredNativeAttachView(
      sourceView: view,
      ownerWindow: ownerWindow
    )
    if nativeView === targetView {
      return
    }

    detachNativeWindow()
    detachNativeView()

    nativeView = targetView
    nativeViewOwnerWindow = ownerWindow
    nativeViewOriginalSuperview = targetView.superview
    nativeViewOriginalFrame = targetView.frame
    nativeViewOriginalAutoresizingMask = targetView.autoresizingMask
    nativeViewOriginalTranslatesAutoresizingMask =
      targetView.translatesAutoresizingMaskIntoConstraints
    nativeViewWindowPlaceholder = nil

    if let ownerWindow = nativeViewOwnerWindow,
      ownerWindow.contentView === targetView
    {
      let placeholder = NSView(frame: targetView.frame)
      placeholder.wantsLayer = true
      placeholder.layer?.backgroundColor = NSColor.black.cgColor
      ownerWindow.contentView = placeholder
      nativeViewWindowPlaceholder = placeholder
      ownerWindow.orderOut(nil)
    }

    targetView.removeFromSuperview()
    targetView.translatesAutoresizingMaskIntoConstraints = false
    addSubview(targetView)
    addSubview(inputOverlayView, positioned: .above, relativeTo: targetView)

    nativeViewPinnedConstraints = [
      targetView.leadingAnchor.constraint(equalTo: leadingAnchor),
      targetView.trailingAnchor.constraint(equalTo: trailingAnchor),
      targetView.topAnchor.constraint(equalTo: topAnchor),
      targetView.bottomAnchor.constraint(equalTo: bottomAnchor),
    ]
    NSLayoutConstraint.activate(nativeViewPinnedConstraints)
    needsLayout = true
    updateOwnerWindowFrame()
  }

  func detachNativeView() {
    guard let nativeView else {
      return
    }

    if !nativeViewPinnedConstraints.isEmpty {
      NSLayoutConstraint.deactivate(nativeViewPinnedConstraints)
      nativeViewPinnedConstraints.removeAll()
    }
    nativeView.removeFromSuperview()
    nativeView.translatesAutoresizingMaskIntoConstraints =
      nativeViewOriginalTranslatesAutoresizingMask
    nativeView.autoresizingMask = nativeViewOriginalAutoresizingMask
    nativeView.frame = nativeViewOriginalFrame

    if let ownerWindow = nativeViewOwnerWindow {
      if let placeholder = nativeViewWindowPlaceholder,
        ownerWindow.contentView === placeholder
      {
        ownerWindow.contentView = nativeView
      } else if let originalSuperview = nativeViewOriginalSuperview {
        originalSuperview.addSubview(nativeView)
      } else if ownerWindow.contentView == nil {
        ownerWindow.contentView = nativeView
      }
      ownerWindow.orderOut(nil)
    }

    self.nativeView = nil
    nativeViewOwnerWindow = nil
    nativeViewWindowPlaceholder = nil
    nativeViewOriginalSuperview = nil
    nativeViewOriginalFrame = .zero
    nativeViewOriginalAutoresizingMask = []
    ownerWindowFrameSyncScheduled = false
  }

  private func resolvePreferredNativeAttachView(
    sourceView: NSView,
    ownerWindow: NSWindow?
  ) -> NSView {
    _ = ownerWindow
    // Keep responder chain stable by attaching the exact view pointer from
    // engine_api. Aggressive child-view descent can break input dispatch.
    return sourceView
  }

  func attachNativeWindow(_ window: NSWindow) {
    detachNativeView()
    if nativeWindow === window {
      installHostObservers()
      updateClipViewObserver()
      attachToHostWindowIfPossible()
      scheduleNativeWindowFrameUpdate()
      return
    }

    detachNativeWindow()
    nativeWindow = window
    configureNativeWindow(window)
    installHostObservers()
    updateClipViewObserver()
    attachToHostWindowIfPossible()
    updateNativeWindowFrame()
    window.orderFront(nil)
  }

  func detachNativeWindow() {
    removeHostObservers()
    removeClipViewObserver()
    guard let nativeWindow else {
      return
    }
    if let parentWindow = nativeWindow.parent {
      parentWindow.removeChildWindow(nativeWindow)
    }
    nativeWindow.orderOut(nil)
    self.nativeWindow = nil
  }

  private func attachToHostWindowIfPossible() {
    guard let hostWindow = window, let nativeWindow else {
      return
    }

    if nativeWindow.parent !== hostWindow {
      if let parentWindow = nativeWindow.parent {
        parentWindow.removeChildWindow(nativeWindow)
      }
      hostWindow.addChildWindow(nativeWindow, ordered: .above)
    }
  }

  private func scheduleNativeWindowFrameUpdate() {
    guard nativeWindow != nil else {
      return
    }
    guard !frameSyncScheduled else {
      return
    }
    frameSyncScheduled = true
    DispatchQueue.main.async { [weak self] in
      guard let self else {
        return
      }
      self.frameSyncScheduled = false
      self.updateNativeWindowFrame()
    }
  }

  private func scheduleOwnerWindowFrameUpdate() {
    guard nativeView != nil, nativeViewOwnerWindow != nil else {
      return
    }
    guard !ownerWindowFrameSyncScheduled else {
      return
    }
    ownerWindowFrameSyncScheduled = true
    DispatchQueue.main.async { [weak self] in
      guard let self else {
        return
      }
      self.ownerWindowFrameSyncScheduled = false
      self.updateOwnerWindowFrame()
    }
  }

  private func updateNativeWindowFrame() {
    guard let hostWindow = window, let nativeWindow else {
      return
    }

    let rectInWindow = convert(bounds, to: nil)
    let rectOnScreen = hostWindow.convertToScreen(rectInWindow).integral
    if nativeWindow.frame.equalTo(rectOnScreen) {
      return
    }
    nativeWindow.setFrame(rectOnScreen, display: true, animate: false)
  }

  private func updateOwnerWindowFrame() {
    guard let hostWindow = window, let ownerWindow = nativeViewOwnerWindow,
      nativeView != nil
    else {
      return
    }
    let rectInWindow = convert(bounds, to: nil)
    let rectOnScreen = hostWindow.convertToScreen(rectInWindow).integral
    if ownerWindow.frame.equalTo(rectOnScreen) {
      return
    }
    ownerWindow.setFrame(rectOnScreen, display: false, animate: false)
  }

  private func configureNativeWindow(_ window: NSWindow) {
    var styleMask = window.styleMask
    styleMask.remove([.titled, .closable, .miniaturizable, .resizable])
    styleMask.insert(.borderless)
    window.styleMask = styleMask
    window.hasShadow = false
    window.backgroundColor = .black
    window.isMovable = false
    window.level = .normal
    window.collectionBehavior.insert(.fullScreenAuxiliary)
  }

  private func installHostObservers() {
    removeHostObservers()
    guard let hostWindow = window,
      nativeWindow != nil || nativeViewOwnerWindow != nil
    else {
      return
    }
    let center = NotificationCenter.default
    let names: [Notification.Name] = [
      NSWindow.didMoveNotification,
      NSWindow.didResizeNotification,
      NSWindow.didChangeScreenNotification,
      NSWindow.didEndLiveResizeNotification,
      NSWindow.didDeminiaturizeNotification,
    ]
    hostWindowObservers = names.map { name in
      center.addObserver(forName: name, object: hostWindow, queue: .main) {
        [weak self] _ in
        self?.scheduleNativeWindowFrameUpdate()
        self?.scheduleOwnerWindowFrameUpdate()
      }
    }
  }

  private func removeHostObservers() {
    let center = NotificationCenter.default
    for observer in hostWindowObservers {
      center.removeObserver(observer)
    }
    hostWindowObservers.removeAll()
  }

  private func updateClipViewObserver() {
    guard nativeWindow != nil || nativeViewOwnerWindow != nil else {
      removeClipViewObserver()
      return
    }
    let clipView = enclosingScrollView?.contentView
    if observedClipView === clipView {
      return
    }
    removeClipViewObserver()
    guard let clipView else {
      return
    }
    clipView.postsBoundsChangedNotifications = true
    observedClipView = clipView
    clipViewObserver = NotificationCenter.default.addObserver(
      forName: NSView.boundsDidChangeNotification,
      object: clipView,
      queue: .main
    ) { [weak self] _ in
      self?.scheduleNativeWindowFrameUpdate()
      self?.scheduleOwnerWindowFrameUpdate()
    }
  }

  private func removeClipViewObserver() {
    if let observer = clipViewObserver {
      NotificationCenter.default.removeObserver(observer)
      clipViewObserver = nil
    }
    observedClipView = nil
  }

  private func emitPointerEvent(type: Int, event: NSEvent, fallbackMask: Int32 = 0) {
    guard bounds.width > 0, bounds.height > 0 else {
      return
    }
    let localPoint = convert(event.locationInWindow, from: nil)
    let backingScale = window?.backingScaleFactor ?? 1.0
    let timestampMicros = Int64(event.timestamp * 1_000_000.0)
    let buttonMask = resolveButtonMask(event: event, fallbackMask: fallbackMask)

    let payload: [String: Any] = [
      "type": NSNumber(value: type),
      "timestampMicros": NSNumber(value: timestampMicros),
      "x": NSNumber(value: Double(localPoint.x) * backingScale),
      "y": NSNumber(value: Double(localPoint.y) * backingScale),
      "deltaX": NSNumber(value: Double(event.deltaX) * backingScale),
      "deltaY": NSNumber(value: Double(event.deltaY) * backingScale),
      "pointerId": NSNumber(value: 0),
      "button": NSNumber(value: Int(buttonMask)),
    ]
    // Avoid AppKit input callback reentrancy by posting to the next runloop turn.
    DispatchQueue.main.async { [weak self] in
      guard let self else {
        return
      }
      self.onPointerEvent(self.viewId, payload)
    }
  }

  private func resolveButtonMask(event: NSEvent, fallbackMask: Int32) -> Int32 {
    let pressedMask = Int32(NSEvent.pressedMouseButtons & 0x7)
    if pressedMask != 0 {
      return pressedMask
    }
    if fallbackMask != 0 {
      return fallbackMask
    }
    switch event.buttonNumber {
    case 0:
      return 1
    case 1:
      return 2
    case 2:
      return 4
    default:
      return 0
    }
  }
}

private final class EngineNativeSurfaceFactory: NSObject, FlutterPlatformViewFactory
{
  private let onCreate: (Int64, EngineNativeSurfaceView) -> Void
  private let onDispose: (Int64) -> Void
  private let onPointerEvent: (Int64, [String: Any]) -> Void

  init(
    onCreate: @escaping (Int64, EngineNativeSurfaceView) -> Void,
    onDispose: @escaping (Int64) -> Void,
    onPointerEvent: @escaping (Int64, [String: Any]) -> Void
  ) {
    self.onCreate = onCreate
    self.onDispose = onDispose
    self.onPointerEvent = onPointerEvent
    super.init()
  }

  func createArgsCodec() -> (NSObjectProtocol & FlutterMessageCodec)? {
    return FlutterStandardMessageCodec.sharedInstance()
  }

  func create(
    withViewIdentifier viewId: Int64,
    arguments _: Any?
  ) -> NSView {
    let nativeSurfaceView = EngineNativeSurfaceView(
      viewId: viewId,
      onDispose: onDispose,
      onPointerEvent: onPointerEvent
    )
    onCreate(viewId, nativeSurfaceView)
    return nativeSurfaceView
  }
}

public class FlutterEngineBridgePlugin: NSObject, FlutterPlugin {
  private static let nativeSurfaceViewType =
    "flutter_engine_bridge/native_engine_surface"

  private let registrar: FlutterPluginRegistrar
  private var textures: [Int64: EngineHostTexture] = [:]
  private var nativeSurfaceViews: [Int64: EngineNativeSurfaceView] = [:]
  private var nativeSurfaceFactory: EngineNativeSurfaceFactory?
  private var pendingNativePointerEvents: [Int64: [[String: Any]]] = [:]

  init(registrar: FlutterPluginRegistrar) {
    self.registrar = registrar
    super.init()
  }

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(
      name: "flutter_engine_bridge",
      binaryMessenger: registrar.messenger
    )
    let instance = FlutterEngineBridgePlugin(registrar: registrar)
    instance.registerNativeSurfaceFactory()
    registrar.addMethodCallDelegate(instance, channel: channel)
  }

  deinit {
    for (textureId, _) in textures {
      registrar.textures.unregisterTexture(textureId)
    }
    for (_, nativeSurfaceView) in nativeSurfaceViews {
      nativeSurfaceView.detachNativeView()
      nativeSurfaceView.detachNativeWindow()
    }
    nativeSurfaceViews.removeAll()
  }

  private func registerNativeSurfaceFactory() {
    let factory = EngineNativeSurfaceFactory(
      onCreate: { [weak self] viewId, nativeSurfaceView in
        self?.nativeSurfaceViews[viewId] = nativeSurfaceView
      },
      onDispose: { [weak self] viewId in
        self?.nativeSurfaceViews.removeValue(forKey: viewId)
        self?.pendingNativePointerEvents.removeValue(forKey: viewId)
      },
      onPointerEvent: { [weak self] viewId, payload in
        self?.enqueueNativePointerEvent(viewId: viewId, payload: payload)
      }
    )
    nativeSurfaceFactory = factory
    registrar.register(factory, withId: Self.nativeSurfaceViewType)
  }

  private func enqueueNativePointerEvent(viewId: Int64, payload: [String: Any]) {
    var queue = pendingNativePointerEvents[viewId] ?? []
    queue.append(payload)
    if queue.count > 1024 {
      queue.removeFirst(queue.count - 1024)
    }
    pendingNativePointerEvents[viewId] = queue
  }

  private func drainNativePointerEvents(viewId: Int64) -> [[String: Any]] {
    let queue = pendingNativePointerEvents[viewId] ?? []
    pendingNativePointerEvents[viewId] = []
    return queue
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "getPlatformVersion":
      result("macOS " + ProcessInfo.processInfo.operatingSystemVersionString)

    case "createTexture":
      let texture = EngineHostTexture()
      let textureId = registrar.textures.register(texture)
      textures[textureId] = texture
      result(textureId)

    case "updateTextureRgba":
      guard let args = call.arguments as? [String: Any],
        let textureIdNumber = args["textureId"] as? NSNumber,
        let rgba = args["rgba"] as? FlutterStandardTypedData,
        let widthNumber = args["width"] as? NSNumber,
        let heightNumber = args["height"] as? NSNumber,
        let rowBytesNumber = args["rowBytes"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message:
              "updateTextureRgba requires textureId/rgba/width/height/rowBytes",
            details: nil
          )
        )
        return
      }

      let textureId = textureIdNumber.int64Value
      let width = widthNumber.intValue
      let height = heightNumber.intValue
      let rowBytes = rowBytesNumber.intValue
      guard let texture = textures[textureId] else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "Texture id not found",
            details: nil
          )
        )
        return
      }

      guard texture.updateFrame(
        rgbaData: rgba.data,
        width: width,
        height: height,
        rowBytes: rowBytes
      ) else {
        result(
          FlutterError(
            code: "texture_update_failed",
            message: "Failed to update texture frame",
            details: nil
          )
        )
        return
      }

      registrar.textures.textureFrameAvailable(textureId)
      result(nil)

    case "disposeTexture":
      guard let args = call.arguments as? [String: Any],
        let textureIdNumber = args["textureId"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "disposeTexture requires textureId",
            details: nil
          )
        )
        return
      }
      let textureId = textureIdNumber.int64Value
      textures.removeValue(forKey: textureId)
      registrar.textures.unregisterTexture(textureId)
      result(nil)

    case "attachNativeWindow":
      guard let args = call.arguments as? [String: Any],
        let viewIdNumber = args["viewId"] as? NSNumber,
        let windowHandleNumber = args["windowHandle"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "attachNativeWindow requires viewId/windowHandle",
            details: nil
          )
        )
        return
      }

      let viewId = viewIdNumber.int64Value
      guard let nativeSurfaceView = nativeSurfaceViews[viewId] else {
        result(
          FlutterError(
            code: "surface_not_found",
            message: "native surface view not found",
            details: nil
          )
        )
        return
      }

      let windowHandle = windowHandleNumber.uint64Value
      guard let rawPointer = UnsafeRawPointer(bitPattern: UInt(windowHandle))
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "windowHandle is invalid",
            details: nil
          )
        )
        return
      }
      let nativeWindow = Unmanaged<NSWindow>.fromOpaque(rawPointer)
        .takeUnretainedValue()
      nativeSurfaceView.attachNativeWindow(nativeWindow)
      result(nil)

    case "attachNativeView":
      guard let args = call.arguments as? [String: Any],
        let viewIdNumber = args["viewId"] as? NSNumber,
        let viewHandleNumber = args["viewHandle"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "attachNativeView requires viewId/viewHandle",
            details: nil
          )
        )
        return
      }

      let viewId = viewIdNumber.int64Value
      guard let nativeSurfaceView = nativeSurfaceViews[viewId] else {
        result(
          FlutterError(
            code: "surface_not_found",
            message: "native surface view not found",
            details: nil
          )
        )
        return
      }

      let viewHandle = viewHandleNumber.uint64Value
      guard let rawViewPointer = UnsafeRawPointer(bitPattern: UInt(viewHandle))
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "viewHandle is invalid",
            details: nil
          )
        )
        return
      }
      let nativeView = Unmanaged<NSView>.fromOpaque(rawViewPointer)
        .takeUnretainedValue()
      if nativeView === nativeSurfaceView {
        result(
          FlutterError(
            code: "invalid_args",
            message: "viewHandle cannot target native surface view itself",
            details: nil
          )
        )
        return
      }

      var nativeWindow: NSWindow?
      if let windowHandleNumber = args["windowHandle"] as? NSNumber {
        let windowHandle = windowHandleNumber.uint64Value
        guard
          let rawWindowPointer = UnsafeRawPointer(
            bitPattern: UInt(windowHandle)
          )
        else {
          result(
            FlutterError(
              code: "invalid_args",
              message: "windowHandle is invalid",
              details: nil
            )
          )
          return
        }
        nativeWindow = Unmanaged<NSWindow>.fromOpaque(rawWindowPointer)
          .takeUnretainedValue()
      }

      nativeSurfaceView.attachNativeView(nativeView, nativeWindow: nativeWindow)
      result(nil)

    case "detachNativeView":
      guard let args = call.arguments as? [String: Any],
        let viewIdNumber = args["viewId"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "detachNativeView requires viewId",
            details: nil
          )
        )
        return
      }

      let viewId = viewIdNumber.int64Value
      guard let nativeSurfaceView = nativeSurfaceViews[viewId] else {
        result(nil)
        return
      }
      nativeSurfaceView.detachNativeView()
      result(nil)

    case "detachNativeWindow":
      guard let args = call.arguments as? [String: Any],
        let viewIdNumber = args["viewId"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "detachNativeWindow requires viewId",
            details: nil
          )
        )
        return
      }

      let viewId = viewIdNumber.int64Value
      guard let nativeSurfaceView = nativeSurfaceViews[viewId] else {
        result(nil)
        return
      }
      nativeSurfaceView.detachNativeWindow()
      result(nil)

    case "drainNativePointerEvents":
      guard let args = call.arguments as? [String: Any],
        let viewIdNumber = args["viewId"] as? NSNumber
      else {
        result(
          FlutterError(
            code: "invalid_args",
            message: "drainNativePointerEvents requires viewId",
            details: nil
          )
        )
        return
      }
      let viewId = viewIdNumber.int64Value
      result(drainNativePointerEvents(viewId: viewId))

    default:
      result(FlutterMethodNotImplemented)
    }
  }
}
