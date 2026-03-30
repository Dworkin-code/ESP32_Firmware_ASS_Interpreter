package com.testbed.nfcrecipetag.ui

import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.tagmodel.ProcessTypes
import com.testbed.nfcrecipetag.core.tagmodel.RecipeStep

class StepsAdapter(private val steps: List<RecipeStep>) : RecyclerView.Adapter<StepsAdapter.VH>() {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val v = LayoutInflater.from(parent.context).inflate(android.R.layout.simple_list_item_2, parent, false)
        return VH(v)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        val s = steps[position]
        val typeName = ProcessTypes.name(s.typeOfProcess)
        if (Log.isLoggable("StepsAdapter", Log.DEBUG)) {
            Log.d("StepsAdapter", "Step ${position + 1}: Decoded TypeOfProcess=${s.typeOfProcess} | Displayed step type=$typeName")
        }
        val detail =
            when (s.typeOfProcess) {
                ProcessTypes.SHAKER ->
                    "${typeName} ${s.parameterProcess1} s"
                ProcessTypes.STORAGE_ALCOHOL,
                ProcessTypes.STORAGE_NON_ALCOHOL -> {
                    val drink = ProcessTypes.drinkName(s.typeOfProcess, s.parameterProcess1)
                        ?: "$typeName#${s.parameterProcess1}"
                    "$drink ${s.parameterProcess2} ml"
                }
                ProcessTypes.TO_STORAGE_GLASS ->
                    "Return to storage"
                else ->
                    "$typeName (param1=${s.parameterProcess1}, param2=${s.parameterProcess2})"
            }
        holder.text1.text = "Step ${position + 1}: $detail"
        holder.text2.text = "ID=${s.id} type byte=${s.typeOfProcess} ProcessCell=${s.processCellId}"
    }

    override fun getItemCount() = steps.size

    class VH(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val text1: TextView = itemView.findViewById(android.R.id.text1)
        val text2: TextView = itemView.findViewById(android.R.id.text2)
    }
}
