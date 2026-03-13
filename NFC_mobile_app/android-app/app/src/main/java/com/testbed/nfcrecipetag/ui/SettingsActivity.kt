package com.testbed.nfcrecipetag.ui

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
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
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }
}
