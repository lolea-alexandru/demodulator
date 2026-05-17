package com.example.demodulator.decoder

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.net.Uri
import androidx.exifinterface.media.ExifInterface

/**
 * Loads an image from a Uri and rotates it according to EXIF metadata,
 * so the resulting Bitmap matches what the user sees in the Gallery.
 */
object FrameLoader {
    fun loadOrientedBitmap(context: Context, uri: Uri): Bitmap? {
        // Decode raw pixels
        val raw = context.contentResolver.openInputStream(uri)?.use { stream ->
            BitmapFactory.decodeStream(stream)
        } ?: return null

        // Read EXIF orientation
        val orientation = context.contentResolver.openInputStream(uri)?.use { stream ->
            ExifInterface(stream).getAttributeInt(
                ExifInterface.TAG_ORIENTATION,
                ExifInterface.ORIENTATION_NORMAL
            )
        } ?: ExifInterface.ORIENTATION_NORMAL

        // Define the rotation degrees of the BITMAP MATRIX.
        // By default, the bitmap matrix is in landscape (portrait + clockwise 90deg)
        val rotationDegrees = when (orientation) {
            ExifInterface.ORIENTATION_ROTATE_90 -> 90f
            ExifInterface.ORIENTATION_ROTATE_180 -> 180f
            ExifInterface.ORIENTATION_ROTATE_270 -> 270f
            else -> 0f
        }

        // No deg => return like so
        if (rotationDegrees == 0f) return raw

        // Apply the rotation matrix
        val matrix = Matrix().apply { postRotate(rotationDegrees) }
        return Bitmap.createBitmap(raw, 0, 0, raw.width, raw.height, matrix, true)
    }
}