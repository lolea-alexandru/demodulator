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

            println(raw.width)
            println(raw.height)
            return raw
    }
}