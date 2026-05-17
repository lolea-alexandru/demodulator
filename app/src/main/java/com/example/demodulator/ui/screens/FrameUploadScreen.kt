package com.example.demodulator.ui.screens


import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.demodulator.decoder.DecoderConfig
import com.example.demodulator.decoder.FrameLoader
import com.example.demodulator.ui.components.CyberButton
import com.example.demodulator.ui.theme.NeonRed

@Composable
fun FrameUploadScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current

    // State that survives recompositions
    var bitmap by remember { mutableStateOf<Bitmap?>(null) }

    // File picker launcher
    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            bitmap = FrameLoader.loadOrientedBitmap(context, uri)
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
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

        if (bitmap == null) {
            CyberButton(
                text = "PICK IMAGE",
                onClick = { launcher.launch("image/*") },
            )
        } else {
            ImageWithLedOverlay(bitmap = bitmap!!)
            Spacer(modifier = Modifier.height(16.dp))
            CyberButton(
                text = "PICK DIFFERENT IMAGE",
                onClick = { launcher.launch("image/*") },
            )
        }

        Spacer(modifier = Modifier.height(8.dp))

        CyberButton(
            text = "BACK",
            onClick = onBack,
        )
    }
}

@Composable
private fun ImageWithLedOverlay(bitmap: Bitmap) {
    val imageBitmap = remember(bitmap) { bitmap.asImageBitmap() }

    BoxWithConstraints(
        modifier = Modifier.fillMaxWidth(),
        contentAlignment = Alignment.Center,
    ) {
        // Calculate the displayed size, preserving aspect ratio
        val availableWidthPx = with(androidx.compose.ui.platform.LocalDensity.current) {
            maxWidth.toPx()
        }
        val scale = availableWidthPx / bitmap.width
        val displayWidthPx = bitmap.width * scale
        val displayHeightPx = bitmap.height * scale

        val ledLeftPx   = DecoderConfig.LED_LEFT   * scale
        val ledTopPx    = DecoderConfig.LED_TOP    * scale
        val ledRightPx  = DecoderConfig.LED_RIGHT  * scale
        val ledBottomPx = DecoderConfig.LED_BOTTOM * scale

        Canvas(
            modifier = Modifier
                .width(maxWidth)
                .height(with(androidx.compose.ui.platform.LocalDensity.current) {
                    displayHeightPx.toDp()
                })
        ) {
            drawImage(imageBitmap, dstSize = androidx.compose.ui.unit.IntSize(
                displayWidthPx.toInt(),
                displayHeightPx.toInt()
            ))
            drawRect(
                color = Color.White,
                topLeft = androidx.compose.ui.geometry.Offset(ledLeftPx, ledTopPx),
                size = androidx.compose.ui.geometry.Size(
                    ledRightPx - ledLeftPx,
                    ledBottomPx - ledTopPx
                ),
                style = Stroke(width = 4f)
            )
        }
    }
}