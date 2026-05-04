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
fun SettingsDialog(
    videoQuality: String,
    framerate: String,
    bitrate: String,
    onVideoQualityChange: (String) -> Unit,
    onFramerateChange: (String) -> Unit,
    onBitrateChange: (Int) -> Unit,
    onDismiss: () -> Unit
) {
    var showQualityDialog by remember { mutableStateOf(false) }
    var showFramerateDialog by remember { mutableStateOf(false) }
    var showBitrateDialog by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = "Settings", fontWeight = FontWeight.Bold) },
        text = {
            Column {
                SettingsOption(
                    label = "Video Quality: ${videoQuality.uppercase()}",
                    onClick = { showQualityDialog = true }
                )
                SettingsOption(
                    label = "Framerate: $framerate FPS",
                    onClick = { showFramerateDialog = true }
                )
                SettingsOption(
                    label = "Bitrate: $bitrate Mbps",
                    onClick = { showBitrateDialog = true }
                )
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Close")
            }
        }
    )

    if (showQualityDialog) {
        SelectionDialog(
            title = "Select Video Quality",
            options = listOf("720p", "1080p", "1440p", "4k"),
            onSelect = {
                onVideoQualityChange(it)
                showQualityDialog = false
            },
            onDismiss = { showQualityDialog = false }
        )
    }

    if (showFramerateDialog) {
        SelectionDialog(
            title = "Select Framerate",
            options = listOf("15", "30", "60", "90"),
            onSelect = {
                onFramerateChange(it)
                showFramerateDialog = false
            },
            onDismiss = { showFramerateDialog = false }
        )
    }

    if (showBitrateDialog) {
        BitrateDialog(
            initialBitrate = bitrate.toIntOrNull() ?: 10,
            onConfirm = {
                onBitrateChange(it)
                showBitrateDialog = false
            },
            onDismiss = { showBitrateDialog = false }
        )
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
