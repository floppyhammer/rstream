package com.gst.android.demo

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun SettingsScreen(
    videoQuality: String,
    framerate: String,
    bitrate: String,
    onVideoQualityClick: () -> Unit,
    onFramerateClick: () -> Unit,
    onBitrateClick: () -> Unit,
    onBackClick: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(modifier = Modifier.fillMaxWidth()) {
            Text(
                text = "Settings",
                fontSize = 24.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier
                    .align(Alignment.Center)
                    .padding(bottom = 16.dp)
            )
        }

        SettingsOption(
            label = "Video Quality: ${videoQuality.uppercase()}",
            onClick = onVideoQualityClick
        )
        SettingsOption(
            label = "Framerate: $framerate FPS",
            onClick = onFramerateClick
        )
        SettingsOption(
            label = "Bitrate: $bitrate Mbps",
            onClick = onBitrateClick
        )
        
        Spacer(modifier = Modifier.weight(1f))
        
        Button(onClick = onBackClick) {
            Text("Back")
        }
    }
}

@Composable
fun SettingsOption(label: String, onClick: () -> Unit) {
    Text(
        text = label,
        fontSize = 18.sp,
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(16.dp)
    )
}

@Composable
fun BitrateDialog(
    initialBitrate: Int,
    onConfirm: (Int) -> Unit,
    onDismiss: () -> Unit
) {
    var sliderPosition by remember { mutableStateOf(initialBitrate.toFloat()) }
    
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = "Select Bitrate") },
        text = {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(text = "${sliderPosition.toInt()} Mbps", fontSize = 20.sp)
                Slider(
                    value = sliderPosition,
                    onValueChange = { sliderPosition = it },
                    valueRange = 1f..100f,
                    modifier = Modifier.padding(top = 16.dp)
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(sliderPosition.toInt()) }) {
                Text("OK")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

@Composable
fun SelectionDialog(
    title: String,
    options: List<String>,
    onSelect: (String) -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = title) },
        text = {
            Column {
                options.forEach { option ->
                    Text(
                        text = option,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onSelect(option) }
                            .padding(16.dp),
                        fontSize = 16.sp
                    )
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}
