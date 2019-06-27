package com.example.whatsapp;

public class Usuario {

    private String uid, nombre, telefono;

    public Usuario(String uid, String nombre, String telefono){
        this.uid = uid;
        this.nombre = nombre;
        this.telefono = telefono;

    }

    public String getUid() {
        return uid;
    }

    public String getTelefono(){
        return telefono;
    }

    public String getNombre(){
        return nombre;
    }

    public void setNombre(String nombre) {
        this.nombre = nombre;
    }

    public void setTelefono(String telefono) {
        this.telefono = telefono;
    }
}
