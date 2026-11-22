package com.zefie.NeoBAEDroid

import android.content.Context
import androidx.compose.runtime.mutableStateOf

/**
 * Shared preferences keys, extension lists, and process-wide playback knobs.
 * Formerly HomeFragment.companion.
 */
object NeoBAEPrefs {
    val BUILT_IN_REVERB_OPTIONS = listOf(
        "None", "Igor's Closet", "Igor's Garage", "Igor's Acoustic Lab",
        "Igor's Cavern", "Igor's Dungeon", "Small Reflections",
        "Early Reflections", "Basement", "Banquet Hall", "Catacombs",
        "Neo Room", "Neo Hall", "Neo Cavern", "Neo Dungeon", "Neo Nokia", "MobileBAE", "Neo Tap Delay"
    )

    val CUSTOM_REVERB_TYPE: Int
        get() = BUILT_IN_REVERB_OPTIONS.size + 1

    // Default to the 2nd option ("NeoBAE S Curve" in the UI)
    val velocityCurve = mutableStateOf(0)
    // Fix Pan LFO DC bias (on by default)
    val fixPanLfoBias = mutableStateOf(true)
    // Classic chorus ordering (off by default)
    val classicChorus = mutableStateOf(false)
    // WebTV Mode: WebTV velocity curve, LFO shapes, default pans, filter glide (off by default)
    val webtvMode = mutableStateOf(false)
    // Native DLS compatibility mode (disables MobileBAE quirks, off by default)
    val dlsCompatibilityMode = mutableStateOf(true) /* default on for new installs */
    // Optional whole-song peak normalize via MIDI+patch estimate (off by default)
    val normalizePlayback = mutableStateOf(false)

    const val PREF_NAME = "NeoBAE_prefs"
    const val KEY_ENABLE_AUDIO_FILES = "enable_audio_files"
    const val KEY_ENABLED_EXTENSIONS = "enabled_extensions"
    const val KEY_HOME_SORT_MODE = "home_sort_mode"
    const val KEY_SEARCH_SORT_MODE = "search_sort_mode"

    // Valid music file extensions
    val AUDIO_EXTENSIONS = listOf("wav", "ogg", "opus", "flac", "au", "mp2", "mp3", "aif", "aiff", "adp", "qoa", "adx", "asf", "wma")
    val SONG_EXTENSIONS = listOf("mid", "midi", "kar", "rmf", "zmf", "xmf", "mxmf", "rmi", "seq", "re", "mthc", "imy", "emy", "rng", "rtx", "ott")

    // Valid sound bank file extensions
    val BANK_EXTENSIONS = setOf("sf2", "hsb", "zsb", "sf3", "sfo", "dls")

    // Get appropriate music extensions based on build type
    fun getMusicExtensions(context: Context): Set<String> {
        val base = SONG_EXTENSIONS.toSet()
        val supported = base + AUDIO_EXTENSIONS
        val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
        val saved = prefs.getStringSet(KEY_ENABLED_EXTENSIONS, null)
        if (saved != null) {
            return saved.map { it.lowercase() }.toSet().intersect(supported)
        }

        // Backwards-compat: fall back to the old audio toggle if the per-extension
        // list hasn't been set yet.
        val enableAudio = prefs.getBoolean(KEY_ENABLE_AUDIO_FILES, true)
        return if (enableAudio) base + AUDIO_EXTENSIONS else base
    }

    fun getSupportedExtensionsForSettings(context: Context): List<String> {
        val base = SONG_EXTENSIONS
        return (base + AUDIO_EXTENSIONS).distinct()
    }

    fun isSoundExtension(extension: String): Boolean {
        return AUDIO_EXTENSIONS.contains(extension.lowercase())
    }
}


/** Package aliases for call sites that used the old private top-level names. */
internal val BUILT_IN_REVERB_OPTIONS get() = NeoBAEPrefs.BUILT_IN_REVERB_OPTIONS
internal val CUSTOM_REVERB_TYPE get() = NeoBAEPrefs.CUSTOM_REVERB_TYPE
