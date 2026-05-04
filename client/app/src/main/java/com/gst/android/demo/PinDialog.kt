package com.gst.android.demo

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp

@Composable
fun PinDialog(
    hostName: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit
) {
    var pinText by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = "Set PIN for $hostName") },
        text = {
            Column {
                Text(text = "Enter 4-digit PIN:", modifier = Modifier.padding(bottom = 8.dp))
                TextField(
                    value = pinText,
                    onValueChange = { if (it.length <= 4) pinText = it },
                    placeholder = { Text("PIN") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    visualTransformation = PasswordVisualTransformation()
                )
            }
        },
        confirmButton = {
            Button(
                onClick = { onConfirm(pinText) },
                enabled = pinText.isNotEmpty()
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

@Composable
fun ClearPinDialog(
    hostName: String,
    savedPin: String?,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit
) {
    val message = if (savedPin != null) {
        "Do you want to clear the saved PIN ($savedPin) for $hostName?"
    } else {
        "No PIN is saved for $hostName."
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = "Clear PIN") },
        text = { Text(text = message) },
        confirmButton = {
            if (savedPin != null) {
                Button(onClick = onConfirm) {
                    Text("Clear")
                }
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(if (savedPin != null) "Cancel" else "OK")
            }
        }
    )
}
