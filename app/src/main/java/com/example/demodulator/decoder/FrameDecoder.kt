package com.example.demodulator.decoder

import kotlin.math.roundToInt

/**
 * Decodes an OOK bit sequence from a vertical brightness profile.
 *
 * Strategy: walk the profile in chunks of ROWS_PER_BIT rows. For each chunk,
 * sample only the middle 50% (skipping the noisy edges near transitions).
 * Count how many of those middle rows exceed the threshold, then majority-vote
 * to decide if the bit is 1 or 0.
 *
 * The threshold is the average brightness across the entire profile — works
 * well for OOK with roughly balanced 1s and 0s.
 */
class FrameDecoder(private val profile: FloatArray) {

    // ---------- Configuration ----------
    // How many rows correspond to one bit's duration.
    // Calibrated from a known 01010101 reference image.
    private val rowsPerBit: Int = DecoderConfig.ROWS_PER_BIT

    // Fraction of each bit's rows to ignore at the edges (25% each side).
    // The middle 50% is sampled.
    private val edgeFraction: Float = 0.25f

    // Number of rows to skip at each edge of a bit's window.
    private val edgeRows: Int = (rowsPerBit * edgeFraction).roundToInt()

    // Number of rows sampled in the middle of each bit (rowsPerBit - 2 * edgeRows).
    // For rowsPerBit=34 and edgeRows=9, this is 16.
    private val midRows: Int = rowsPerBit - 2 * edgeRows


    // ---------- Threshold ----------
    // Average brightness across the entire profile is the threshold.
    // Anything brighter than the average is "on" (1), anything dimmer is "off" (0).
    private val threshold: Float = profile.average().toFloat()


    /**
     * Decode the entire profile into a bit string.
     *
     * @return a String of '0's and '1's representing the decoded bits.
     */
    fun decode(): String {
        // ---------- Output collector ----------
        val result = StringBuilder()

        // ---------- Walk the profile in steps of rowsPerBit ----------
        // Starting from row 0, increment by rowsPerBit each iteration.
        // Stop when there isn't a full bit's worth of rows remaining.
        var bitStart = 0
        while (bitStart + rowsPerBit <= profile.size) {

            // ---------- Identify the middle region of this bit ----------
            // Skip the first 25% (edgeRows) and last 25% (edgeRows) of rows.
            // Sample only the middle midRows rows.
            val sampleFrom = bitStart + edgeRows
            val sampleTo = sampleFrom + midRows

            // ---------- Count how many middle rows exceed the threshold ----------
            // For each row in the middle region, check if its brightness > threshold.
            // If yes, increment the count by 1.
            var onCount = 0
            for (row in sampleFrom until sampleTo) {
                if (profile[row] > threshold) {
                    onCount++
                }
            }

            // ---------- Majority vote ----------
            // Divide onCount by midRows to get the fraction of rows that were "on".
            // If more than half the rows were "on", this bit is a 1; otherwise 0.
            val fractionOn = onCount.toFloat() / midRows
            if (fractionOn > 0.3f) {
                result.append('1')
            } else {
                result.append('0')
            }

            // ---------- Advance to the next bit ----------
            bitStart += rowsPerBit
        }

        return result.toString()
    }


    /**
     * Exposes the threshold value (useful for debugging/visualization).
     */
    fun getThreshold(): Float = threshold


    /**
     * Returns the bit window boundaries for each decoded bit.
     * Each entry is (startRow, endRow) of the central sampled region.
     * Useful for overlaying on the image to visualize decoding.
     */
    fun getBitWindows(): List<IntRange> {
        val windows = mutableListOf<IntRange>()
        var bitStart = 0
        while (bitStart + rowsPerBit <= profile.size) {
            val sampleFrom = bitStart + edgeRows
            val sampleTo = sampleFrom + midRows
            windows.add(sampleFrom until sampleTo)
            bitStart += rowsPerBit
        }
        return windows
    }
}