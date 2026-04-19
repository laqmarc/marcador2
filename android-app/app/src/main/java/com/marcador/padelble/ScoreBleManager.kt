package com.marcador.padelble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject
import java.nio.charset.StandardCharsets
import java.util.UUID

private const val DEVICE_NAME = "MarcadorPadel-BLE"
private val SERVICE_UUID: UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
private val STATE_UUID: UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")
private val COMMAND_UUID: UUID = UUID.fromString("e3223119-9445-4e96-a4a1-85358c4046a2")
private val CLIENT_CONFIG_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

class ScoreBleManager(private val context: Context) {
    private val bluetoothManager =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter: BluetoothAdapter? = bluetoothManager.adapter

    private val _connectionText = MutableStateFlow("Desconnectat")
    val connectionText: StateFlow<String> = _connectionText.asStateFlow()

    private val _scoreState = MutableStateFlow(ScoreState())
    val scoreState: StateFlow<ScoreState> = _scoreState.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private var bluetoothGatt: BluetoothGatt? = null
    private var commandCharacteristic: BluetoothGattCharacteristic? = null
    private var stateCharacteristic: BluetoothGattCharacteristic? = null
    private val mainHandler = Handler(Looper.getMainLooper())
    private var isConnected = false

    private val pollStateRunnable = object : Runnable {
        override fun run() {
            readStateCharacteristic()
            if (isConnected) {
                mainHandler.postDelayed(this, 800)
            }
        }
    }

    fun hasPermissions(): Boolean {
        val permissions = buildList {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(Manifest.permission.BLUETOOTH_SCAN)
                add(Manifest.permission.BLUETOOTH_CONNECT)
            } else {
                add(Manifest.permission.ACCESS_FINE_LOCATION)
            }
        }

        return permissions.all {
            ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
        }
    }

    fun isBluetoothReady(): Boolean = adapter?.isEnabled == true

    @SuppressLint("MissingPermission")
    fun startScan() {
        val scanner = adapter?.bluetoothLeScanner ?: run {
            _connectionText.value = "Bluetooth no disponible"
            return
        }

        if (_isScanning.value) {
            return
        }

        disconnect()
        _connectionText.value = "Buscant marcador..."
        _isScanning.value = true

        val filters = listOf(
            ScanFilter.Builder()
                .setServiceUuid(android.os.ParcelUuid(SERVICE_UUID))
                .build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        scanner.startScan(filters, settings, scanCallback)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        _isScanning.value = false
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        stopScan()
        stopPolling()
        isConnected = false
        commandCharacteristic = null
        stateCharacteristic = null
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        _connectionText.value = "Desconnectat"
    }

    @SuppressLint("MissingPermission")
    fun sendCommand(command: String) {
        val characteristic = commandCharacteristic ?: return
        characteristic.value = command.toByteArray(StandardCharsets.UTF_8)
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        bluetoothGatt?.writeCharacteristic(characteristic)
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScan()
            connect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            _isScanning.value = false
            _connectionText.value = "Error d'escaneig BLE: $errorCode"
        }
    }

    @SuppressLint("MissingPermission")
    private fun connect(device: BluetoothDevice) {
        _connectionText.value = "Connectant amb ${device.name ?: "ESP32"}..."
        bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    @SuppressLint("MissingPermission")
    private fun readStateCharacteristic() {
        val gatt = bluetoothGatt ?: return
        val characteristic = stateCharacteristic ?: return
        gatt.readCharacteristic(characteristic)
    }

    private fun startPolling() {
        stopPolling()
        mainHandler.postDelayed(pollStateRunnable, 800)
    }

    private fun stopPolling() {
        mainHandler.removeCallbacks(pollStateRunnable)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                isConnected = false
                stopPolling()
                _connectionText.value = "Error BLE: $status"
                return
            }

            when (newState) {
                BluetoothGatt.STATE_CONNECTED -> {
                    isConnected = true
                    _connectionText.value = "Connectat, carregant serveis..."
                    gatt.discoverServices()
                }
                BluetoothGatt.STATE_DISCONNECTED -> {
                    isConnected = false
                    stopPolling()
                    commandCharacteristic = null
                    stateCharacteristic = null
                    _connectionText.value = "Desconnectat"
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                _connectionText.value = "No s'han descobert els serveis"
                return
            }

            val service: BluetoothGattService = gatt.getService(SERVICE_UUID) ?: run {
                _connectionText.value = "Servei BLE no trobat"
                return
            }

            stateCharacteristic = service.getCharacteristic(STATE_UUID)
            commandCharacteristic = service.getCharacteristic(COMMAND_UUID)

            val state = stateCharacteristic
            if (state != null) {
                enableNotifications(gatt, state)
                readStateCharacteristic()
                startPolling()
            }

            _connectionText.value = "Connectat a MarcadorPadel-BLE"
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int
        ) {
            if (characteristic.uuid == STATE_UUID && status == BluetoothGatt.GATT_SUCCESS) {
                updateScoreState(value)
            }
        }

        @Deprecated("Compat callback for Android < 13")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (characteristic.uuid == STATE_UUID && status == BluetoothGatt.GATT_SUCCESS) {
                updateScoreState(characteristic.value ?: byteArrayOf())
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            if (characteristic.uuid == STATE_UUID) {
                updateScoreState(value)
            }
        }

        @Deprecated("Compat callback for Android < 13")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == STATE_UUID) {
                updateScoreState(characteristic.value ?: byteArrayOf())
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (descriptor.characteristic.uuid == STATE_UUID && status == BluetoothGatt.GATT_SUCCESS) {
                readStateCharacteristic()
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun enableNotifications(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic
    ) {
        gatt.setCharacteristicNotification(characteristic, true)
        val descriptor = characteristic.getDescriptor(CLIENT_CONFIG_UUID) ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        } else {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        }
    }

    private fun updateScoreState(raw: ByteArray) {
        val payload = raw.toString(StandardCharsets.UTF_8)
        runCatching {
            val json = JSONObject(payload)
            _scoreState.value = ScoreState(
                teamAPoints = json.optString("teamAPoints", "0"),
                teamAGames = json.optInt("teamAGames", 0),
                teamBPoints = json.optString("teamBPoints", "0"),
                teamBGames = json.optInt("teamBGames", 0)
            )
        }.onFailure {
            _connectionText.value = "Error llegint estat BLE"
        }
    }
}
