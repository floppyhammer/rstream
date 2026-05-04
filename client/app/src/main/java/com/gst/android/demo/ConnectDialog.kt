package com.gst.android.demo

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun ConnectDialog(
    initialIp: String,
    onConnect: (String) -> Unit,
    onDismiss: () -> Unit
) {
    var ipText by remember { mutableStateOf(initialIp) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = "Manual Connect") },
        text = {
            Column {
                Text(text = "Enter host IP address:", modifier = Modifier.padding(bottom = 8.dp))
                TextField(
                    value = ipText,
                    onValueChange = { ipText = it },
                    placeholder = { Text("e.g. 192.168.1.100") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
            }
        },
        confirmButton = {
            Button(
                onClick = { onConnect(ipText) },
                enabled = ipText.isNotBlank()
            ) {
                Text("Connect")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}
