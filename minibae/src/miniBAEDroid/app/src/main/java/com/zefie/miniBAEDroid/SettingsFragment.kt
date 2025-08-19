package com.zefie.miniBAEDroid

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.fragment.app.Fragment
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import org.minibae.Mixer

class SettingsFragment: Fragment(){

    private val prefName = "miniBAE_prefs"
    private val keyBankPath = "last_bank_path"
    private val builtinMarker = "__builtin__"
    private val keyMasterVol = "master_volume"
    private val keyCurve = "velocity_curve"
    private val keyReverb = "default_reverb"

    private val openHsbPicker = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val data: Intent? = result.data
            data?.data?.let { uri ->
                requireContext().contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                val cached = copyUriToCache(uri, "selected_bank.hsb")
                if (cached != null) {
                    // Read bytes and call native memory loader to avoid filesystem path issues
                    val bytes = cached.readBytes()
                    val r = Mixer.addBankFromMemory(bytes)
                    if (r == 0) {
                        val friendly = Mixer.getBankFriendlyName()
                        val fallback = uri.lastPathSegment ?: cached.name
                        view?.findViewById<TextView>(R.id.bank_name)?.text = friendly ?: fallback
                        val prefs = requireContext().getSharedPreferences(prefName, Context.MODE_PRIVATE)
                        prefs.edit().putString(keyBankPath, cached.absolutePath).apply()
                    } else {
                        view?.findViewById<TextView>(R.id.bank_name)?.text = "Failed to load bank (err=$r)"
                    }
                }
            }
        }
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
        val v = inflater.inflate(R.layout.fragment_settings, container, false)
        // Shared prefs (moved up so UI controls can read persisted settings)
        val prefs = requireContext().getSharedPreferences(prefName, Context.MODE_PRIVATE)

        val reverbSpinner = v.findViewById<Spinner>(R.id.reverb_spinner)
        val reverbOptions = listOf("None","Igor's Closet","Igor's Garage","Igor's Acoustic Lab","Igor's Cavern","Igor's Dungeon","Small Reflections","Early Reflections","Basement","Banquet Hall","Catacombs")
        reverbSpinner.adapter = ArrayAdapter(requireContext(), android.R.layout.simple_spinner_dropdown_item, reverbOptions)

        // Restore saved reverb (engine uses 1-based types). Default to 1 (None).
        val savedReverbEngine = prefs.getInt(keyReverb, 1)
        val spinnerIndex = if (savedReverbEngine >= 1) savedReverbEngine - 1 else 0
        reverbSpinner.setSelection(spinnerIndex)
        // Propagate restored value to native mixer (no-op if mixer not created)
        Mixer.setDefaultReverb(savedReverbEngine)

        reverbSpinner.setOnItemSelectedListener(object: android.widget.AdapterView.OnItemSelectedListener{
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                val engineValue = position + 1 // spinner pos 0 -> engine 1
                Mixer.setDefaultReverb(engineValue)
                prefs.edit().putInt(keyReverb, engineValue).apply()
            }
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        })

    val bankBtn = v.findViewById<Button>(R.id.load_bank)
    val builtinBtn = v.findViewById<Button>(R.id.load_builtin_patches)
    val bankNameTv = v.findViewById<TextView>(R.id.bank_name)
        bankBtn.setOnClickListener {
            // Launch SAF picker for .hsb
            val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
                putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/hsb", "application/x-hsb"))
            }
            openHsbPicker.launch(i)
        }

        builtinBtn.setOnClickListener {
            // Load embedded built-in patches compiled into the native library
            val r = Mixer.addBuiltInPatches()
            val prefs = requireContext().getSharedPreferences(prefName, Context.MODE_PRIVATE)
            if (r == 0) {
                val friendly = Mixer.getBankFriendlyName()
                bankNameTv.text = friendly ?: "Built-in patches"
                // Persist that builtin patches are selected so next launch restores them
                prefs.edit().putString(keyBankPath, builtinMarker).apply()
            } else {
                bankNameTv.text = "Failed to load built-in patches (err=$r)"
            }
        }

    val volumeSeek = v.findViewById<SeekBar>(R.id.master_volume)
        val savedVol = prefs.getInt(keyMasterVol, 75)
        volumeSeek.progress = savedVol
        Mixer.setMasterVolumePercent(savedVol)
        volumeSeek.setOnSeekBarChangeListener(object: SeekBar.OnSeekBarChangeListener{
            override fun onProgressChanged(p0: SeekBar?, p1: Int, p2: Boolean) {
                Mixer.setMasterVolumePercent(p1)
                prefs.edit().putInt(keyMasterVol, p1).apply()
            }
            override fun onStartTrackingTouch(p0: SeekBar?) {}
            override fun onStopTrackingTouch(p0: SeekBar?) {}
        })

        val curveSpinner = v.findViewById<Spinner>(R.id.curve_spinner)
        val curveOptions = listOf("Default","Peaky","WebTV","Expo","Linear")
        curveSpinner.adapter = ArrayAdapter(requireContext(), android.R.layout.simple_spinner_dropdown_item, curveOptions)
        val savedCurve = prefs.getInt(keyCurve, 0)
        curveSpinner.setSelection(savedCurve)
        Mixer.setDefaultVelocityCurve(savedCurve)
        curveSpinner.setOnItemSelectedListener(object: android.widget.AdapterView.OnItemSelectedListener{
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                Mixer.setDefaultVelocityCurve(position)
                prefs.edit().putInt(keyCurve, position).apply()
            }
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        })

        // If the native mixer already has a bank loaded, show its friendly name
        // and skip the restore/auto-load logic. Otherwise restore last bank if
        // persisted; if none or set to builtin, load built-in.
        val currentFriendly = Mixer.getBankFriendlyName()
        if (!currentFriendly.isNullOrEmpty()) {
            bankNameTv.text = currentFriendly
        } else {
            val lastBank = prefs.getString(keyBankPath, null)
            if (lastBank.isNullOrEmpty() || lastBank == builtinMarker) {
                val r = Mixer.addBuiltInPatches()
                if (r == 0) {
                    val friendly = Mixer.getBankFriendlyName()
                    bankNameTv.text = friendly ?: "Built-in patches"
                    prefs.edit().putString(keyBankPath, builtinMarker).apply()
                } else {
                    bankNameTv.text = "Failed to load built-in patches (err=$r)"
                }
            } else {
                val f = File(lastBank)
                if (f.exists()) {
                    try {
                        val bytes = f.readBytes()
                        val r = Mixer.addBankFromMemory(bytes)
                        if (r == 0) {
                            val friendly = Mixer.getBankFriendlyName()
                            bankNameTv.text = friendly ?: f.name
                        } else {
                            bankNameTv.text = "Failed to load saved bank (err=$r)"
                        }
                    } catch (ex: Exception) {
                        bankNameTv.text = "Failed to read saved bank"
                    }
                } else {
                    // Saved path missing on disk; fall back to builtin patches
                    val r = Mixer.addBuiltInPatches()
                    if (r == 0) {
                        val friendly = Mixer.getBankFriendlyName()
                        bankNameTv.text = friendly ?: "Built-in patches"
                        prefs.edit().putString(keyBankPath, builtinMarker).apply()
                    } else {
                        bankNameTv.text = "Failed to load saved bank and built-in (err=$r)"
                    }
                }
            }
        }

        return v
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
