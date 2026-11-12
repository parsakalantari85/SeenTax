# SeenTax

SeenTax is a touch-free pointer for Windows. It uses a standard webcam to track one hand in real time and translates its position and gestures into mouse input: the hand moves the cursor, and a pinch between the thumb and a finger clicks. No additional hardware is required.

All processing takes place on the device. No camera data is stored or transmitted.

---

## Gestures

| Gesture | Action |
|---|---|
| Move the hand | Move the pointer |
| Tap thumb and index finger | Left click |
| Tap thumb and middle finger | Right click |
| Hold thumb and index finger | Press and hold: select text, move or resize windows |
| Hold thumb and middle finger, move up or down | Scroll |
| Close and open the fist | Close the active window |
| Frame the screen with both hands | Take a screenshot |

Click, hold and scroll gestures can be reassigned in the settings.

---

## Features

- **Low latency.** Hand tracking runs at the camera's full frame rate, and the pointer is updated at the display's refresh rate with motion prediction between frames.
- **Stable pointing.** Adaptive filtering removes hand tremor at rest without adding lag to fast movements.
- **Precision mode.** Slow, deliberate movements are scaled down for accurate targeting of small elements.
- **Either hand.** Users can switch hands at any time; the pointer continues from its current position.
- **Reliable detection.** A hand is followed only after it has been confirmed over several frames, and input pauses while the hand is too close to or too far from the camera.
- **Quick control.** Tracking can be paused from the notification area or with **Ctrl+Alt+H** in any application.

---

## Requirements

| | Minimum | Recommended |
|---|---|---|
| **OS** | Windows 11, 64-bit | Windows 11 24H2, or later |
| **Processor** | 4-core x64 processor | 6-core x64 processor |
| **Graphics** | - | DirectX 12 compatible |
| **Camera** | 640 x 480 (30) | 1920 x 1080 (60)* |
| **Storage** | 60 MB | 60 MB |

\* A higher-resolution camera won't make SeenTax run more smoothly. Good lighting improves tracking far more reliably.

---

## Performance

Inference time per frame, median of 200 runs:

| | CPU | GPU |
|---|---|---|
| Palm detector | 9.5 ms | 2.0 ms |
| Hand landmark model | 4.3 ms | 1.2 ms |

The palm detector runs only while no hand is being tracked. Once a hand is found, each frame requires the landmark model alone.

Measured on the reference system: Intel Core i5-13450HX, NVIDIA GeForce RTX 3050 Laptop GPU (6 GB). Performance may vary.

---

## Technology

SeenTax is written in C++20. The control panel is built with WinUI 3, and inference runs on Windows ML with Google's MediaPipe hand models, on either the CPU or the GPU through DirectML.

---

## License

SeenTax is released under the [MIT License](LICENSE.txt). The hand models are MediaPipe's, converted to ONNX by the [OpenCV Model Zoo](https://github.com/opencv/opencv_zoo) and licensed under Apache 2.0.
