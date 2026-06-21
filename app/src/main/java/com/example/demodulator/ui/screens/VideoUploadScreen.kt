package com.example.demodulator.ui.screens


import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.graphics.createBitmap
import com.example.demodulator.LED
import com.example.demodulator.OpenCVUtils
import com.example.demodulator.decoder.FrameLoader
import com.example.demodulator.ui.components.CyberButton
import com.example.demodulator.ui.theme.NeonRed
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import android.util.Log


/** One decoded video frame: the image plus its per-LED strings and BERs. */
data class DecodedFrame(
    val bitmap: Bitmap,
    val decoded: Array<String>,
    val errorRates: Array<Double>,
)


@Composable
fun VideoUploadScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val COLUMNS_PER_SYMBOL = 16
    val CAMERA_FPS = 30

    // State that survives recompositions
    var referenceBitmap by remember { mutableStateOf<Bitmap?>(null) }
    var ledArray by remember { mutableStateOf<Array<LED>?>(null) }
    var frames by remember { mutableStateOf<List<DecodedFrame>>(emptyList()) }
    var isProcessing by remember { mutableStateOf(false) }

    // Same as the frame screen: load image, find bounds, return the marked-up bitmap.
    fun runThreshold(src: Bitmap?): Bitmap? {
        if (src == null) return null
        val dst = createBitmap(src.width, src.height)
        ledArray = OpenCVUtils.findLEDBounds(src, dst)
        return dst
    }

    // Step 1: reference picker — establishes the LED bounds reused for every frame.
    val refImgLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        if (uri != null) {
            val loaded = FrameLoader.loadOrientedBitmap(context, uri)
            referenceBitmap = runThreshold(loaded)
            frames = emptyList()        // drop any results from a previous video
        }
    }

    // Steps 2-4: video picker — extract frames at the camera fps and decode each.
    val videoLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        val leds = ledArray
        if (uri != null && leds != null) {
            scope.launch {
                isProcessing = true
                frames = emptyList()
                // Heavy work (decode + JNI) off the main thread.
                val result = withContext(Dispatchers.Default) {
                    extractAndDecode(context, uri, leds, CAMERA_FPS, COLUMNS_PER_SYMBOL)
                }
                frames = result
                isProcessing = false
            }
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
            text = "VIDEO UPLOAD",
            color = NeonRed,
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold,
        )

        Spacer(modifier = Modifier.height(16.dp))

        val currentReference = referenceBitmap
        if (currentReference == null) {
            // Step 1: nothing yet — pick a reference image.
            CyberButton(
                text = "PICK REFERENCE IMAGE",
                onClick = { refImgLauncher.launch("image/*") },
            )
        } else {
            // Show the reference (with detected bounds) only until a video is chosen.
            if (frames.isEmpty() && !isProcessing) {
                Image(
                    bitmap = currentReference.asImageBitmap(),
                    contentDescription = "Reference with bounds",
                )
                Spacer(modifier = Modifier.height(16.dp))
            }

            when {
                isProcessing -> {
                    Text(
                        text = "Processing frames…",
                        color = NeonRed,
                        fontSize = 14.sp,
                    )
                }

                frames.isEmpty() -> {
                    // Step 2: reference is set, no video yet.
                    CyberButton(
                        text = "PICK VIDEO",
                        onClick = { videoLauncher.launch("video/*") },
                    )
                }

                else -> {
                    // Step 5: carousel — one page per frame, each showing the
                    // frame image with its BER matrix directly underneath.
                    val pagerState = rememberPagerState(pageCount = { frames.size })
                    HorizontalPager(state = pagerState) { page ->
                        val frame = frames[page]
                        Column(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            Image(
                                bitmap = frame.bitmap.asImageBitmap(),
                                contentDescription = "Frame ${page + 1}",
                            )
                            Spacer(modifier = Modifier.height(8.dp))
                            DecodedGrid(frame.decoded, frame.errorRates)
                        }
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "Frame ${pagerState.currentPage + 1} / ${frames.size}",
                        color = NeonRed,
                        fontSize = 12.sp,
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Step 6: pick a different video (once one has been processed) + back.
        if (referenceBitmap != null && frames.isNotEmpty() && !isProcessing) {
            CyberButton(
                text = "PICK DIFFERENT VIDEO",
                onClick = { videoLauncher.launch("video/*") },
            )
            Spacer(modifier = Modifier.height(8.dp))
        }

        CyberButton(
            text = "BACK",
            onClick = onBack,
        )
    }
}


// Cap to keep a list of full-res bitmaps from OOM-ing on a long clip. Raise/remove
// as needed, or downscale the stored bitmaps if you want more frames.
private const val MAX_FRAMES = 150

/**
 * Pulls frames from [uri] at [fps] and decodes each against the reference [leds].
 * Call this off the main thread (e.g. inside withContext(Dispatchers.Default)).
 */
private fun extractAndDecode(
    context: Context,
    uri: Uri,
    leds: Array<LED>,
    fps: Int,
    columnsPerSymbol: Int,
): List<DecodedFrame> {
    val retriever = MediaMetadataRetriever()
    val out = mutableListOf<DecodedFrame>()
    try {
        retriever.setDataSource(context, uri)
        val durationMs = retriever
            .extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
            ?.toLongOrNull() ?: 0L

        val frameIntervalUs = 1_000_000L / fps                  // 33_333 µs at 30 fps
        val frameCount = minOf((durationMs * fps / 1000L).toInt(), MAX_FRAMES)

        val totalErrors = DoubleArray(24)
        val totalBits = LongArray(24)

        for (i in 0 until frameCount) {
            val timeUs = i * frameIntervalUs
            val raw = retriever.getFrameAtTime(timeUs, MediaMetadataRetriever.OPTION_CLOSEST)
                ?: continue

            // The native pipeline asserts RGBA_8888 — make sure the frame matches.
            val frame = if (raw.config == Bitmap.Config.ARGB_8888) raw
            else raw.copy(Bitmap.Config.ARGB_8888, false)

            // Decode the CLEAN frame first — drawing the boxes mutates pixels, so
            // doing it before decode would corrupt the LED regions.
            val decoded = OpenCVUtils.demodulateBoardColMajor(frame, leds, columnsPerSymbol)
            val rates = Array(decoded.size) { idx ->
                val s = decoded[idx]
                // minHammingDistance returns a Double (jdouble JNI). If it already
                // returns the *rate*, drop the "/ s.length"; if it returns the raw
                // distance count, this turns it into a BER.
                val ledErrors = if (s.isEmpty()) 0.0 else OpenCVUtils.minHammingDistance(s)

                if (s.isNotEmpty() && s.any { it != '0' }) {
                    totalErrors[idx] += ledErrors
                    totalBits[idx] += s.length
                }

                // Per-frame rate for the carousel grid display.
                if (s.isEmpty()) 0.0 else ledErrors / s.length
            }

            // Now overlay the reference bounds onto a mutable copy for display.
            val display = frame.copy(Bitmap.Config.ARGB_8888, true)
            OpenCVUtils.drawLEDBounds(display, leds)

            out.add(DecodedFrame(display, decoded, rates))
        }

        // Pooled per-LED BER over the whole video: BER_j = totalErrors_j / totalBits_j.
        for (i in 0 until 24) {
            val avgBer = if (totalBits[i] == 0L) 0.0 else totalErrors[i] / totalBits[i]
            Log.d("BER", "AVG BER on led $i is $avgBer")
        }
    } finally {
        retriever.release()
    }
    return out
}