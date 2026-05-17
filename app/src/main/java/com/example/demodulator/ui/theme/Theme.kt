package com.example.demodulator.ui.theme

import android.app.Activity
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val CyberpunkColorScheme = darkColorScheme(
    primary = NeonRed,
    onPrimary = DeepBlue,
    secondary = GhostBlue,
    background = DeepBlue,
    onBackground = NeonRed,
    surface = MidnightBlue,
    onSurface = NeonRed,
)

@Composable
fun DemodulatorTheme(
    content: @Composable () -> Unit
) {
    val colorScheme = CyberpunkColorScheme
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            // TODO: change the status bar to match the app theme
            // window.statusBarColor = colorScheme.background.toArgb()
            WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = false
        }
    }
    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content
    )
}