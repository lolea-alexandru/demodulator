package com.example.demodulator.ui.screens


import android.graphics.Bitmap
import android.net.Uri
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.demodulator.OpenCVUtils
import com.example.demodulator.decoder.FrameLoader
import com.example.demodulator.ui.components.CyberButton
import com.example.demodulator.ui.theme.NeonRed
import androidx.core.graphics.createBitmap
import com.example.demodulator.LED
import androidx.compose.ui.graphics.Canvas
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.PaintingStyle



@Composable
fun FrameUploadScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val DEBUG_TAG = "RECT"
    val OTSU_TAG = "OTSU"
    val INFO_TAG = "INFO"

    // State that survives recompositions
    var bitmap by remember { mutableStateOf<Bitmap?>(null) }
    var decodedBits by remember { mutableStateOf<String?>(null) }
    var otsuResult by remember { mutableStateOf<Bitmap?>(null) }
    var ledArray by remember { mutableStateOf<Array<LED>?>(null) }
    var otsuThresholdsArray by remember { mutableStateOf<Array<Double>?>(null) }


    fun runThreshold(src: Bitmap?): Bitmap? {
        if (src == null) return null;
        val dst = createBitmap(src.width, src.height)
        ledArray = OpenCVUtils.findLEDBounds(src, dst)
        val currentLEDArray = ledArray ?: return null;
        val averageColumnGap = OpenCVUtils.findColumnGap(currentLEDArray)
        val maxSpan = OpenCVUtils.findMaxSpan(currentLEDArray);
        Log.e(INFO_TAG, "The avg gap is $averageColumnGap and the max span is $maxSpan")
        return dst
    }

    fun decode() {
        // Make sure the delegated variables are constant throughout execution (kinda grim ngl)
        val localLEDArr = ledArray ?: return;
        val localBitmap = bitmap ?: return;
//        val localOtsuThresholdsArray = otsuThresholdsArray ?: return;
        val localOtsuThresholdsArray: Array<Double> = Array(24) { 0.0 }

        // Get the Otsu thresholds for each of the LED regions
        // TODO: change the hardocded "7" to a calculated result
        val decodedResult = OpenCVUtils.demodulateBoardColMajor(localBitmap, localLEDArr, 16)

        for(decodedString in decodedResult) {
            Log.d("DECODED", decodedString);
        }
    }

    // File picker launcher
    val refImgLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            bitmap = FrameLoader.loadOrientedBitmap(context, uri)
            decodedBits = null // Reset previous result in case new img picked
            otsuResult = runThreshold(bitmap);
            bitmap = otsuResult
        }
    }

    val demodLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            // Get the current bitmap
            bitmap = FrameLoader.loadOrientedBitmap(context, uri)

            val localBitmap = bitmap ?: return@rememberLauncherForActivityResult
            val localLedArray = ledArray ?: return@rememberLauncherForActivityResult
            OpenCVUtils.drawLEDBounds(localBitmap, localLedArray)
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "FRAME UPLOAD",
            color = NeonRed,
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold,
        )

        Spacer(modifier = Modifier.height(16.dp))


        val currentBitmap = bitmap // created for smart-casting
        if (currentBitmap == null) {
            CyberButton(
                text = "PICK REFERENCE IMAGE",
                onClick = { refImgLauncher.launch("image/*") },
            )
        } else {
            bitmap?.let { bmp -> Image(bitmap = bmp.asImageBitmap(), contentDescription = "Otsu result") }
            Spacer(modifier = Modifier.height(16.dp))

            CyberButton(
                text = "PICK DEMODULATION IMAGE",
                onClick = { demodLauncher.launch("image/*") },
            )

            CyberButton(
                text = "DECODE",
                onClick = {
                    decode()
                },
            )

            CyberButton(
                text = "PICK DIFFERENT IMAGE",
                onClick = { demodLauncher.launch("image/*") },
            )

            // --- Decoded result display ---
            val bits = decodedBits
            if (bits != null) {
                Spacer(modifier = Modifier.height(16.dp))
                DecodedBitsDisplay(bits = bits)
            }
        }

            Spacer(modifier = Modifier.height(8.dp))

            CyberButton(
                text = "BACK",
                onClick = onBack,
            )
        }
}

@Composable
private fun DecodedBitsDisplay(bits: String) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "DECODED (${bits.length} bits)",
            color = NeonRed,
            fontSize = 16.sp,
            fontWeight = FontWeight.Bold,
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = bits,
            color = NeonRed,
            fontSize = 14.sp,
            fontFamily = FontFamily.Monospace,
        )
    }
}

