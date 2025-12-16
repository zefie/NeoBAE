package com.zefie.miniBAEDroid

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import org.minibae.Mixer
import org.minibae.Song

class SettingsFragment: Fragment(){

    private val prefName = "miniBAE_prefs"
    private val keyBankPath = "last_bank_path"
    private val builtinMarker = "__builtin__"
    private val keyMasterVol = "master_volume"
    private val keyCurve = "velocity_curve"
    private val keyReverb = "default_reverb"
    
    private var currentBankName = mutableStateOf("Loading...")
    private var isLoadingBank = mutableStateOf(false)
    private var reverbType = mutableStateOf(1)
    private var velocityCurve = mutableStateOf(0)
    private var masterVolume = mutableStateOf(75)

    private val openHsbPicker = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val data: Intent? = result.data
            data?.data?.let { uri ->
                loadBankFromUri(uri, true)
            }
        }
    }
    
    private fun loadBankFromUri(uri: Uri, hotSwap: Boolean) {
        isLoadingBank.value = true
        Thread {
            try {
                requireContext().contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                val cached = copyUriToCache(uri, "selected_bank.hsb")
                if (cached != null) {
                    // If hot-swapping, we need to handle the current playback state
                    val prefs = requireContext().getSharedPreferences(prefName, Context.MODE_PRIVATE)
                    
                    if (hotSwap) {
                        // Store current playback state
                        // Note: This would ideally interact with the HomeFragment's state
                        // For now, we just reload the bank
                    }
                    
                    // Read bytes and call native memory loader to avoid filesystem path issues
                    val bytes = cached.readBytes()
                    android.util.Log.d("SettingsFragment", "Calling Mixer.addBankFromMemory with ${bytes.size} bytes")
                    val r = Mixer.addBankFromMemory(bytes)
                    android.util.Log.d("SettingsFragment", "Mixer.addBankFromMemory returned: $r")
                    activity?.runOnUiThread {
                        if (r == 0) {
                            val friendly = Mixer.getBankFriendlyName()
                            val fallback = uri.lastPathSegment ?: cached.name
                            currentBankName.value = friendly ?: fallback
                            prefs.edit().putString(keyBankPath, cached.absolutePath).apply()
                            
                            // Hot-swap: reload song immediately with new bank
                            if (hotSwap) {
                                android.util.Log.d("SettingsFragment", "Calling reloadCurrentSongForBankSwap")
                                Thread {
                                    (activity as? MainActivity)?.reloadCurrentSongForBankSwap()
                                }.start()
                            }
                            
                            Toast.makeText(requireContext(), "Bank loaded: ${friendly ?: fallback}", Toast.LENGTH_SHORT).show()
                        } else {
                            currentBankName.value = "Failed to load bank"
                            Toast.makeText(requireContext(), "Failed to load bank (err=$r)", Toast.LENGTH_SHORT).show()
                        }
                        isLoadingBank.value = false
                    }
                } else {
                    activity?.runOnUiThread {
                        currentBankName.value = "Failed to cache file"
                        isLoadingBank.value = false
                    }
                }
            } catch (ex: Exception) {
                activity?.runOnUiThread {
                    currentBankName.value = "Error: ${ex.message}"
                    isLoadingBank.value = false
                    Toast.makeText(requireContext(), "Error loading bank: ${ex.message}", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }
    
    private fun loadBuiltInPatches() {
        isLoadingBank.value = true
        Thread {
            val r = Mixer.addBuiltInPatches()
            activity?.runOnUiThread {
                val prefs = requireContext().getSharedPreferences(prefName, Context.MODE_PRIVATE)
                if (r == 0) {
                    val friendly = Mixer.getBankFriendlyName()
                    currentBankName.value = friendly ?: "Built-in patches"
                    prefs.edit().putString(keyBankPath, builtinMarker).apply()
                    
                    // Hot-swap: reload song immediately with built-in bank
                    Thread {
                        (activity as? MainActivity)?.reloadCurrentSongForBankSwap()
                    }.start()
                    
                    Toast.makeText(requireContext(), "Loaded built-in patches", Toast.LENGTH_SHORT).show()
                } else {
                    currentBankName.value = "Failed to load built-in"
                    Toast.makeText(requireContext(), "Failed to load built-in patches (err=$r)", Toast.LENGTH_SHORT).show()
                }
                isLoadingBank.value = false
            }
        }.start()
    }


    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): android.view.View {
        // Load saved preferences
        val prefs = requireContext().getSharedPreferences(prefName, Context.MODE_PRIVATE)
        reverbType.value = prefs.getInt(keyReverb, 1)
        velocityCurve.value = prefs.getInt(keyCurve, 0)
        masterVolume.value = prefs.getInt(keyMasterVol, 75)
        
        // Initialize bank name from current mixer state
        Thread {
            val friendly = Mixer.getBankFriendlyName()
            activity?.runOnUiThread {
                currentBankName.value = friendly ?: "Unknown Bank"
            }
        }.start()
        
        return ComposeView(requireContext()).apply {
            setContent {
                MaterialTheme(
                    colors = if (androidx.compose.foundation.isSystemInDarkTheme()) darkColors() else lightColors()
                ) {
                    SettingsScreen(
                        bankName = currentBankName.value,
                        isLoadingBank = isLoadingBank.value,
                        reverbType = reverbType.value,
                        velocityCurve = velocityCurve.value,
                        masterVolume = masterVolume.value,
                        onLoadBank = {
                            val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                                addCategory(Intent.CATEGORY_OPENABLE)
                                type = "*/*"
                                putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/hsb", "application/x-hsb"))
                            }
                            openHsbPicker.launch(i)
                        },
                        onLoadBuiltin = {
                            loadBuiltInPatches()
                        },
                        onReverbChange = { value ->
                            reverbType.value = value
                            Mixer.setDefaultReverb(value)
                            prefs.edit().putInt(keyReverb, value).apply()
                        },
                        onCurveChange = { value ->
                            velocityCurve.value = value
                            Mixer.setDefaultVelocityCurve(value)
                            prefs.edit().putInt(keyCurve, value).apply()
                        },
                        onVolumeChange = { value ->
                            masterVolume.value = value
                            Mixer.setMasterVolumePercent(value)
                            prefs.edit().putInt(keyMasterVol, value).apply()
                        },
                        onClose = {
                            requireActivity().onBackPressed()
                        }
                    )
                }
            }
        }
    }

    private fun copyUriToCache(uri: Uri, outName: String): File? {
        return try {
            val input: InputStream? = requireContext().contentResolver.openInputStream(uri)
            input?.use { ins ->
                val outFile = File(requireContext().cacheDir, outName)
                FileOutputStream(outFile).use { fos ->
                    ins.copyTo(fos)
                }
                outFile
            }
        } catch (ex: Exception) {
            null
        }
    }
}

@Composable
fun SettingsScreen(
    bankName: String,
    isLoadingBank: Boolean,
    reverbType: Int,
    velocityCurve: Int,
    masterVolume: Int,
    onLoadBank: () -> Unit,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onClose: () -> Unit
) {
    val reverbOptions = listOf(
        "None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab",
        "Igor's Cavern", "Igor's Dungeon", "Small Reflections",
        "Early Reflections", "Basement", "Banquet Hall", "Catacombs"
    )
    
    val curveOptions = listOf("Default", "Peaky", "WebTV", "Expo", "Linear")
    
    var reverbExpanded by remember { mutableStateOf(false) }
    var curveExpanded by remember { mutableStateOf(false) }
    
    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colors.background
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp)
        ) {
            // Header with close button
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "Settings",
                    style = MaterialTheme.typography.h5,
                    fontWeight = FontWeight.Bold
                )
                IconButton(onClick = onClose) {
                    Icon(
                        Icons.Filled.Close,
                        contentDescription = "Close Settings",
                        modifier = Modifier.size(28.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            
            // Bank Section
            SettingCard(title = "Sound Bank", icon = Icons.Filled.LibraryMusic) {
                Column {
                    if (isLoadingBank) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.Center,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            CircularProgressIndicator(modifier = Modifier.size(24.dp))
                            Spacer(modifier = Modifier.width(12.dp))
                            Text("Loading bank...", style = MaterialTheme.typography.body2)
                        }
                    } else {
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(8.dp),
                            color = MaterialTheme.colors.primary.copy(alpha = 0.1f)
                        ) {
                            Row(
                                modifier = Modifier.padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.MusicNote,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.primary,
                                    modifier = Modifier.size(20.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                    text = bankName,
                                    style = MaterialTheme.typography.body1,
                                    fontWeight = FontWeight.SemiBold,
                                    color = MaterialTheme.colors.onSurface
                                )
                            }
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = onLoadBank,
                            modifier = Modifier.weight(1f),
                            enabled = !isLoadingBank
                        ) {
                            Icon(Icons.Filled.FolderOpen, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(modifier = Modifier.width(4.dp))
                            Text("Load .hsb")
                        }
                        OutlinedButton(
                            onClick = onLoadBuiltin,
                            modifier = Modifier.weight(1f),
                            enabled = !isLoadingBank
                        ) {
                            Icon(Icons.Filled.GetApp, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(modifier = Modifier.width(4.dp))
                            Text("Built-in")
                        }
                    }
                    
                    Text(
                        text = "Hot-swap support: Bank changes reload current song automatically",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Reverb Section
            SettingCard(title = "Reverb", icon = Icons.Filled.GraphicEq) {
                Column {
                    Box {
                        OutlinedButton(
                            onClick = { reverbExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = reverbOptions.getOrNull(reverbType - 1) ?: "None",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = reverbExpanded,
                            onDismissRequest = { reverbExpanded = false }
                        ) {
                            reverbOptions.forEachIndexed { index, option ->
                                DropdownMenuItem(onClick = {
                                    onReverbChange(index + 1)
                                    reverbExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Velocity Curve Section
            SettingCard(title = "Velocity Curve", icon = Icons.Filled.TrendingUp) {
                Column {
                    Box {
                        OutlinedButton(
                            onClick = { curveExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = curveOptions.getOrNull(velocityCurve) ?: "Default",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = curveExpanded,
                            onDismissRequest = { curveExpanded = false }
                        ) {
                            curveOptions.forEachIndexed { index, option ->
                                DropdownMenuItem(onClick = {
                                    onCurveChange(index)
                                    curveExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Master Volume Section
            SettingCard(title = "Master Volume", icon = Icons.Filled.VolumeUp) {
                Column {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Slider(
                            value = masterVolume.toFloat(),
                            onValueChange = { onVolumeChange(it.toInt()) },
                            valueRange = 0f..100f,
                            modifier = Modifier.weight(1f),
                            colors = SliderDefaults.colors(
                                thumbColor = MaterialTheme.colors.primary,
                                activeTrackColor = MaterialTheme.colors.primary,
                                inactiveTrackColor = MaterialTheme.colors.onSurface.copy(alpha = 0.2f)
                            )
                        )
                        Spacer(modifier = Modifier.width(12.dp))
                        Text(
                            text = "$masterVolume%",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary,
                            modifier = Modifier.width(60.dp)
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun SettingCard(
    title: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    content: @Composable ColumnScope.() -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        elevation = 4.dp,
        shape = RoundedCornerShape(12.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.padding(bottom = 12.dp)
            ) {
                Icon(
                    icon,
                    contentDescription = null,
                    tint = MaterialTheme.colors.primary,
                    modifier = Modifier.size(24.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = title,
                    style = MaterialTheme.typography.h6,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colors.primary
                )
            }
            content()
        }
    }
}
