package com.example.demodulator.decoder

/**
 * Hardcoded configuration for the demodulator.
 * Adjust the LED bounds for your specific camera setup.
 *
 * All coordinates are in the display-oriented bitmap (after EXIF rotation).
 * The orientation after EXIF rotation is clockwise by 90 degrees (charging port to the left)
 * Origin (0, 0) is the top-left corner as you'd see it in the gallery.
 */
object DecoderConfig {
    // LED bounding box in the photo (pixel coordinates).
    const val LED_LEFT   = 1084
    const val LED_RIGHT  = 1670

    const val LED_TOP    = 232
    const val LED_BOTTOM = 778

    // VLC parameters
    const val LED_FREQUENCY_HZ = 1000     // modulation frequency
    const val FRAME_RATE_HZ = 30          // camera frame rate

    // Calibrated from a 01010101 reference image — adjust based on the setup
    const val ROWS_PER_BIT = 34
}