package com.example.demodulator.ui.components


import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.demodulator.ui.theme.DimRed
import com.example.demodulator.ui.theme.DisabledGrey
import com.example.demodulator.ui.theme.DisabledTextGrey
import com.example.demodulator.ui.theme.MidnightBlue
import com.example.demodulator.ui.theme.NeonRed

/**
 * A cyberpunk-styled button with customizable text and accent color.
 *
 * @param text Main label shown on the button.
 * @param onClick What to do when tapped.
 * @param modifier Layout modifier passed from the caller.
 * @param accentColor Border + text color (defaults to neon red).
 * @param subtitle Optional smaller text below the main label (used for "coming soon").
 * @param enabled If false, the button is greyed out and not clickable.
 */
@Composable
fun CyberButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    accentColor: Color = NeonRed,
    subtitle: String? = null,
    enabled: Boolean = true,)
{
    Button(
        onClick = onClick,
        modifier = modifier
            .fillMaxWidth()
            .height(if (subtitle != null) 80.dp else 64.dp)
            .padding(vertical = 8.dp),
        enabled = enabled,
        shape = RoundedCornerShape(4.dp),
        border = BorderStroke(
            width = 2.dp,
            color = if (enabled) accentColor else DisabledGrey,
        ),
        colors = ButtonDefaults.buttonColors(
            containerColor = MidnightBlue,
            contentColor = accentColor,
            disabledContainerColor = MidnightBlue,
            disabledContentColor = DisabledTextGrey,
        ),
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = text,
                fontSize = 18.sp,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
            )
            if (subtitle != null) {
                Text(
                    text = subtitle,
                    fontSize = 12.sp,
                    textAlign = TextAlign.Center,
                )
            }
        }
    }
}