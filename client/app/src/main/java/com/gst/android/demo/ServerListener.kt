package com.gst.android.demo

import android.net.wifi.WifiManager
import android.util.Log
import kotlinx.coroutines.*
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketException
import kotlin.coroutines.CoroutineContext

class UdpListener(
    private val port: Int,
    private val wifiManager: WifiManager, // Pass WifiManager for MulticastLock
    private val onPacketReceived: (String, String) -> Unit // Callback function
) : CoroutineScope {

    private val TAG = "ServerListener"
    private var job: Job? = null

    // We use the same CoroutineContext, but 'job' is now managed manually
    override val coroutineContext: CoroutineContext
        get() = Dispatchers.IO + (job ?: Job())

    private var socket: DatagramSocket? = null
    private var multicastLock: WifiManager.MulticastLock? = null
    private val bufferSize = 1024 // Size of the incoming buffer

    fun startListening() {
        // Check if a job exists AND is active before returning
        if (job != null && job!!.isActive) {
            Log.w(TAG, "Listener is already running.")
            return
        }

        job = Job() // Create a new job if the old one was cancelled

        // 💡 FIX 3: Re-declare the CoroutineScope to pick up the new Job
        val scope = CoroutineScope(Dispatchers.IO + job!!)

        // Acquire the MulticastLock
        multicastLock = wifiManager.createMulticastLock("UdpListenerLock").apply {
            setReferenceCounted(true)
            acquire()
            Log.d(TAG, "MulticastLock acquired.")
        }

        scope.launch {
            try {
                // 1. Create a DatagramSocket and bind it to the port
                // Binding to 0.0.0.0 (default) allows listening on all interfaces
                socket = DatagramSocket(port).apply {
                    reuseAddress = true
                    // Set to true to receive broadcast packets (optional, but good practice)
                    broadcast = true
                }
                Log.d(TAG, "UDP Socket created and listening on port $port")

                val buffer = ByteArray(bufferSize)
                val packet = DatagramPacket(buffer, bufferSize)

                while (isActive) {
                    try {
                        Log.i(TAG, "Wait for a packet")

                        // 2. Wait for a packet
                        socket?.receive(packet)

                        // 3. Extract data
                        val message = String(packet.data, 0, packet.length)
                        val senderIp = packet.address.hostAddress ?: "Unknown"

                        Log.i(TAG, "UDP received packet: $message")

                        // 4. Use withContext(Dispatchers.Main) for UI updates if needed,
                        // or just invoke the callback on the current Coroutine's context (IO)
                        // If you need to update UI, change to: withContext(Dispatchers.Main) { onPacketReceived(message, senderIp) }
                        onPacketReceived(message, senderIp)

                        // Clear the packet for the next receive
                        packet.length = bufferSize
                    } catch (e: Exception) {
                        if (isActive) {
                            Log.e(TAG, "UDP Receive Error: ${e.message}")
                        }
                        // If the job is active, rethrow or handle specific non-cancellation exceptions
                    }
                }
            } catch (e: SocketException) {
                if (isActive) {
                    Log.e(TAG, "UDP Socket Error (Start): ${e.message}")
                }
            } catch (e: Exception) {
                Log.e(TAG, "General Listener Error: ${e.message}")
            } finally {
                stopListening() // Ensure cleanup on final termination
            }
        }
    }

    fun stopListening() {
        Log.d(TAG, "Stopping UDP Listener...")
        // Cancel the coroutine job
        job?.cancel()
        job = null // Set job to null so startListening can restart it

        // Close the socket
        socket?.close()
        socket = null

        // Release the MulticastLock
        multicastLock?.let {
            if (it.isHeld) {
                it.release()
                Log.d(TAG, "MulticastLock released.")
            }
        }
        multicastLock = null
    }
}
