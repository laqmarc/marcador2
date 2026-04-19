package com.marcador.padelble

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            val manager = remember { ScoreBleManager(applicationContext) }
            val bluetoothManager = applicationContext.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
            val bluetoothAdapter = bluetoothManager.adapter

            val permissionLauncher = rememberLauncherForActivityResult(
                contract = ActivityResultContracts.RequestMultiplePermissions()
            ) { grants ->
                if (grants.values.all { it }) {
                    manager.startScan()
                }
            }

            val enableBluetoothLauncher = rememberLauncherForActivityResult(
                contract = ActivityResultContracts.StartActivityForResult()
            ) {
                if (manager.hasPermissions()) {
                    manager.startScan()
                }
            }

            LaunchedEffect(Unit) {
                if (!manager.isBluetoothReady()) {
                    enableBluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                } else if (!manager.hasPermissions()) {
                    permissionLauncher.launch(requiredPermissions())
                } else {
                    manager.startScan()
                }
            }

            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = Color(0xFF07131F)) {
                    ScoreScreen(
                        manager = manager,
                        onGrantPermissions = { permissionLauncher.launch(requiredPermissions()) },
                        onEnableBluetooth = {
                            enableBluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                        },
                        onOpenBluetoothSettings = {
                            startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
                        }
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
    }
}

private fun requiredPermissions(): Array<String> =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT
        )
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

@Composable
private fun ScoreScreen(
    manager: ScoreBleManager,
    onGrantPermissions: () -> Unit,
    onEnableBluetooth: () -> Unit,
    onOpenBluetoothSettings: () -> Unit
) {
    val connectionText by manager.connectionText.collectAsState()
    val scoreState by manager.scoreState.collectAsState()
    val isScanning by manager.isScanning.collectAsState()

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.linearGradient(
                    colors = listOf(Color(0xFF07131F), Color(0xFF12304A))
                )
            )
            .padding(20.dp)
    ) {
        Column(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text(
                text = "Marcador de Padel",
                color = Color.White,
                fontSize = 30.sp,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = connectionText,
                color = Color(0xFF9FB6C8),
                fontSize = 16.sp
            )

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                ScoreCard(
                    modifier = Modifier.weight(1f),
                    title = "Parella A",
                    points = scoreState.teamAPoints,
                    games = scoreState.teamAGames,
                    accent = Color(0xFFFFB703)
                )
                ScoreCard(
                    modifier = Modifier.weight(1f),
                    title = "Parella B",
                    points = scoreState.teamBPoints,
                    games = scoreState.teamBGames,
                    accent = Color(0xFF8ECAE6)
                )
            }

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                ActionButton(
                    modifier = Modifier.weight(1f),
                    label = "Punt A",
                    color = Color(0xFFFFB703),
                    onClick = { manager.sendCommand("A") }
                )
                ActionButton(
                    modifier = Modifier.weight(1f),
                    label = "Punt B",
                    color = Color(0xFF8ECAE6),
                    onClick = { manager.sendCommand("B") }
                )
            }

            ActionButton(
                modifier = Modifier.fillMaxWidth(),
                label = "Reset",
                color = Color(0xFFFB5607),
                onClick = { manager.sendCommand("R") }
            )

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                SecondaryButton(
                    modifier = Modifier.weight(1f),
                    label = if (isScanning) "Buscant..." else "Reconnectar",
                    onClick = { manager.startScan() }
                )
                SecondaryButton(
                    modifier = Modifier.weight(1f),
                    label = "Permisos",
                    onClick = onGrantPermissions
                )
            }

            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                SecondaryButton(
                    modifier = Modifier.weight(1f),
                    label = "Bluetooth ON",
                    onClick = onEnableBluetooth
                )
                SecondaryButton(
                    modifier = Modifier.weight(1f),
                    label = "Ajustos BT",
                    onClick = onOpenBluetoothSettings
                )
            }

            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "L'app busca automaticament MarcadorPadel-BLE i rep notificacions BLE amb l'estat del marcador.",
                color = Color(0xFF9FB6C8),
                fontSize = 14.sp
            )
        }
    }
}

@Composable
private fun ScoreCard(
    modifier: Modifier = Modifier,
    title: String,
    points: String,
    games: Int,
    accent: Color
) {
    Column(
        modifier = modifier
            .background(Color(0x22000000), RoundedCornerShape(24.dp))
            .padding(18.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text(
            text = title,
            color = Color(0xFF9FB6C8),
            fontSize = 14.sp
        )
        Text(
            text = points,
            color = accent,
            fontSize = 64.sp,
            fontWeight = FontWeight.ExtraBold
        )
        Text(
            text = "Jocs: $games",
            color = Color.White,
            fontSize = 22.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
private fun ActionButton(
    modifier: Modifier = Modifier,
    label: String,
    color: Color,
    onClick: () -> Unit
) {
    Button(
        modifier = modifier.height(56.dp),
        onClick = onClick,
        colors = ButtonDefaults.buttonColors(containerColor = color, contentColor = Color(0xFF07131F)),
        shape = RoundedCornerShape(18.dp)
    ) {
        Text(text = label, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun SecondaryButton(
    modifier: Modifier = Modifier,
    label: String,
    onClick: () -> Unit
) {
    Button(
        modifier = modifier.height(52.dp),
        onClick = onClick,
        colors = ButtonDefaults.buttonColors(containerColor = Color(0x22000000), contentColor = Color.White),
        shape = RoundedCornerShape(18.dp)
    ) {
        Text(text = label)
    }
}
