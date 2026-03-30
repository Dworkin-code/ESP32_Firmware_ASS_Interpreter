package com.testbed.nfcrecipetag.ui

import android.os.Bundle
import android.widget.ArrayAdapter
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.core.cellprofile.CellProfileRegistry
import com.testbed.nfcrecipetag.core.cellprofile.getActiveCellProfileId
import com.testbed.nfcrecipetag.core.cellprofile.setActiveCellProfileId
import com.testbed.nfcrecipetag.databinding.ActivitySettingsBinding

class SettingsActivity : AppCompatActivity() {

    private lateinit var binding: ActivitySettingsBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        binding.switchWriteEnabled.isChecked = prefs.getBoolean("write_enabled", false)
        binding.switchExpertMode.isChecked = prefs.getBoolean("expert_mode", false)
        binding.switchWriteEnabled.setOnCheckedChangeListener { _, checked ->
            prefs.edit().putBoolean("write_enabled", checked).apply()
        }
        binding.switchExpertMode.setOnCheckedChangeListener { _, checked ->
            prefs.edit().putBoolean("expert_mode", checked).apply()
        }

        // Cell profile: list all profiles, persist selection
        val profiles = CellProfileRegistry.ALL_PROFILES
        val displayNames = profiles.map { it.displayName }
        binding.spinnerCellProfile.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, displayNames).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        val currentId = getActiveCellProfileId()
        val selectedIndex = profiles.indexOfFirst { it.id == currentId }.coerceAtLeast(0)
        binding.spinnerCellProfile.setSelection(selectedIndex)
        binding.spinnerCellProfile.setOnItemSelectedListener(object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                setActiveCellProfileId(profiles[position].id)
            }
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        })
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }
}
