package com.example.demodulator


import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.demodulator.ui.components.CyberButton
import com.example.demodulator.ui.theme.NeonRed
import com.example.demodulator.ui.theme.DemodulatorTheme
import androidx.compose.runtime.*
import com.example.demodulator.ui.screens.FrameUploadScreen

enum class Screen { Main, FrameUpload }
class MainActivity : ComponentActivity() {
    external fun stringFromCpp(name: String): String

    companion object {
        init {
            System.loadLibrary("demodulator")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            DemodulatorTheme {
                Scaffold { innerPadding ->
                    AppNavigation(modifier = Modifier.padding(innerPadding))
                }
            }
        }
    }
}


@Composable
fun AppNavigation(modifier: Modifier = Modifier) {
    var currentScreen by remember { mutableStateOf(Screen.Main) }

    when (currentScreen) {
        Screen.Main -> MainScreen(
            onFrameUploadClick = { currentScreen = Screen.FrameUpload },
            modifier = modifier,
        )
        Screen.FrameUpload -> FrameUploadScreen(
            onBack = { currentScreen = Screen.Main },
            modifier = modifier,
        )
    }
}


@Composable
fun MainScreen(onFrameUploadClick: () -> Unit,modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = "DEMODULATOR",
            color = NeonRed,
            fontSize = 28.sp,
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
        )
        Text(
            text = "Visible Light Communication",
            color = NeonRed,
            fontSize = 14.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(top = 4.dp),
        )

        Spacer(modifier = Modifier.height(48.dp))

        CyberButton(
            text = "FRAME UPLOAD",
            onClick = onFrameUploadClick,
        )

        CyberButton(
            text = "VIDEO UPLOAD",
            onClick = { /* TODO */ },
        )
    }
}