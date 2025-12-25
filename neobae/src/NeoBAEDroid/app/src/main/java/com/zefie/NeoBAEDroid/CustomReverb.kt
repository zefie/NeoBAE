package com.zefie.NeoBAEDroid

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.zefie.NeoBAE.Mixer

private const val PREF_NAME = "NeoBAE_prefs"
private const val KEY_ACTIVE_PRESET = "custom_reverb_preset"
private const val KEY_CURRENT_LOWPASS = "custom_reverb_lowpass"

// Match the engine/desktop limit (GenPriv.h: MAX_NEO_COMBS)
private const val MAX_COMBS = 4
private const val MAX_DELAY_MS = 500

// Defaults that match the engine's initialization (GenReverbNeo.c) reasonably closely.
// (The engine stores custom delays internally as frames; these are approximate ms defaults.)
private const val DEFAULT_COMB_COUNT = 4
private val DEFAULT_DELAYS_MS = intArrayOf(23, 29, 36, 43)
private const val DEFAULT_FEEDBACK = 112 // ~0.75 feedback mapped to 0..127 (max is ~0.85)
private const val DEFAULT_GAIN = 127
private const val DEFAULT_LOWPASS = 64

data class CustomReverbPreset(
    val name: String,
    val combCount: Int,
    val delaysMs: IntArray,
    val feedback: IntArray,
    val gain: IntArray,
    val lowpass: Int
)

private fun prefs(ctx: Context): SharedPreferences =
    ctx.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

fun getActiveCustomReverbPresetName(ctx: Context): String? {
    val name = prefs(ctx).getString(KEY_ACTIVE_PRESET, null)?.trim()
    return if (name.isNullOrEmpty()) null else name
}

fun setActiveCustomReverbPresetName(ctx: Context, name: String?) {
    val e = prefs(ctx).edit()
    if (name.isNullOrBlank()) e.remove(KEY_ACTIVE_PRESET) else e.putString(KEY_ACTIVE_PRESET, name)
    e.apply()
}

fun loadCustomReverbPresetNames(ctx: Context): List<String> {
    val allKeys = prefs(ctx).all.keys
    var maxIdx = -1
    val re = Regex("^custom_reverb_(\\d+)_name$")
    for (k in allKeys) {
        val m = re.find(k) ?: continue
        val idx = m.groupValues[1].toIntOrNull() ?: continue
        if (idx > maxIdx) maxIdx = idx
    }

    if (maxIdx < 0) return emptyList()

    val out = ArrayList<String>()
    val p = prefs(ctx)
    for (idx in 0..maxIdx) {
        val name = p.getString("custom_reverb_${idx}_name", null)?.trim()
        if (!name.isNullOrEmpty()) out.add(name)
    }
    return out
}

private fun findPresetIndexByName(p: SharedPreferences, name: String): Int? {
    val target = name.trim()
    if (target.isEmpty()) return null

    val allKeys = p.all.keys
    val re = Regex("^custom_reverb_(\\d+)_name$")
    for (k in allKeys) {
        val m = re.find(k) ?: continue
        val idx = m.groupValues[1].toIntOrNull() ?: continue
        val v = p.getString(k, null) ?: continue
        if (v.trim() == target) return idx
    }
    return null
}

private fun getMaxPresetIndex(p: SharedPreferences): Int {
    var maxIdx = -1
    val re = Regex("^custom_reverb_(\\d+)_name$")
    for (k in p.all.keys) {
        val m = re.find(k) ?: continue
        val idx = m.groupValues[1].toIntOrNull() ?: continue
        if (idx > maxIdx) maxIdx = idx
    }
    return maxIdx
}

fun snapshotCustomReverbFromEngine(ctx: Context, name: String): CustomReverbPreset {
    val p = prefs(ctx)
    val combCount = Mixer.getNeoCustomReverbCombCount().coerceIn(1, MAX_COMBS)
    val delays = IntArray(MAX_COMBS) { i -> Mixer.getNeoCustomReverbCombDelay(i).coerceIn(1, MAX_DELAY_MS) }
    val feedback = IntArray(MAX_COMBS) { i -> Mixer.getNeoCustomReverbCombFeedback(i).coerceIn(0, 127) }
    val gain = IntArray(MAX_COMBS) { i -> Mixer.getNeoCustomReverbCombGain(i).coerceIn(0, 127) }
    val lowpass = p.getInt(KEY_CURRENT_LOWPASS, DEFAULT_LOWPASS).coerceIn(0, 127)
    return CustomReverbPreset(name.trim(), combCount, delays, feedback, gain, lowpass)
}

