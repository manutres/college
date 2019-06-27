package com.example.whatsapp;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.ArrayList;

public class ListaUsuarioAdaptador extends RecyclerView.Adapter<ListaUsuarioAdaptador.VistaUsuario> {

    ArrayList<Usuario> usuarios;

    public ListaUsuarioAdaptador(ArrayList usuarios){
        this.usuarios = usuarios;
    }

    @NonNull
    @Override
    public VistaUsuario onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View layoutView = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_usuario, null, false);
        RecyclerView.LayoutParams lp = new RecyclerView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        layoutView.setLayoutParams(lp);

        VistaUsuario vu = new VistaUsuario(layoutView);
        return vu;
    }

    @Override
    public void onBindViewHolder(@NonNull VistaUsuario holder, int position) {
        holder.nombre.setText(usuarios.get(position).getNombre());
        holder.telefono.setText(usuarios.get(position).getTelefono());

        holder.layaout.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                //se necesita la id del usuario de la base de datos y del chat.
                String idChat;
                //indicar a la base de datos que el chat esta activo para el usuario.


            }
        });
    }

    @Override
    public int getItemCount() {
        return usuarios.size();
    }

    public class VistaUsuario extends RecyclerView.ViewHolder {
        public TextView nombre, telefono;
        public LinearLayout layaout;

        public VistaUsuario(View view) {
            super(view);

            nombre = view.findViewById(R.id.nombre);
            telefono = view.findViewById(R.id.telefono);
            layaout = view.findViewById(R.id.Usuario);
        }
    }
}
