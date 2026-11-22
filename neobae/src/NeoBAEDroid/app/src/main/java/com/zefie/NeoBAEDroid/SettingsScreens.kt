package com.zefie.NeoBAEDroid

import android.R
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Build
import android.provider.DocumentsContract
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.*
import androidx.compose.material.icons.filled.*
import androidx.compose.material.pullrefresh.PullRefreshIndicator
import androidx.compose.material.pullrefresh.pullRefresh
import androidx.compose.material.pullrefresh.rememberPullRefreshState
import androidx.compose.runtime.*
import androidx.compose.runtime.collectAsState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import androidx.core.view.WindowCompat
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.viewmodel.compose.viewModel
import com.zefie.NeoBAE.Mixer
import com.zefie.NeoBAE.Song
import java.io.File
import java.io.IOException
import kotlin.math.roundToInt
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
fun SettingsScreenContent(
    bankName: String,
    bankPath: String,
    hasEggsBank: Boolean,
    hasMobileBAEBank: Boolean,
    dlsBankLevel: Int,
    hasXmfOverlay: Boolean,
    hasRmiEmbedded: Boolean,
    rmiUsesSf2: Boolean,
    isLoadingBank: Boolean,
    reverbType: Int,
    velocityCurve: Int,
    fixPanLfoBias: Boolean,
    classicChorus: Boolean,
    webtvMode: Boolean,
    dlsCompatibilityMode: Boolean,
    normalizePlayback: Boolean,
    exportCodec: Int,
    baeScriptEnabled: Boolean,
    baeScriptSource: String,
    searchResultLimit: Int,
    showSearchFeatures: Boolean,
    onLoadBuiltin: () -> Unit,
    onReverbChange: (Int) -> Unit,
    onCurveChange: (Int) -> Unit,
    onFixPanLfoChange: (Boolean) -> Unit,
    onClassicChorusChange: (Boolean) -> Unit,
    onWebTVModeChange: (Boolean) -> Unit,
    onDLSCompatibilityModeChange: (Boolean) -> Unit,
    onNormalizePlaybackChange: (Boolean) -> Unit,
    onVolumeChange: (Int) -> Unit,
    onExportCodecChange: (Int) -> Unit,
    onBaeScriptEnabledChange: (Boolean) -> Unit,
    onBaeScriptSourceChange: (String) -> Unit,
    onSearchLimitChange: (Int) -> Unit,
    onOpenFileTypes: () -> Unit,
    onBrowseBanks: () -> Unit,
    onOpenCustomReverb: () -> Unit,
    onCustomReverbSync: () -> Unit,
    customEQSyncSerial: Int,
    onCustomEQSync: () -> Unit,
    onAddFolder: () -> Unit,
    onRemovePersistedFolder: (String) -> Unit
) {
    val context = LocalContext.current
    val builtInReverbOptions = BUILT_IN_REVERB_OPTIONS
    var presetNames by remember { mutableStateOf(loadCustomReverbPresetNames(context)) }
    var activePresetName by remember { mutableStateOf(getActiveCustomReverbPresetName(context)) }
    var showSavePresetDialog by remember { mutableStateOf(false) }
    var savePresetName by remember { mutableStateOf(activePresetName ?: "") }
    var showDeletePresetDialog by remember { mutableStateOf(false) }
    var showFolderManagerDialog by remember { mutableStateOf(false) }
    var folderManagerRefreshTick by remember { mutableStateOf(0) }
    val persistedFolderEntries = remember(folderManagerRefreshTick) { getPersistedSafFolderEntries(context) }

    // EQ State
    val prefs = context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
    var eqEnabled by remember(customEQSyncSerial) { mutableStateOf(prefs.getBoolean("eq_enabled", false)) }
    val eqGains = remember(customEQSyncSerial) {
        androidx.compose.runtime.mutableStateListOf(
            prefs.getFloat("eq_band_0", 0f),
            prefs.getFloat("eq_band_1", 0f),
            prefs.getFloat("eq_band_2", 0f),
            prefs.getFloat("eq_band_3", 0f),
            prefs.getFloat("eq_band_4", 0f)
        )
    }
    var eqExpanded by remember { mutableStateOf(false) }
    var showSaveEQDialog by remember { mutableStateOf(false) }
    var saveEQName by remember { mutableStateOf("") }
    var activeEQName by remember { mutableStateOf(getActiveCustomEQPresetName(context)) }
    var eqPresetNames by remember { mutableStateOf(loadCustomEQPresetNames(context)) }

    @Composable
    fun FolderAccessManagerDialog() {
        if (!showFolderManagerDialog) return

        AlertDialog(
            onDismissRequest = { showFolderManagerDialog = false },
            title = { Text("Manage Folder Access") },
            text = {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 420.dp)
                        .verticalScroll(rememberScrollState())
                ) {
                    Text(
                        text = "These folders have persisted access. Removing one revokes access and clears any favorites from that folder.",
                        style = MaterialTheme.typography.body2,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f)
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    if (persistedFolderEntries.isEmpty()) {
                        Text(
                            text = "No persisted folders yet.",
                            style = MaterialTheme.typography.body2,
                            color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f)
                        )
                    } else {
                        persistedFolderEntries.forEach { entry ->
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(bottom = 8.dp),
                                shape = RoundedCornerShape(8.dp),
                                color = MaterialTheme.colors.surface
                            ) {
                                Column(modifier = Modifier.padding(12.dp)) {
                                    Text(
                                        text = entry.label,
                                        style = MaterialTheme.typography.body1.copy(fontWeight = FontWeight.SemiBold),
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    Spacer(modifier = Modifier.height(4.dp))
                                    Text(
                                        text = entry.uri.toString(),
                                        style = MaterialTheme.typography.caption,
                                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                                        maxLines = 2,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    Spacer(modifier = Modifier.height(8.dp))
                                    TextButton(
                                        onClick = {
                                            onRemovePersistedFolder(entry.uri.toString())
                                            folderManagerRefreshTick++
                                        }
                                    ) {
                                        Text("Forget")
                                    }
                                }
                            }
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showFolderManagerDialog = false }) {
                    Text("Close")
                }
            },
            dismissButton = {
                TextButton(onClick = onAddFolder) {
                    Text("Add Folder")
                }
            }
        )
    }

    if (showSaveEQDialog) {
        AlertDialog(
            onDismissRequest = { showSaveEQDialog = false },
            title = { Text("Save EQ Preset") },
            text = {
                OutlinedTextField(
                    value = saveEQName,
                    onValueChange = { saveEQName = it },
                    label = { Text("Preset Name") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        val name = saveEQName.trim()
                        if (name.isNotEmpty()) {
                            val preset = CustomEQPreset(name, eqGains.toFloatArray())
                            saveCustomEQPreset(context, preset)
                            eqPresetNames = loadCustomEQPresetNames(context)
                            activeEQName = name
                            setActiveCustomEQPresetName(context, name)
                        }
                        showSaveEQDialog = false
                    }
                ) {
                    Text("Save")
                }
            },
            dismissButton = {
                TextButton(onClick = { showSaveEQDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }

    @Composable
    fun FolderAccessManagerCard() {
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
                        Icons.Filled.FolderOpen,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Folder Access",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }

                Text(
                    text = "Review persisted folders and revoke access when you no longer need it.",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                )

                Spacer(modifier = Modifier.height(12.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                    OutlinedButton(
                        onClick = { showFolderManagerDialog = true },
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Filled.Folder, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Manage")
                    }
                    Button(
                        onClick = onAddFolder,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Add Folder")
                    }
                }
            }
        }
    }

    val importNeoReverbLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        try {
            val xml = context.contentResolver.openInputStream(uri)?.use { it.readBytes().toString(Charsets.UTF_8) }
            val preset = xml?.let { parseNeoReverbXml(it) }
            if (preset != null) {
                saveCustomReverbPreset(context, preset)
                presetNames = loadCustomReverbPresetNames(context)
                activePresetName = preset.name
                setActiveCustomReverbPresetName(context, preset.name)
                onReverbChange(CUSTOM_REVERB_TYPE)
                applyCustomReverbPresetToEngine(context, preset)
                onCustomReverbSync()
                Toast.makeText(context, "Imported preset: ${preset.name}", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(context, "Invalid .neoreverb file", Toast.LENGTH_SHORT).show()
            }
        } catch (_: Exception) {
            Toast.makeText(context, "Failed to import .neoreverb", Toast.LENGTH_SHORT).show()
        }
    }

    val exportNeoReverbLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/xml")
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        try {
            val name = activePresetName
            if (name.isNullOrBlank() || !presetNames.contains(name)) {
                Toast.makeText(context, "Select a saved preset to export", Toast.LENGTH_SHORT).show()
                return@rememberLauncherForActivityResult
            }
            val preset = loadCustomReverbPreset(context, name) ?: snapshotCustomReverbFromEngine(context, name)
            val xml = presetToNeoReverbXml(preset)
            context.contentResolver.openOutputStream(uri)?.use { it.write(xml.toByteArray(Charsets.UTF_8)) }
            Toast.makeText(context, "Exported preset: ${preset.name}", Toast.LENGTH_SHORT).show()
        } catch (_: Exception) {
            Toast.makeText(context, "Failed to export .neoreverb", Toast.LENGTH_SHORT).show()
        }
    }

    val importBaeScriptLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        try {
            val name = DocumentFile.fromSingleUri(context, uri)?.name
                ?: uri.lastPathSegment
                ?: ""
            val lowerName = name.lowercase()
            if (!lowerName.endsWith(".bscript") && !lowerName.endsWith(".txt")) {
                Toast.makeText(context, "Please select a .bscript (or .txt) file", Toast.LENGTH_SHORT).show()
                return@rememberLauncherForActivityResult
            }

            val script = context.contentResolver.openInputStream(uri)
                ?.use { it.readBytes().toString(Charsets.UTF_8) }

            if (script != null) {
                onBaeScriptSourceChange(script)
                Toast.makeText(context, "BAEScript loaded", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(context, "Failed to read BAEScript", Toast.LENGTH_SHORT).show()
            }
        } catch (_: Exception) {
            Toast.makeText(context, "Failed to import BAEScript", Toast.LENGTH_SHORT).show()
        }
    }

    val exportBaeScriptLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.CreateDocument("application/octet-stream")
    ) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        try {
            context.contentResolver.openOutputStream(uri)?.use {
                it.write(baeScriptSource.toByteArray(Charsets.UTF_8))
            }
            Toast.makeText(context, "BAEScript exported", Toast.LENGTH_SHORT).show()
        } catch (_: Exception) {
            Toast.makeText(context, "Failed to export BAEScript", Toast.LENGTH_SHORT).show()
        }
    }

    val customEntryIndex = builtInReverbOptions.size
    val reverbOptions = remember(presetNames) { builtInReverbOptions + listOf("Custom") + presetNames }

    val selectedReverbLabel = when {
        reverbType == CUSTOM_REVERB_TYPE -> {
            val ap = activePresetName
            if (ap != null && presetNames.contains(ap)) ap else "Custom"
        }
        else -> builtInReverbOptions.getOrNull(reverbType - 1) ?: "None"
    }

    fun applyPresetByName(name: String) {
        val preset = loadCustomReverbPreset(context, name) ?: return
        setActiveCustomReverbPresetName(context, name)
        activePresetName = name
        onReverbChange(CUSTOM_REVERB_TYPE)
        applyCustomReverbPresetToEngine(context, preset)
        onCustomReverbSync()
    }

    fun clearActivePreset() {
        setActiveCustomReverbPresetName(context, null)
        activePresetName = null
    }
    
    val curveOptions = listOf("NeoBAE S Curve", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear", "No Curve")
    val exportCodecOptions = listOf("WAV", "OGG", "FLAC")
    val searchLimitOptions = listOf(
        250 to "250",
        500 to "500",
        750 to "750",
        1000 to "1,000",
        2500 to "2,500",
        5000 to "5,000",
        10000 to "10,000",
        25000 to "25,000",
        50000 to "50,000",
        -1 to "Unlimited"
    )
    
    var reverbExpanded by remember { mutableStateOf(false) }
    var curveExpanded by remember { mutableStateOf(false) }
    var exportCodecExpanded by remember { mutableStateOf(false) }
    var searchLimitExpanded by remember { mutableStateOf(false) }
    var showUnlimitedWarning by remember { mutableStateOf(false) }
    val scrollState = rememberScrollState()
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    
    @Composable
    fun EqualizerCard() {
        // Equalizer Section
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(
                        Icons.Filled.Tune,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "5-Band Equalizer",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Switch(
                        checked = eqEnabled,
                        onCheckedChange = {
                            eqEnabled = it
                            context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                .edit().putBoolean("eq_enabled", it).apply()
                            Mixer.setEQEnabled(it)
                        }
                    )
                }
                
                if (eqEnabled) {
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    // Dropdown for EQ Presets
                    val eqOptions = listOf("None") + BUILT_IN_EQ_PRESETS.keys.toList() + eqPresetNames
                    val currentEqLabel = activeEQName ?: "None"
                    
                    Box {
                        OutlinedButton(
                            onClick = { eqExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = currentEqLabel,
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = eqExpanded,
                            onDismissRequest = { eqExpanded = false }
                        ) {
                            eqOptions.forEach { option ->
                                DropdownMenuItem(onClick = {
                                    if (option == "None") {
                                        activeEQName = null
                                        setActiveCustomEQPresetName(context, null)
                                        for (i in 0 until 5) {
                                            eqGains[i] = 0f
                                            Mixer.setEQGain(i, 0f)
                                            context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                                .edit().putFloat("eq_band_$i", 0f).apply()
                                        }
                                    } else if (BUILT_IN_EQ_PRESETS.containsKey(option)) {
                                        activeEQName = option
                                        setActiveCustomEQPresetName(context, option)
                                        val presetGains = BUILT_IN_EQ_PRESETS[option]!!
                                        for (i in 0 until 5) {
                                            eqGains[i] = presetGains[i]
                                            Mixer.setEQGain(i, presetGains[i])
                                            context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                                .edit().putFloat("eq_band_$i", presetGains[i]).apply()
                                        }
                                    } else {
                                        // Custom preset
                                        loadCustomEQPreset(context, option)?.let { p ->
                                            activeEQName = option
                                            setActiveCustomEQPresetName(context, option)
                                            for (i in 0 until 5) {
                                                eqGains[i] = p.gains[i]
                                                Mixer.setEQGain(i, p.gains[i])
                                                context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                                    .edit().putFloat("eq_band_$i", p.gains[i]).apply()
                                            }
                                        }
                                    }
                                    eqExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(16.dp))
                    
                    if (isLandscape) {
                        // Sliders (Horizontal)
                        val labels = listOf("60Hz", "230Hz", "910Hz", "3.6kHz", "14kHz")
                        for (i in 0 until 5) {
                            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                                Text(labels[i], modifier = Modifier.width(50.dp), style = MaterialTheme.typography.caption)
                                Slider(
                                    value = eqGains[i],
                                    onValueChange = { 
                                        eqGains[i] = it
                                        Mixer.setEQGain(i, it)
                                    },
                                    onValueChangeFinished = {
                                        activeEQName = null
                                        setActiveCustomEQPresetName(context, null)
                                        context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                            .edit().putFloat("eq_band_$i", eqGains[i]).apply()
                                    },
                                    valueRange = -12f..12f,
                                    modifier = Modifier.weight(1f)
                                )
                                Text(
                                    String.format("%.1f dB", eqGains[i]),
                                    modifier = Modifier.width(50.dp),
                                    style = MaterialTheme.typography.caption,
                                    textAlign = androidx.compose.ui.text.style.TextAlign.End
                                )
                            }
                        }
                    } else {
                        // Sliders (Vertical)
                        val labels = listOf("60Hz", "230Hz", "910Hz", "3.6kHz", "14kHz")
                        Row(
                            horizontalArrangement = Arrangement.SpaceEvenly,
                            modifier = Modifier.fillMaxWidth().padding(vertical = 16.dp)
                        ) {
                            for (i in 0 until 5) {
                                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                    Text(
                                        String.format("%+.1f", eqGains[i]),
                                        style = MaterialTheme.typography.caption,
                                        modifier = Modifier.padding(bottom = 8.dp)
                                    )
                                    Box(
                                        modifier = Modifier.width(48.dp).height(240.dp),
                                        contentAlignment = Alignment.Center
                                    ) {
                                        Slider(
                                            value = eqGains[i],
                                            onValueChange = { 
                                                eqGains[i] = it
                                                Mixer.setEQGain(i, it)
                                            },
                                            onValueChangeFinished = {
                                                activeEQName = null
                                                setActiveCustomEQPresetName(context, null)
                                                context.getSharedPreferences("NeoBAE_prefs", Context.MODE_PRIVATE)
                                                    .edit().putFloat("eq_band_$i", eqGains[i]).apply()
                                            },
                                            valueRange = -12f..12f,
                                            modifier = Modifier
                                                .requiredWidth(240.dp)
                                                .rotate(-90f)
                                        )
                                    }
                                    Text(
                                        labels[i],
                                        style = MaterialTheme.typography.caption,
                                        modifier = Modifier.padding(top = 8.dp)
                                    )
                                }
                            }
                        }
                    }
                    
                    // Custom preset actions
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        OutlinedButton(onClick = {
                            saveEQName = activeEQName ?: ""
                            showSaveEQDialog = true
                        }, modifier = Modifier.weight(1f)) {
                            Text("Save Preset")
                        }
                        val canDelete = activeEQName != null && eqPresetNames.contains(activeEQName)
                        OutlinedButton(
                            onClick = {
                                if (canDelete) {
                                    activeEQName?.let { deleteCustomEQPreset(context, it) }
                                    activeEQName = null
                                    setActiveCustomEQPresetName(context, null)
                                    eqPresetNames = loadCustomEQPresetNames(context)
                                }
                            },
                            enabled = canDelete
                        ) {
                            Text("Delete")
                        }
                    }
                }
            }
        }
    }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp)
    ) {
        if (isLandscape) {
            // Landscape: 2-column layout
            // Sound Bank Button
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
                            Icons.Filled.LibraryMusic,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Sound Bank",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
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
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(12.dp),
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
                                    color = MaterialTheme.colors.onSurface,
                                    modifier = Modifier.weight(1f),
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis
                                )
                                BankFlavorBadgesRow(
                                    bankPath = bankPath,
                                    hasEggsBank = hasEggsBank,
                                    hasMobileBAEBank = hasMobileBAEBank,
                                    dlsBankLevel = dlsBankLevel,
                                    dlsCompatibilityMode = dlsCompatibilityMode,
                                    hasXmfOverlay = hasXmfOverlay,
                                    hasRmiEmbedded = hasRmiEmbedded,
                                    rmiUsesSf2 = rmiUsesSf2,
                                )
                            }
                        }

                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "Get more from SoundMusicSys",
                            style = MaterialTheme.typography.caption.copy(textDecoration = TextDecoration.Underline),
                            color = MaterialTheme.colors.primary,
                            modifier = Modifier.clickable {
                                try {
                                    val url = "https://www.soundmusicsys.com/content/PatchBanks/"
                                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url))
                                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                                    context.startActivity(intent)
                                } catch (_: Exception) {
                                    Toast.makeText(context, "Unable to open link", Toast.LENGTH_SHORT).show()
                                }
                            }
                        )
                    }
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    Button(
                        onClick = onBrowseBanks,
                        modifier = Modifier.fillMaxWidth(),
                        enabled = !isLoadingBank
                    ) {
                        Icon(Icons.Filled.LibraryMusic, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Change Sound Bank")
                    }
                    
                    Text(
                        text = "Supports HSB, SF2, SF3, DLS formats • Hot-swap: reloads current song automatically",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            EqualizerCard()
            Spacer(modifier = Modifier.height(16.dp))
            
            // Row 1: Reverb and Velocity Curve
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                // Reverb Section
                Card(
                    modifier = Modifier.weight(1f),
                    elevation = 4.dp,
                    shape = RoundedCornerShape(12.dp)
                ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.GraphicEq,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Reverb",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                Box {
                    OutlinedButton(
                        onClick = { reverbExpanded = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = selectedReverbLabel,
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
                                when {
                                    index < customEntryIndex -> {
                                        clearActivePreset()
                                        val selectedReverbType = index + 1
                                        onReverbChange(selectedReverbType)
                                        // For Neo presets (REVERB_TYPE_13-17), load their values into custom reverb for editing
                                        if (selectedReverbType in 13..17) {
                                            val preset = getNeoReverbPreset(context, selectedReverbType, option)
                                            applyCustomReverbPresetToEngine(context, preset)
                                            onCustomReverbSync()
                                        }
                                    }
                                    index == customEntryIndex -> {
                                        clearActivePreset()
                                        onReverbChange(CUSTOM_REVERB_TYPE)
                                        applyDefaultCustomReverbToEngine(context)
                                        onCustomReverbSync()
                                    }
                                    else -> {
                                        val presetName = presetNames.getOrNull(index - customEntryIndex - 1)
                                        if (presetName != null) {
                                            applyPresetByName(presetName)
                                        }
                                    }
                                }
                                reverbExpanded = false
                            }) {
                                Text(option)
                            }
                        }
                    }
                }

                if (reverbType >= 12 && reverbType != 18) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                        OutlinedButton(onClick = onOpenCustomReverb, modifier = Modifier.weight(1f)) {
                            Text("Custom")
                        }
                        OutlinedButton(onClick = {
                            savePresetName = activePresetName ?: ""
                            showSavePresetDialog = true
                        }) {
                            Text("+")
                        }
                        val canDelete = activePresetName != null && presetNames.contains(activePresetName)
                        OutlinedButton(
                            onClick = { if (canDelete) showDeletePresetDialog = true },
                            enabled = canDelete
                        ) {
                            Text("-")
                        }

                        OutlinedButton(
                            onClick = { importNeoReverbLauncher.launch(arrayOf("application/xml", "text/xml", "*/*")) }
                        ) {
                            Icon(Icons.Filled.GetApp, contentDescription = "Import Preset")
                        }

                        val canExport = activePresetName != null && presetNames.contains(activePresetName)
                        OutlinedButton(
                            onClick = {
                                val safe = sanitizePresetNameForFilename(activePresetName ?: "preset")
                                exportNeoReverbLauncher.launch("$safe.neoreverb")
                            },
                            enabled = canExport
                        ) {
                            Icon(Icons.Filled.Publish, contentDescription = "Export Preset")
                        }
                    }
                }
            }
        }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Row 2: Velocity Curve and Export Codec
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
        // Velocity Curve Section
        Card(
            modifier = Modifier.weight(1f),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.TrendingUp,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Velocity Curve (HSB Only)",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                Box {
                    OutlinedButton(
                        onClick = { curveExpanded = true },
                        enabled = !webtvMode,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            // WebTV Mode forces the WebTV curve; the saved choice returns when it is off.
                            text = if (webtvMode) curveOptions[2] else (curveOptions.getOrNull(velocityCurve) ?: "Default"),
                            modifier = Modifier.weight(1f)
                        )
                        Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                    }
                    DropdownMenu(
                        expanded = curveExpanded && !webtvMode,
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
        
                // Export Codec Section
                Card(
                    modifier = Modifier.weight(1f),
                    elevation = 4.dp,
                    shape = RoundedCornerShape(12.dp)
                ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.padding(bottom = 12.dp)
                ) {
                    Icon(
                        Icons.Filled.GetApp,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Export Codec",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                Box {
                    OutlinedButton(
                        onClick = { exportCodecExpanded = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(
                            text = exportCodecOptions.getOrNull(exportCodec - 1) ?: "OGG",
                            modifier = Modifier.weight(1f)
                        )
                        Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                    }
                    DropdownMenu(
                        expanded = exportCodecExpanded,
                        onDismissRequest = { exportCodecExpanded = false }
                    ) {
                        exportCodecOptions.forEachIndexed { index, option ->
                            DropdownMenuItem(onClick = {
                                onExportCodecChange(index + 1)
                                exportCodecExpanded = false
                            }) {
                                Text(option)
                            }
                        }
                    }
                }
            }
        }
            }
            
            Spacer(modifier = Modifier.height(16.dp))

        // BAEScript entry (full width in landscape)
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
                        Icons.Filled.Code,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "BAEScript",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Switch(
                        checked = baeScriptEnabled,
                        onCheckedChange = onBaeScriptEnabledChange
                    )
                }

                OutlinedTextField(
                    value = baeScriptSource,
                    onValueChange = onBaeScriptSourceChange,
                    enabled = true,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(160.dp),
                    label = { Text("Script") },
                    placeholder = { Text("on.script ({ exporter.loopcount = 2; });") },
                    singleLine = false
                )

                Spacer(modifier = Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = { importBaeScriptLauncher.launch(arrayOf("text/plain", "application/octet-stream")) },
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Filled.GetApp, contentDescription = "Load BAEScript")
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Load")
                    }
                    OutlinedButton(
                        onClick = { exportBaeScriptLauncher.launch("script.bscript") },
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Filled.Publish, contentDescription = "Save BAEScript")
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Save")
                    }
                }
            }
        }

            Spacer(modifier = Modifier.height(16.dp))

        // File Types entry (full width in landscape)
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
                        Icons.Filled.Audiotrack,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "File Types",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }

                Text(
                    text = "Choose which file types appear in the app",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                )

                Spacer(modifier = Modifier.height(12.dp))

                Button(
                    onClick = onOpenFileTypes,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Filled.Audiotrack, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Choose File Types")
                }
            }
        }

        if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
            Spacer(modifier = Modifier.height(16.dp))
            FolderAccessManagerCard()
        }

        if (showSearchFeatures) {
            Spacer(modifier = Modifier.height(16.dp))

            // Search Result Limit Section (full width in landscape)
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
                            Icons.Filled.Search,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Search Result Limit",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }

                    Box {
                        OutlinedButton(
                            onClick = { searchLimitExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = searchLimitOptions.find { it.first == searchResultLimit }?.second ?: "1,000",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = searchLimitExpanded,
                            onDismissRequest = { searchLimitExpanded = false }
                        ) {
                            searchLimitOptions.forEach { (value, label) ->
                                DropdownMenuItem(onClick = {
                                    if (value == -1) {
                                        // Show warning for unlimited
                                        showUnlimitedWarning = true
                                        searchLimitExpanded = false
                                    } else {
                                        onSearchLimitChange(value)
                                        searchLimitExpanded = false
                                    }
                                }) {
                                    Text(label)
                                }
                            }
                        }
                    }

                    Text(
                        text = "Maximum number of search results to display. Lower limits improve performance.",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
        }
        
        // Warning dialog for unlimited option
        if (showSearchFeatures && showUnlimitedWarning) {
            androidx.compose.material.AlertDialog(
                onDismissRequest = { showUnlimitedWarning = false },
                title = { Text("Warning") },
                text = {
                    Text("Setting the search limit to Unlimited may cause slowdowns and increased memory usage with large databases. Are you sure you want to continue?")
                },
                confirmButton = {
                    androidx.compose.material.TextButton(
                        onClick = {
                            onSearchLimitChange(-1)
                            showUnlimitedWarning = false
                        }
                    ) {
                        Text("Yes, Unlimited")
                    }
                },
                dismissButton = {
                    androidx.compose.material.TextButton(
                        onClick = { showUnlimitedWarning = false }
                    ) {
                        Text("Cancel")
                    }
                }
            )
        }
        } else {
            // Portrait: vertical layout (original)
            // Sound Bank Section
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
                            Icons.Filled.LibraryMusic,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Sound Bank",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
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
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(12.dp),
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
                                    color = MaterialTheme.colors.onSurface,
                                    modifier = Modifier.weight(1f),
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis
                                )
                                BankFlavorBadgesRow(
                                    bankPath = bankPath,
                                    hasEggsBank = hasEggsBank,
                                    hasMobileBAEBank = hasMobileBAEBank,
                                    dlsBankLevel = dlsBankLevel,
                                    dlsCompatibilityMode = dlsCompatibilityMode,
                                    hasXmfOverlay = hasXmfOverlay,
                                    hasRmiEmbedded = hasRmiEmbedded,
                                    rmiUsesSf2 = rmiUsesSf2,
                                )
                            }
                        }

                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "Get more from SoundMusicSys",
                            style = MaterialTheme.typography.caption.copy(textDecoration = TextDecoration.Underline),
                            color = MaterialTheme.colors.primary,
                            modifier = Modifier.clickable {
                                try {
                                    val url = "https://www.soundmusicsys.com/content/PatchBanks/"
                                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url))
                                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                                    context.startActivity(intent)
                                } catch (_: Exception) {
                                    Toast.makeText(context, "Unable to open link", Toast.LENGTH_SHORT).show()
                                }
                            }
                        )
                    }
                    
                    Spacer(modifier = Modifier.height(12.dp))
                    
                    Button(
                        onClick = onBrowseBanks,
                        modifier = Modifier.fillMaxWidth(),
                        enabled = !isLoadingBank
                    ) {
                        Icon(Icons.Filled.LibraryMusic, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Change Sound Bank")
                    }
                    
                    Text(
                        text = "Supports HSB, SF2, SF3, DLS formats • Hot-swap: reloads current song automatically",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            EqualizerCard()
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Reverb Section
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
                            Icons.Filled.GraphicEq,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Reverb",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    Box {
                        OutlinedButton(
                            onClick = { reverbExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = selectedReverbLabel,
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
                                    when {
                                        index < customEntryIndex -> {
                                            clearActivePreset()
                                            onReverbChange(index + 1)
                                        }
                                        index == customEntryIndex -> {
                                            clearActivePreset()
                                            onReverbChange(CUSTOM_REVERB_TYPE)
                                            applyDefaultCustomReverbToEngine(context)
                                            onCustomReverbSync()
                                        }
                                        else -> {
                                            val presetName = presetNames.getOrNull(index - customEntryIndex - 1)
                                            if (presetName != null) {
                                                applyPresetByName(presetName)
                                            }
                                        }
                                    }
                                    reverbExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }

                    if (reverbType >= 12 && reverbType != 17 && reverbType != 18) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Column(modifier = Modifier.fillMaxWidth()) {
                            // Keep the small action buttons in one row for portrait.
                            Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                OutlinedButton(
                                    onClick = {
                                        savePresetName = activePresetName ?: ""
                                        showSavePresetDialog = true
                                    },
                                    modifier = Modifier.weight(1f)
                                ) {
                                    Text("+")
                                }

                                val canDelete = activePresetName != null && presetNames.contains(activePresetName)
                                OutlinedButton(
                                    onClick = { if (canDelete) showDeletePresetDialog = true },
                                    enabled = canDelete,
                                    modifier = Modifier.weight(1f)
                                ) {
                                    Text("-")
                                }

                                OutlinedButton(
                                    onClick = { importNeoReverbLauncher.launch(arrayOf("application/xml", "text/xml", "*/*")) },
                                    modifier = Modifier.weight(1f)
                                ) {
                                    Icon(Icons.Filled.GetApp, contentDescription = "Import Preset")
                                }

                                val canExport = activePresetName != null && presetNames.contains(activePresetName)
                                OutlinedButton(
                                    onClick = {
                                        val safe = sanitizePresetNameForFilename(activePresetName ?: "preset")
                                        exportNeoReverbLauncher.launch("$safe.neoreverb")
                                    },
                                    enabled = canExport,
                                    modifier = Modifier.weight(1f)
                                ) {
                                    Icon(Icons.Filled.Publish, contentDescription = "Export Preset")
                                }
                            }

                            Spacer(modifier = Modifier.height(8.dp))

                            // Put Custom on its own line so it doesn't get squished.
                            OutlinedButton(
                                onClick = onOpenCustomReverb,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text("Custom")
                            }
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Velocity Curve Section
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
                            Icons.AutoMirrored.Filled.TrendingUp,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Velocity Curve (HSB Only)",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    Box {
                        OutlinedButton(
                            onClick = { curveExpanded = true },
                            enabled = !webtvMode,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                // WebTV Mode forces the WebTV curve; the saved choice returns when it is off.
                                text = if (webtvMode) curveOptions[2] else (curveOptions.getOrNull(velocityCurve) ?: "Default"),
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = curveExpanded && !webtvMode,
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
            
            // Export Codec Section
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
                            Icons.Filled.GetApp,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Export Codec",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }
                    
                    Box {
                        OutlinedButton(
                            onClick = { exportCodecExpanded = true },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(
                                text = exportCodecOptions.getOrNull(exportCodec - 1) ?: "OGG",
                                modifier = Modifier.weight(1f)
                            )
                            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                        }
                        DropdownMenu(
                            expanded = exportCodecExpanded,
                            onDismissRequest = { exportCodecExpanded = false }
                        ) {
                            exportCodecOptions.forEachIndexed { index, option ->
                                DropdownMenuItem(onClick = {
                                    onExportCodecChange(index + 1)
                                    exportCodecExpanded = false
                                }) {
                                    Text(option)
                                }
                            }
                        }
                    }
                }
            }
            
            Spacer(modifier = Modifier.height(16.dp))

            // BAEScript section
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
                            Icons.Filled.Code,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "BAEScript",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary,
                            modifier = Modifier.weight(1f)
                        )
                        Switch(
                            checked = baeScriptEnabled,
                            onCheckedChange = onBaeScriptEnabledChange
                        )
                    }

                    OutlinedTextField(
                        value = baeScriptSource,
                        onValueChange = onBaeScriptSourceChange,
                        enabled = true,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(160.dp),
                        label = { Text("Script") },
                        placeholder = { Text("on.script { exporter.loopcount = 2 }") },
                        singleLine = false
                    )

                    Spacer(modifier = Modifier.height(8.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedButton(
                            onClick = { importBaeScriptLauncher.launch(arrayOf("text/plain", "application/octet-stream")) },
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(Icons.Filled.GetApp, contentDescription = "Load BAEScript")
                            Spacer(modifier = Modifier.width(8.dp))
                            Text("Load")
                        }
                        OutlinedButton(
                            onClick = { exportBaeScriptLauncher.launch("script.bscript") },
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(Icons.Filled.Publish, contentDescription = "Save BAEScript")
                            Spacer(modifier = Modifier.width(8.dp))
                            Text("Save")
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // File Types entry
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
                            Icons.Filled.Audiotrack,
                            contentDescription = null,
                            tint = MaterialTheme.colors.primary,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "File Types",
                            style = MaterialTheme.typography.h6,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colors.primary
                        )
                    }

                    Text(
                        text = "Choose which file types appear in the app",
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                    )

                    Spacer(modifier = Modifier.height(12.dp))

                    Button(
                        onClick = onOpenFileTypes,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Filled.Audiotrack, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Choose File Types")
                    }
                }
            }

            if (!BuildConfig.USE_MANAGE_EXTERNAL_STORAGE) {
                Spacer(modifier = Modifier.height(16.dp))
                FolderAccessManagerCard()
            }

            if (showSearchFeatures) {
                Spacer(modifier = Modifier.height(16.dp))

                // Search Result Limit Section
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
                                Icons.Filled.Search,
                                contentDescription = null,
                                tint = MaterialTheme.colors.primary,
                                modifier = Modifier.size(24.dp)
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text(
                                text = "Search Result Limit",
                                style = MaterialTheme.typography.h6,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colors.primary
                            )
                        }

                        Box {
                            OutlinedButton(
                                onClick = { searchLimitExpanded = true },
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Text(
                                    text = searchLimitOptions.find { it.first == searchResultLimit }?.second ?: "1,000",
                                    modifier = Modifier.weight(1f)
                                )
                                Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
                            }
                            DropdownMenu(
                                expanded = searchLimitExpanded,
                                onDismissRequest = { searchLimitExpanded = false }
                            ) {
                                searchLimitOptions.forEach { (value, label) ->
                                    DropdownMenuItem(onClick = {
                                        if (value == -1) {
                                            // Show warning for unlimited
                                            showUnlimitedWarning = true
                                            searchLimitExpanded = false
                                        } else {
                                            onSearchLimitChange(value)
                                            searchLimitExpanded = false
                                        }
                                    }) {
                                        Text(label)
                                    }
                                }
                            }
                        }

                        Text(
                            text = "Maximum number of search results to display. Lower limits improve performance.",
                            style = MaterialTheme.typography.caption,
                            color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                            modifier = Modifier.padding(top = 8.dp)
                        )
                    }
                }
            }
        }
        
        // Warning dialog for unlimited option (outside if/else)
        if (showSearchFeatures && showUnlimitedWarning) {
            androidx.compose.material.AlertDialog(
                onDismissRequest = { showUnlimitedWarning = false },
                title = { Text("Warning") },
                text = {
                    Text("Setting the search limit to Unlimited may cause slowdowns and increased memory usage with large databases. Are you sure you want to continue?")
                },
                confirmButton = {
                    androidx.compose.material.TextButton(
                        onClick = {
                            onSearchLimitChange(-1)
                            showUnlimitedWarning = false
                        }
                    ) {
                        Text("Yes, Unlimited")
                    }
                },
                dismissButton = {
                    androidx.compose.material.TextButton(
                        onClick = { showUnlimitedWarning = false }
                    ) {
                        Text("Cancel")
                    }
                }
            )
        }
        
        Spacer(modifier = Modifier.height(16.dp))

        // Fix Pan LFO Bias Section (same for both orientations)
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onFixPanLfoChange(!fixPanLfoBias) },
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Filled.Tune,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Fix Pan LFO Bias",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Checkbox(
                        checked = fixPanLfoBias,
                        onCheckedChange = { onFixPanLfoChange(it) }
                    )
                }
                Text(
                    text = "Corrects extreme panning caused by stereo pan LFO DC offset accumulation in HSB instruments.",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))

        // Classic Chorus Order Section (same for both orientations)
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onClassicChorusChange(!classicChorus) },
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Filled.Tune,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Classic Chorus Order",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Checkbox(
                        checked = classicChorus,
                        onCheckedChange = { onClassicChorusChange(it) }
                    )
                }
                Text(
                    text = "Uses pre-DLS Beatnik chorus ordering (reverb before chorus). Disable for DLS-spec compliant ordering.",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))

        // WebTV Mode Section (same for both orientations)
        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onWebTVModeChange(!webtvMode) },
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Filled.Tune,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "WebTV Mode",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Checkbox(
                        checked = webtvMode,
                        onCheckedChange = { onWebTVModeChange(it) }
                    )
                }
                Text(
                    text = "Modifies synth params to be more accurate to WebTV",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            onDLSCompatibilityModeChange(!dlsCompatibilityMode)
                        },
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Filled.Tune,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Smart DLS Handling",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Checkbox(
                        checked = dlsCompatibilityMode,
                        onCheckedChange = { onDLSCompatibilityModeChange(it) }
                    )
                }
                Text(
                    text = "Enables Smart DLS Handling. Disable to force all DLS to use mobileBAE behavior.",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        Card(
            modifier = Modifier.fillMaxWidth(),
            elevation = 4.dp,
            shape = RoundedCornerShape(12.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { onNormalizePlaybackChange(!normalizePlayback) },
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        Icons.Filled.Tune,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Normalize Playback",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary,
                        modifier = Modifier.weight(1f)
                    )
                    Checkbox(
                        checked = normalizePlayback,
                        onCheckedChange = { onNormalizePlaybackChange(it) }
                    )
                }
                Text(
                    text = "Fast normalization by scanning MIDI + patch data and creating a peak estimate.",
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // About Section (same for both orientations)
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
                        Icons.Filled.Info,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "About",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }
                
                val baeVersion = remember { Mixer.getVersion() ?: "Unknown" }
                val baeCompileInfo = remember { Mixer.getCompileInfo() ?: "Unknown" }
                val baeFeatures = remember { Mixer.getFeatureString() ?: "" }
                val appTitle = remember(context) {
                    runCatching { context.applicationInfo.loadLabel(context.packageManager).toString() }
                        .getOrElse { "NeoBAE" }
                }
                
                Text(
                    text = appTitle,
                    style = MaterialTheme.typography.body1,
                    fontWeight = FontWeight.SemiBold
                )
                Spacer(modifier = Modifier.height(4.dp))
                if (isPlaySafVariant()) {
                    Text(
                        text = "A multimedia engine with MIDI Synth and more, Play Store version.",
                        style = MaterialTheme.typography.body2,
                        color = Color.Gray
                    )
                } else {
                    Text(
                        text = "A multimedia engine with MIDI Synth and more, OSS version.",
                        style = MaterialTheme.typography.body2,
                        color = Color.Gray
                    )
                }
                
                if (baeVersion.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(12.dp))
                    val versionUrl = remember(baeVersion) { buildGitHubUrlForBaeVersion(baeVersion) }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = "Version: ",
                            style = MaterialTheme.typography.body2,
                            color = MaterialTheme.colors.onSurface
                        )
                        Text(
                            text = baeVersion,
                            style = MaterialTheme.typography.body2.copy(
                                textDecoration = if (versionUrl != null) TextDecoration.Underline else null
                            ),
                            color = if (versionUrl != null) MaterialTheme.colors.primary else MaterialTheme.colors.onSurface,
                            modifier = if (versionUrl != null) {
                                Modifier.clickable {
                                    try {
                                        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(versionUrl))
                                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                                        context.startActivity(intent)
                                    } catch (_: Exception) {
                                        Toast.makeText(context, "Unable to open link", Toast.LENGTH_SHORT).show()
                                    }
                                }
                            } else {
                                Modifier
                            }
                        )
                    }
                }
                
                if (baeCompileInfo.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "Compiled with: $baeCompileInfo",
                        style = MaterialTheme.typography.body2,
                        color = MaterialTheme.colors.onSurface
                    )
                }
                
                if (baeFeatures.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Features:",
                        style = MaterialTheme.typography.caption,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f)
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = baeFeatures,
                        style = MaterialTheme.typography.caption,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.6f)
                    )
                }
            }
        }
    }

    if (showSavePresetDialog) {
        AlertDialog(
            onDismissRequest = { showSavePresetDialog = false },
            title = { Text("Save Custom Reverb Preset") },
            text = {
                OutlinedTextField(
                    value = savePresetName,
                    onValueChange = { savePresetName = it },
                    label = { Text("Preset name") },
                    singleLine = true
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    val name = savePresetName.trim()
                    if (name.isNotEmpty()) {
                        val preset = snapshotCustomReverbFromEngine(context, name)
                        saveCustomReverbPreset(context, preset)
                        presetNames = loadCustomReverbPresetNames(context)
                        activePresetName = name
                        onReverbChange(CUSTOM_REVERB_TYPE)
                        onCustomReverbSync()
                    }
                    showSavePresetDialog = false
                }) { Text("Save") }
            },
            dismissButton = {
                TextButton(onClick = { showSavePresetDialog = false }) { Text("Cancel") }
            }
        )
    }

    if (showDeletePresetDialog) {
        val name = activePresetName
        AlertDialog(
            onDismissRequest = { showDeletePresetDialog = false },
            title = { Text("Delete Preset") },
            text = { Text(if (name.isNullOrEmpty()) "Delete preset?" else "Delete preset \"$name\"?") },
            confirmButton = {
                TextButton(onClick = {
                    if (!name.isNullOrEmpty()) {
                        deleteCustomReverbPreset(context, name)
                        presetNames = loadCustomReverbPresetNames(context)
                        clearActivePreset()
                        onReverbChange(CUSTOM_REVERB_TYPE)
                        onCustomReverbSync()
                    }
                    showDeletePresetDialog = false
                }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { showDeletePresetDialog = false }) { Text("Cancel") }
            }
        )
    }

    FolderAccessManagerDialog()
}

