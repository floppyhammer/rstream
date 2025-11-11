package com.gst.android.demo

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView

data class Host(val name: String, val ipAddress: String)

class HostAdapter(private val hosts: MutableList<Host>, private val onItemClick: (Host) -> Unit) :
    RecyclerView.Adapter<HostAdapter.HostViewHolder>() {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): HostViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.host_item, parent, false)
        return HostViewHolder(view)
    }

    override fun onBindViewHolder(holder: HostViewHolder, position: Int) {
        val host = hosts[position]
        holder.bind(host)
        holder.itemView.setOnClickListener { onItemClick(host) }
    }

    override fun getItemCount() = hosts.size

    fun updateHosts(newHosts: List<Host>) {
        hosts.clear()
        hosts.addAll(newHosts)
        notifyDataSetChanged()
    }

    class HostViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val nameTextView: TextView = itemView.findViewById(R.id.host_name)
        private val ipTextView: TextView = itemView.findViewById(R.id.host_ip)

        fun bind(host: Host) {
            nameTextView.text = host.name
            ipTextView.text = host.ipAddress
        }
    }
}