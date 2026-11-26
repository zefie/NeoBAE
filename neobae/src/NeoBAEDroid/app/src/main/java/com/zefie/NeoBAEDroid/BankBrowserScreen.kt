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
fun BankBrowserScreen(
    currentPath: String,
    files: List<PlaylistItem>,
    isLoading: Boolean,
    onNavigate: (String) -> Unit,
    onSelectBank: (File) -> Unit,
    onLoadBuiltin: () -> Unit,
    showPickFolderAction: Boolean,
    onPickFolder: () -> Unit,
    onClose: () -> Unit
) {
    // In SAF builds "/" is the saved-folders list (every folder the user has authorized),
    // and ".." at a SAF tree root returns there, matching the main file browser.
    val atSafRoot = showPickFolderAction && !currentPath.startsWith("content://")
    val parentPath: String? = when {
        currentPath.startsWith("content://") ->
            getSafParentPath(currentPath) ?: if (showPickFolderAction) "/" else null
        currentPath == "/" || atSafRoot -> null
        else -> File(currentPath).parent
    }

    Column(modifier = Modifier.fillMaxSize()) {
        // Current path bar
        Surface(
            modifier = Modifier.fillMaxWidth(),
            color = MaterialTheme.colors.surface,
            elevation = 2.dp
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    Icons.Filled.Folder,
                    contentDescription = null,
                    tint = MaterialTheme.colors.onSurface.copy(alpha = 0.6f),
                    modifier = Modifier.size(16.dp)
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = if (atSafRoot) "Saved Folders" else getDisplayPath(currentPath),
                    style = MaterialTheme.typography.caption,
                    color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        
        // File list
        when {
            isLoading -> {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = MaterialTheme.colors.primary)
                }
            }
            files.isEmpty() -> {
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    if (showPickFolderAction) {
                        item {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onPickFolder() },
                                color = MaterialTheme.colors.primary.copy(alpha = 0.06f)
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(
                                        Icons.Filled.FolderOpen,
                                        contentDescription = null,
                                        tint = MaterialTheme.colors.primary,
                                        modifier = Modifier.size(24.dp)
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = if (atSafRoot) "Add Folder" else "Choose Different Folder",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.SemiBold,
                                        color = MaterialTheme.colors.onBackground
                                    )
                                }
                            }
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }

                    // Built-in Patches option at the top
                    item {
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { 
                                    onLoadBuiltin()
                                    onClose()
                                },
                            color = MaterialTheme.colors.primary.copy(alpha = 0.05f)
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.GetApp,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.secondary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = "Built-in Patches",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground
                                    )
                                    Text(
                                        text = "Beatnik Standard (v1.1.1, uncomp)",
                                        fontSize = 12.sp,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.onBackground.copy(alpha = 0.4f)
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                    
                    // Show parent directory ".." if not at root
                    if (parentPath != null) {
                        item {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onNavigate(parentPath) },
                                color = Color.Transparent
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(
                                        Icons.Filled.Folder,
                                        contentDescription = null,
                                        tint = MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                                        modifier = Modifier.size(40.dp)
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = "..",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                            }
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }
                    
                    // Empty folder message
                    item {
                        Box(modifier = Modifier.fillMaxWidth().padding(vertical = 32.dp), contentAlignment = Alignment.Center) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                Icon(
                                    Icons.Filled.FolderOpen,
                                    contentDescription = null,
                                    modifier = Modifier.size(64.dp),
                                    tint = Color.Gray
                                )
                                Spacer(modifier = Modifier.height(16.dp))
                                Text(if (atSafRoot) "No saved folders yet" else "No bank files in this folder", color = Color.Gray)
                                Spacer(modifier = Modifier.height(8.dp))
                                Text(
                                    "Supported: .sf2, .sf3, .sfo, .hsb, .dls",
                                    fontSize = 12.sp,
                                    color = Color.Gray
                                )
                            }
                        }
                    }
                }
            }
            else -> {
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    if (showPickFolderAction) {
                        item {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onPickFolder() },
                                color = MaterialTheme.colors.primary.copy(alpha = 0.06f)
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(
                                        Icons.Filled.FolderOpen,
                                        contentDescription = null,
                                        tint = MaterialTheme.colors.primary,
                                        modifier = Modifier.size(24.dp)
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = if (atSafRoot) "Add Folder" else "Choose Different Folder",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.SemiBold,
                                        color = MaterialTheme.colors.onBackground
                                    )
                                }
                            }
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }

                    // Built-in Patches option at the top
                    item {
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { 
                                    onLoadBuiltin()
                                    onClose()
                                },
                            color = MaterialTheme.colors.primary.copy(alpha = 0.05f)
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 14.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.GetApp,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.secondary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = "Built-in Patches",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground
                                    )
                                    Text(
                                        text = "Beatnik Standard (v1.1.1, uncomp)",
                                        fontSize = 12.sp,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.onBackground.copy(alpha = 0.4f)
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                    
                    // Show parent directory ".." if not at root
                    if (parentPath != null) {
                        item {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onNavigate(parentPath) },
                                color = Color.Transparent
                            ) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 16.dp, vertical = 12.dp),
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Icon(
                                        Icons.Filled.Folder,
                                        contentDescription = null,
                                        tint = MaterialTheme.colors.onBackground.copy(alpha = 0.6f),
                                        modifier = Modifier.size(40.dp)
                                    )
                                    Spacer(modifier = Modifier.width(12.dp))
                                    Text(
                                        text = "..",
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Bold,
                                        color = MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                                    )
                                }
                            }
                            Divider(color = Color.Gray.copy(alpha = 0.2f))
                        }
                    }
                    
                    // Show folders
                    itemsIndexed(files.filter { it.isFolder }) { _, item ->
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onNavigate(item.path) },
                            color = Color.Transparent
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.Folder,
                                    contentDescription = null,
                                    tint = MaterialTheme.colors.primary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Text(
                                    text = item.title,
                                    fontSize = 14.sp,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colors.onBackground
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                    
                    // Show bank files
                    itemsIndexed(files.filter { !it.isFolder }) { _, item ->
                        val fileSizeBytes = resolvePlaylistItemSize(item).coerceAtLeast(0L)
                        val isTooLarge = fileSizeBytes >= BANK_SIZE_LIMIT_BYTES
                        val disabledColor = MaterialTheme.colors.onBackground.copy(alpha = 0.38f)
                        val primaryTextColor = if (isTooLarge) disabledColor else MaterialTheme.colors.onBackground
                        val secondaryTextColor = if (isTooLarge) disabledColor else MaterialTheme.colors.onBackground.copy(alpha = 0.6f)
                        Surface(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable(enabled = !isTooLarge) { onSelectBank(item.file) },
                            color = Color.Transparent
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 16.dp, vertical = 12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Icon(
                                    Icons.Filled.LibraryMusic,
                                    contentDescription = null,
                                    tint = if (isTooLarge) disabledColor else MaterialTheme.colors.secondary,
                                    modifier = Modifier.size(40.dp)
                                )
                                Spacer(modifier = Modifier.width(12.dp))
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = item.title,
                                        fontSize = 14.sp,
                                        fontWeight = FontWeight.Normal,
                                        color = primaryTextColor,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                    Text(
                                        text = run {
                                            val ext = item.file.extension.uppercase()
                                            if (fileSizeBytes > 0) "$ext • ${formatFileSize(fileSizeBytes)}" else ext
                                        },
                                        fontSize = 12.sp,
                                        color = secondaryTextColor
                                    )
                                }
                                Icon(
                                    Icons.AutoMirrored.Filled.ArrowForward,
                                    contentDescription = null,
                                    tint = if (isTooLarge) disabledColor else MaterialTheme.colors.onBackground.copy(alpha = 0.4f)
                                )
                            }
                        }
                        Divider(color = Color.Gray.copy(alpha = 0.2f))
                    }
                }
            }
        }
    }
}