fun loadCustomReverbPreset(ctx: Context, name: String): CustomReverbPreset? {
    val p = prefs(ctx)
    val idx = findPresetIndexByName(p, name) ?: return null

    val presetName = p.getString("custom_reverb_${idx}_name", null)?.trim() ?: return null
    val combCount = p.getInt("custom_reverb_${idx}_comb_count", DEFAULT_COMB_COUNT).coerceIn(1, MAX_COMBS)

    val delays = IntArray(MAX_COMBS) { i -> p.getInt("custom_reverb_${idx}_delay_${i}", DEFAULT_DELAYS_MS[i]).coerceIn(1, MAX_DELAY_MS) }
    val feedback = IntArray(MAX_COMBS) { i -> p.getInt("custom_reverb_${idx}_feedback_${i}", DEFAULT_FEEDBACK).coerceIn(0, 127) }
    val gain = IntArray(MAX_COMBS) { i -> p.getInt("custom_reverb_${idx}_gain_${i}", DEFAULT_GAIN).coerceIn(0, 127) }
    val lowpass = p.getInt("custom_reverb_${idx}_lowpass", DEFAULT_LOWPASS).coerceIn(0, 127)

    return CustomReverbPreset(presetName, combCount, delays, feedback, gain, lowpass)
}

fun applyCustomReverbPresetToEngine(ctx: Context, preset: CustomReverbPreset) {
    Mixer.setNeoCustomReverbCombCount(preset.combCount)
    for (i in 0 until MAX_COMBS) {
        Mixer.setNeoCustomReverbCombDelay(i, preset.delaysMs[i])
        Mixer.setNeoCustomReverbCombFeedback(i, preset.feedback[i])
        Mixer.setNeoCustomReverbCombGain(i, preset.gain[i])
    }
    Mixer.setNeoCustomReverbLowpass(preset.lowpass)
    prefs(ctx).edit().putInt(KEY_CURRENT_LOWPASS, preset.lowpass).apply()
}

fun applyDefaultCustomReverbToEngine(ctx: Context) {
    Mixer.setNeoCustomReverbCombCount(DEFAULT_COMB_COUNT)
    for (i in 0 until MAX_COMBS) {
        Mixer.setNeoCustomReverbCombDelay(i, DEFAULT_DELAYS_MS[i])
        Mixer.setNeoCustomReverbCombFeedback(i, DEFAULT_FEEDBACK)
        Mixer.setNeoCustomReverbCombGain(i, DEFAULT_GAIN)
    }
    Mixer.setNeoCustomReverbLowpass(DEFAULT_LOWPASS)
    prefs(ctx).edit().putInt(KEY_CURRENT_LOWPASS, DEFAULT_LOWPASS).apply()
}

fun saveCustomReverbPreset(ctx: Context, preset: CustomReverbPreset) {
    val p = prefs(ctx)
    val existingIdx = findPresetIndexByName(p, preset.name)
    val idx = existingIdx ?: (getMaxPresetIndex(p) + 1).coerceAtLeast(0)

    val e = p.edit()
    e.putString("custom_reverb_${idx}_name", preset.name)
    e.putInt("custom_reverb_${idx}_comb_count", preset.combCount)
    for (i in 0 until MAX_COMBS) {
        e.putInt("custom_reverb_${idx}_delay_${i}", preset.delaysMs[i])
        e.putInt("custom_reverb_${idx}_feedback_${i}", preset.feedback[i])
        e.putInt("custom_reverb_${idx}_gain_${i}", preset.gain[i])
    }
    e.putInt("custom_reverb_${idx}_lowpass", preset.lowpass)
    e.apply()

    setActiveCustomReverbPresetName(ctx, preset.name)
}

fun deleteCustomReverbPreset(ctx: Context, name: String): Boolean {
    val p = prefs(ctx)
    val idx = findPresetIndexByName(p, name) ?: return false

    val e = p.edit()
    e.remove("custom_reverb_${idx}_name")
    e.remove("custom_reverb_${idx}_comb_count")
    for (i in 0 until MAX_COMBS) {
        e.remove("custom_reverb_${idx}_delay_${i}")
        e.remove("custom_reverb_${idx}_feedback_${i}")
        e.remove("custom_reverb_${idx}_gain_${i}")
    }
    e.remove("custom_reverb_${idx}_lowpass")
    e.apply()

    val active = getActiveCustomReverbPresetName(ctx)
    if (active != null && active == name.trim()) {
        setActiveCustomReverbPresetName(ctx, null)
    }

    return true
}

