package com.example.demodulator.decoder

import android.graphics.Bitmap

/**
 * Computes a vertical brightness profile of the LED region.
 *
 * Returns an array where each entry is the average red-channel brightness
 * of one row across the cropped LED region.
 */
object BrightnessProfile {

    fun compute(bitmap: Bitmap): FloatArray {
        // Define the boundaries of the image
        val left   = DecoderConfig.LED_LEFT
        val top    = DecoderConfig.LED_TOP
        val right  = DecoderConfig.LED_RIGHT
        val bottom = DecoderConfig.LED_BOTTOM
        val width  = right - left
        val height = bottom - top

        // Read all pixels in the LED region at once
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, left, top, width, height)

        // Average red channel across columns for each row
        val profile = FloatArray(height)
        for (row in 0 until height) {
            var sum = 0
            for (col in 0 until width) {
                val pixel = pixels[row * width + col]
                val red = (pixel shr 16) and 0xFF // mask to get the RED value of the pixels
                sum += red
            }

            // the profile value for row i will be the average value of the pixel value on that row
            profile[row] = sum.toFloat() / width
        }
        return profile
    }
}