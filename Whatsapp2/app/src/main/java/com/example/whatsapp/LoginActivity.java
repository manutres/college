package com.example.whatsapp;

import androidx.appcompat.app.AppCompatActivity;

import android.content.Intent;
import android.os.Bundle;
import android.text.Editable;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;

public class LoginActivity extends AppCompatActivity {

    private EditText telefono, codigo;
    private Button enviar, prueba;



    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        //inicialización de base de datos

        telefono = findViewById(R.id.telefono);
        codigo = findViewById(R.id.codigo);

        enviar = findViewById(R.id.verificar);





        enviar.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                verificarTelefono();
            }
        });


    }



    private void usuarioLogeado(){
        //recuperar usuario de la base de datos
        String user ="";
        if(user != null) {
            startActivity(new Intent(getApplicationContext(), PaginaPrincipal.class));
            finish();
            return;
        }
    }
    private void verificarTelefono(){
        //comprobar con la base de datos el telefono
        usuarioLogeado();

    }
}