@Composable
fun FileTypesScreenContent(
    enabledExtensions: Set<String>,
    onExtensionEnabledChange: (String, Boolean) -> Unit
) {
    val scrollState = rememberScrollState()
    val context = LocalContext.current
    val supportedExtensions = remember { HomeFragment.getSupportedExtensionsForSettings(context) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scrollState)
            .padding(16.dp)
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
                        Icons.Filled.Audiotrack,
                        contentDescription = null,
                        tint = MaterialTheme.colors.primary,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "File Types",
                        style = MaterialTheme.typography.h6,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colors.primary
                    )
                }

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = {
                            supportedExtensions.forEach { ext ->
                                if (!enabledExtensions.contains(ext)) {
                                    onExtensionEnabledChange(ext, true)
                                }
                            }
                        }
                    ) {
                        Text("Check All")
                    }

                    OutlinedButton(
                        onClick = {
                            supportedExtensions.forEach { ext ->
                                val shouldEnable = !HomeFragment.isSoundExtension(ext)
                                if (enabledExtensions.contains(ext) != shouldEnable) {
                                    onExtensionEnabledChange(ext, shouldEnable)
                                }
                            }
                        }
                    ) {
                        Text("MIDI")
                    }

                    OutlinedButton(
                        onClick = {
                            supportedExtensions.forEach { ext ->
                                val shouldEnable = HomeFragment.isSoundExtension(ext)
                                if (enabledExtensions.contains(ext) != shouldEnable) {
                                    onExtensionEnabledChange(ext, shouldEnable)
                                }
                            }
                        }
                    ) {
                        Text("Audio")
                    }
                }

                supportedExtensions.forEach { ext ->
                    val checked = enabledExtensions.contains(ext)
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp)
                            .clickable { onExtensionEnabledChange(ext, !checked) },
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Checkbox(
                            checked = checked,
                            onCheckedChange = { onExtensionEnabledChange(ext, it) }
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = ".$ext",
                            style = MaterialTheme.typography.body1
                        )
                    }
                }
            }

        }

    }
}