@Composable
fun CustomReverbScreenContent(
    ctx: Context,
    syncSerial: Int,
    onLowpassChanged: (Int) -> Unit
) {
    var combCount by remember { mutableStateOf(DEFAULT_COMB_COUNT) }
    var delays by remember { mutableStateOf(IntArray(MAX_COMBS) { DEFAULT_DELAYS_MS[it] }) }
    var feedback by remember { mutableStateOf(IntArray(MAX_COMBS) { DEFAULT_FEEDBACK }) }
    var gain by remember { mutableStateOf(IntArray(MAX_COMBS) { DEFAULT_GAIN }) }
    var lowpass by remember { mutableStateOf(prefs(ctx).getInt(KEY_CURRENT_LOWPASS, DEFAULT_LOWPASS).coerceIn(0, 127)) }

    fun reloadFromEngine() {
        combCount = Mixer.getNeoCustomReverbCombCount().coerceIn(1, MAX_COMBS)
        delays = IntArray(MAX_COMBS) { i -> Mixer.getNeoCustomReverbCombDelay(i).coerceIn(1, MAX_DELAY_MS) }
        feedback = IntArray(MAX_COMBS) { i -> Mixer.getNeoCustomReverbCombFeedback(i).coerceIn(0, 127) }
        gain = IntArray(MAX_COMBS) { i -> Mixer.getNeoCustomReverbCombGain(i).coerceIn(0, 127) }
        lowpass = prefs(ctx).getInt(KEY_CURRENT_LOWPASS, DEFAULT_LOWPASS).coerceIn(0, 127)
    }

    LaunchedEffect(syncSerial) {
        reloadFromEngine()
    }

    val scroll = rememberScrollState()
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(scroll)
            .padding(16.dp)
    ) {
        Text("Custom Reverb Settings", style = MaterialTheme.typography.h6)
        Spacer(Modifier.height(12.dp))

        Text("Comb Count: $combCount")
        Slider(
            value = combCount.toFloat(),
            onValueChange = { v ->
                val newVal = v.toInt().coerceIn(1, MAX_COMBS)
                if (newVal != combCount) {
                    combCount = newVal
                    Mixer.setNeoCustomReverbCombCount(combCount)
                }
            },
            valueRange = 1f..MAX_COMBS.toFloat(),
            steps = (MAX_COMBS - 2).coerceAtLeast(0)
        )

        Spacer(Modifier.height(8.dp))

        for (i in 0 until combCount) {
            Card(elevation = 2.dp, modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text("Comb ${i + 1}", style = MaterialTheme.typography.subtitle1)
                    Spacer(Modifier.height(8.dp))

                    Text("Delay (ms): ${delays[i]}")
                    Slider(
                        value = delays[i].toFloat(),
                        onValueChange = { v ->
                            val newVal = v.toInt().coerceIn(1, MAX_DELAY_MS)
                            if (newVal != delays[i]) {
                                val arr = delays.clone()
                                arr[i] = newVal
                                delays = arr
                                Mixer.setNeoCustomReverbCombDelay(i, newVal)
                            }
                        },
                        valueRange = 1f..MAX_DELAY_MS.toFloat()
                    )

                    Text("Feedback: ${feedback[i]}")
                    Slider(
                        value = feedback[i].toFloat(),
                        onValueChange = { v ->
                            val newVal = v.toInt().coerceIn(0, 127)
                            if (newVal != feedback[i]) {
                                val arr = feedback.clone()
                                arr[i] = newVal
                                feedback = arr
                                Mixer.setNeoCustomReverbCombFeedback(i, newVal)
                            }
                        },
                        valueRange = 0f..127f
                    )

                    Text("Gain: ${gain[i]}")
                    Slider(
                        value = gain[i].toFloat(),
                        onValueChange = { v ->
                            val newVal = v.toInt().coerceIn(0, 127)
                            if (newVal != gain[i]) {
                                val arr = gain.clone()
                                arr[i] = newVal
                                gain = arr
                                Mixer.setNeoCustomReverbCombGain(i, newVal)
                            }
                        },
                        valueRange = 0f..127f
                    )
                }
            }
            Spacer(Modifier.height(12.dp))
        }

        Text("Low-pass: $lowpass")
        Slider(
            value = lowpass.toFloat(),
            onValueChange = { v ->
                val newVal = v.toInt().coerceIn(0, 127)
                if (newVal != lowpass) {
                    lowpass = newVal
                    Mixer.setNeoCustomReverbLowpass(newVal)
                    prefs(ctx).edit().putInt(KEY_CURRENT_LOWPASS, newVal).apply()
                    onLowpassChanged(newVal)
                }
            },
            valueRange = 0f..127f
        )

        Spacer(Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "Changes apply immediately.",
                style = MaterialTheme.typography.caption
            )
        }
    }
}
