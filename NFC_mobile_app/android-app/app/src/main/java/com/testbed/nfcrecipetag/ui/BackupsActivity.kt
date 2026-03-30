package com.testbed.nfcrecipetag.ui

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.backup.BackupEntry
import com.testbed.nfcrecipetag.core.backup.BackupManager
import com.testbed.nfcrecipetag.databinding.ActivityBackupsBinding

class BackupsActivity : AppCompatActivity() {

    private lateinit var binding: ActivityBackupsBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityBackupsBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        binding.recyclerBackups.layoutManager = LinearLayoutManager(this)
        val list = BackupManager(this).listBackups()
        binding.recyclerBackups.adapter = BackupsAdapter(list) { entry ->
            val dump = BackupManager(this).loadBackupRaw(entry.metaPath)
            if (dump != null) TagDetailActivity.startFromBackup(this, dump)
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }
}

class BackupsAdapter(
    private val entries: List<BackupEntry>,
    private val onItemClick: (BackupEntry) -> Unit
) : RecyclerView.Adapter<BackupsAdapter.VH>() {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val v = LayoutInflater.from(parent.context).inflate(android.R.layout.simple_list_item_2, parent, false)
        return VH(v)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val e = entries[position]
        holder.text1.text = e.uid
        holder.text2.text = "${e.tagType}  ${e.timestamp}"
        holder.itemView.setOnClickListener { onItemClick(e) }
    }

    override fun getItemCount() = entries.size

    class VH(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val text1: TextView = itemView.findViewById(android.R.id.text1)
        val text2: TextView = itemView.findViewById(android.R.id.text2)
    }
}
