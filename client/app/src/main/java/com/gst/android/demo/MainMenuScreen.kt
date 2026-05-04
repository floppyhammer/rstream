package com.gst.android.demo

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun MainMenuScreen(
    hosts: List<Host>,
    onManualConnectClick: () -> Unit,
    onSettingsClick: () -> Unit,
    onHostClick: (Host) -> Unit,
    onHostLongClick: (Host) -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "Discovered Hosts",
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(bottom = 8.dp)
        )

        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .background(Color.Black)
                .padding(1.dp)
        ) {
            items(hosts) { host ->
                HostItemView(
                    host = host,
                    onClick = { onHostClick(host) },
                    onLongClick = { onHostLongClick(host) }
                )
            }
        }

        Button(
            onClick = onManualConnectClick,
            modifier = Modifier
                .padding(top = 16.dp)
        ) {
            Text("Manual Connect")
        }

        Button(
            onClick = onSettingsClick,
            modifier = Modifier
                .padding(top = 8.dp)
        ) {
            Text("Settings")
        }
    }
}

@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
fun HostItemView(
    host: Host,
    onClick: () -> Unit,
    onLongClick: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(
                onClick = onClick,
                onLongClick = onLongClick
            )
            .background(Color.White) // Assuming white background for items as they were in vertical layout
            .padding(16.dp)
    ) {
        Text(text = host.name, fontSize = 18.sp, color = Color.Black)
        Text(text = host.ipAddress, fontSize = 14.sp, color = Color.Gray)
    }
}
